/* * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * *
 * Proyecto: MSX-SDF-1                                                       *
 * Autor: Carlos Escobar                                                     *
 * Abr-2023                                                                  *
 * Board ATmega328 20mhz  (el cristal es de 20 MHz; el BOM v1.1 esta mal,    *
 *                         ver hardware/rev1/README.md)                      *
 * Additional Boards Manager:                                                *
 *  https://mcudude.github.io/MiniCore/package_MCUdude_MiniCore_index.json   *
 *  Tools->MiniCore->ATmega328                                               *
 * Libraries:                                                                *
 *  Adafruit SH1106                                                          *
 *  SdFat - Adafruit Fork: https://github.com/adafruit/SdFat                 *
 *   (NO la de greiman: el sketch usa SPI_FULL_SPEED y el typedef File)      *
 *                                                                           *
 * Las interrupciones son PCINT nativo, sin libreria. Antes se usaba         *
 * YetAnotherArduinoPcIntLibrary; ya no hace falta instalarla.               *
 *                                                                           *
 * Sin el IDE, desde la raiz del repo:  make firmware                        *
 * El FQBN con el reloj fijo esta en el Makefile.                            *
 * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * */

#include "defs.h"
//#include <U8x8lib.h>
#include <SdFat.h>
#include <EEPROM.h>
#include <util/delay.h>

#define SCREEN_WIDTH 128 // Ancho del display OLED
#define SCREEN_HEIGHT 64 // Alto del display OLED
#define i2c_Address 0x3c //initialize with the I2C addr 0x3C Typically eBay OLED's
#define OLED_RESET -1   //   QT-PY / XIAO

//U8X8_SH1106_128X64_NONAME_HW_I2C u8x8(/* reset=*/ U8X8_PIN_NONE);

SdFat SD;
char curFile[14];
File dsk;
File dir;
volatile bool _debug = false;
volatile uint8_t _stat=0;
volatile uint8_t _cmd;
volatile uint8_t _cmd_st;
volatile uint8_t _checksum=0;
volatile uint32_t _total=0;
volatile uint8_t _drive_number, _last_drv=0;
volatile uint8_t _n_sectors;
volatile uint8_t _media;
volatile uint8_t _sec_H;
volatile uint8_t _sec_L;
volatile uint8_t _addr_H;
volatile uint8_t _addr_L;
volatile uint16_t _sector;
volatile uint16_t _address;
volatile uint32_t _sector_pos;
volatile uint16_t _idx_sec=0;
volatile byte _disk_number = 0;

// Imagenes montadas en A y B, por su nombre 8.3; vacio = drive sin imagen.
// Arrancan con lo que quedo en la EEPROM (ver loadMounted) y CALL SDFMOUNT
// las cambia.
char _mounted[2][DSK_NAME_LEN];
volatile uint8_t _dsk_changed[2] = { 0, 0 }; //lo levanta CMD_SDFMOUNT, lo consume CMD_DSKCHG
volatile bool _save_mounted = false;         //lo levanta CMD_SDFMOUNT, lo graba loop()
char _mount_name[DSK_NAME_LEN];
volatile uint8_t _mount_drive, _mount_len, _mount_idx, _mount_result;
volatile uint8_t _io_status;
char _list_name[DSK_NAME_LEN];
volatile uint8_t _list_idx;
volatile uint8_t _test_idx;
char _test_msg[TEST_MSG_MAX + 1]; //lo arma buildTestMsg, lo manda CMD_SDFTEST

// La SD la inicializa loop(), reintentando hasta que ande, y no setup(): asi
// el MSX tiene quien le conteste aunque no haya tarjeta. Mientras _sd_ok sea
// false la ISR no toca la SD, porque loop() puede estar en medio de un
// SD.begin() usando el SPI. Pasa a true con la raiz abierta y las imagenes
// montadas ya recuperadas de la EEPROM.
volatile bool _sd_ok = false;
volatile uint8_t _sd_error = SD_NOT_TRIED; //sdErrorCode() del ultimo intento fallido

// CALL SDFNEW: la ISR recibe el pedido y loop() crea la imagen, porque llenarla
// de ceros tarda segundos. Mientras _sd_busy este en true el SPI es de loop() y
// la ISR no toca la SD. La imagen creada queda en _new_name, que es el drive
// NEW_DRIVE: por ahi la ROM le escribe los sectores de sistema.
char _new_name[DSK_NAME_LEN];
volatile uint8_t _new_media;
volatile uint8_t _new_result;
volatile bool _new_request = false;
volatile bool _sd_busy = false;
volatile uint8_t _fmt_media; //respuesta de CMD_DSKFMT

// La ISR puede usar la SD: ya esta inicializada y loop() no la esta usando.
inline bool sdReady()
{
  return _sd_ok && !_sd_busy;
}

const char* diskFile(uint8_t drive)
{
  if (drive == NEW_DRIVE)
    return _new_name; //la imagen que acaba de crear CALL SDFNEW
  return _mounted[drive ? 1 : 0];
}

// Avanza dir hasta el proximo .DSK de la raiz y deja su nombre en _list_name.
// Usa el nombre corto 8.3, que es el que entiende MSX-DOS 1, en mayusculas.
// Devuelve false cuando no quedan mas.
bool nextDskName()
{
  while (true)
  {
    File entry = dir.openNextFile();
    if (!entry)
      return false; // no more files

    bool ok = !entry.isDir() && !entry.isHidden() && entry.getSFN(_list_name, sizeof(_list_name));
    entry.close();
    if (!ok)
      continue;

    size_t n = strlen(_list_name);
    if (n > 4 && strcasecmp(_list_name + n - 4, ".DSK") == 0)
    {
      for (char *p = _list_name; *p; p++)
        *p = toupper(*p);
      return true;
    }
  }
}

// Verifica que name sea una imagen que la ROM sabe leer. Devuelve 0 o el
// numero de error de BASIC; si esta bien, deja en name su nombre corto.
// Mira solo el largo, no el byte de la FAT: una imagen de juego con cargador
// propio, sin FAT, tambien tiene que montar.
uint8_t checkDskImage(char *name)
{
  if (name[0] == 0)
    return ERR_BAD_FILE_NAME;

  uint8_t err = 0;
  File f = SD.open(name, O_READ);
  if (!f || f.isDir() || !f.getSFN(name, DSK_NAME_LEN))
    err = ERR_FILE_NOT_FOUND;
  else if (f.fileSize() != DSK_720K_SIZE && f.fileSize() != DSK_360K_SIZE)
    err = ERR_BAD_FILE_MODE;
  f.close();
  return err;
}

// Recupera de la EEPROM las imagenes montadas antes de apagar. Si la EEPROM
// esta virgen, o la imagen ya no esta en la SD, el drive queda sin imagen y
// DSKIO le contesta "not ready" al DOS. La EEPROM no se toca: la SD puede
// volver.
void loadMounted()
{
  bool valid = EEPROM.read(EEPROM_MAGIC_ADDR) == EEPROM_MAGIC;
  for (uint8_t d = 0; d < 2; d++)
  {
    for (uint8_t i = 0; i < DSK_NAME_LEN; i++)
      _mounted[d][i] = valid ? EEPROM.read(EEPROM_MOUNTED_ADDR + d * DSK_NAME_LEN + i) : 0;
    _mounted[d][DSK_NAME_LEN - 1] = 0;

    if (checkDskImage(_mounted[d]) != 0)
      _mounted[d][0] = 0;
  }
}

// Cierra CMD_SDFMOUNT: valida el pedido y deja en _mount_result 0 o el numero
// de error de BASIC. La imagen montada se guarda por su nombre corto.
void mountDisk()
{
  _cmd_st = CMD_SDFMOUNT__RESULT;

  if (_mount_drive > 1)
  {
    _mount_result = ERR_BAD_DRIVE_NAME;
    return;
  }
  if (!sdReady())
  {
    _mount_result = ERR_DISK_OFFLINE;
    return;
  }
  if (_mount_len >= sizeof(_mount_name))
  {
    _mount_result = ERR_BAD_FILE_NAME;
    return;
  }
  _mount_name[_mount_len] = 0;

  _mount_result = checkDskImage(_mount_name);
  if (_mount_result == 0)
  {
    strcpy(_mounted[_mount_drive], _mount_name);
    _dsk_changed[_mount_drive] = 1; //el proximo DSKCHG de ese drive avisa al DOS
    _save_mounted = true;           //la EEPROM la graba loop(), no la ISR
  }
}

// Cierra CMD_SDFUMOUNT: deja el drive sin imagen, tambien en la EEPROM.
// Desmontar un drive que ya estaba vacio no es error. Sin SD no se puede:
// _mounted todavia no tiene lo de la EEPROM, y grabarlo pisaria el otro drive.
void umountDisk(uint8_t drive)
{
  _cmd_st = CMD_SDFUMOUNT__RESULT;

  if (drive > 1)
  {
    _mount_result = ERR_BAD_DRIVE_NAME;
    return;
  }
  if (!_sd_ok)
  {
    _mount_result = ERR_DISK_OFFLINE;
    return;
  }
  _mounted[drive][0] = 0;
  _dsk_changed[drive] = 1; //el DOS descarta lo que tenga en buffers de ese drive
  _save_mounted = true;
  _mount_result = 0;
}

// Arma el texto de CALL SDFTEST: la version y, en otra linea, el estado de la
// SD. La ROM lo imprime tal cual despues de "Firmware ".
// ---- Mensajes de prueba hacia el MSX ---------------------------------
//
// CALL SDFDEBUG imprime esto y lo vacia. Para dejar un mensaje desde
// cualquier punto del firmware:
//
//     dbg("monte ");  dbg(_mount_name);  dbg("\r\n");
//     dbg("estado "); dbgHex(_io_status); dbg("\r\n");
//
// Se puede llamar desde loop() y desde la ISR; si las dos escriben a la vez el
// texto sale mezclado, que para una herramienta de prueba alcanza. Lo que no
// entra se descarta: nunca se pasa del buffer.
char _dbg_msg[DEBUG_MSG_MAX];
volatile uint8_t _dbg_len = 0;
volatile uint8_t _dbg_idx = 0;

void dbg(const char *s)
{
  while (*s && _dbg_len < DEBUG_MSG_MAX - 1)
    _dbg_msg[_dbg_len++] = *s++;
  _dbg_msg[_dbg_len] = 0;
}

void dbgHex(uint8_t b)
{
  static const char hex[] = "0123456789ABCDEF";
  char t[3];
  t[0] = hex[b >> 4];
  t[1] = hex[b & 0x0F];
  t[2] = 0;
  dbg(t);
}

void buildTestMsg()
{
  strcpy(_test_msg, FW_VERSION "\r\nSD ");
  if (_sd_ok)
    strcat(_test_msg, "ok");
  else if (_sd_error == SD_NOT_TRIED)
    strcat(_test_msg, "iniciando");
  else if (_sd_error == SD_CARD_ERROR_CMD0)
    strcat(_test_msg, "no contesta"); //sin tarjeta, modulo desenchufado o cableado
  else if (_sd_error == 0)
    strcat(_test_msg, "no es FAT");   //la tarjeta anda: exFAT o sin formatear
  else
  {
    static const char hex[] = "0123456789ABCDEF";
    char code[] = "error 0x00";       //el numero es el enum SD_CARD_ERROR de SdFat
    code[8] = hex[_sd_error >> 4];
    code[9] = hex[_sd_error & 0x0f];
    strcat(_test_msg, code);
  }
}
static_assert(sizeof(FW_VERSION "\r\nSD no contesta") - 1 <= TEST_MSG_MAX,
              "el texto de CALL SDFTEST no entra en lo que lee la ROM");

// Pasa el nombre de CALL SDFNEW a 8.3 en mayusculas, con .DSK si no trae
// extension. Devuelve false si no es un nombre corto valido: SdFat crearia un
// nombre largo, y MSX-DOS 1 y SDFMOUNT solo ven el corto. dst tiene lugar
// para DSK_NAME_LEN bytes, y len tiene que ser menor que eso.
bool makeNewName(const char *src, uint8_t len, char *dst)
{
  uint8_t base = 0, ext = 0;
  bool dot = false;
  for (uint8_t i = 0; i < len; i++)
  {
    char c = toupper(src[i]);
    if (c == '.')
    {
      if (dot || base == 0)
        return false;
      dot = true;
    }
    else if (c <= ' ' || c > '~' || strchr("\"*+,/:;<=>?[\\]|", c))
      return false;
    else if (dot ? ++ext > 3 : ++base > 8)
      return false;
    *dst++ = c;
  }
  if (base == 0 || (dot && ext == 0))
    return false;
  strcpy(dst, dot ? "" : ".DSK");
  return true;
}

// Cierra la recepcion de CMD_SDFNEW: valida el pedido y se lo pasa a loop().
// Hasta que loop() termine, la ROM lee SDFNEW_BUSY.
void requestNewDisk()
{
  _cmd_st = CMD_SDFNEW__RESULT;

  if (!sdReady())
    _new_result = ERR_DISK_OFFLINE;
  else if (_new_media != DSK_720K_MEDIA && _new_media != DSK_360K_MEDIA)
    _new_result = ERR_BAD_FILE_MODE;
  else if (_mount_len >= sizeof(_mount_name) || !makeNewName(_mount_name, _mount_len, _new_name))
  {
    _new_name[0] = 0; //lo que haya dejado makeNewName no es una imagen
    _new_result = ERR_BAD_FILE_NAME;
  }
  else
  {
    _sd_busy = true;  //desde ya, la ISR no toca la SD
    _new_result = SDFNEW_BUSY;
    _new_request = true;
  }
}

// Crea la imagen de CALL SDFNEW, desde loop(): la reserva contigua si se puede y
// la llena de ceros, para que no arrastre restos de archivos borrados de la SD.
// Si algo falla la borra, para no dejar una imagen a medias.
void createNewDisk()
{
  static const uint8_t zeros[NEW_ZERO_CHUNK] = { 0 };
  uint32_t left = _new_media == DSK_360K_MEDIA ? DSK_360K_SIZE : DSK_720K_SIZE;
  uint8_t err = 0;

  if (SD.exists(_new_name))
    err = ERR_FILE_ALREADY_EXISTS;
  else
  {
    File f = SD.open(_new_name, O_RDWR | O_CREAT | O_EXCL);
    if (!f)
      err = ERR_DISK_IO; //raiz llena, o la SD no escribe
    else
    {
      f.preAllocate(left); //si no hay lugar contiguo, write() la agranda igual
      while (left > 0 && err == 0)
      {
        uint16_t n = left < sizeof(zeros) ? left : sizeof(zeros);
        if (f.write(zeros, n) != n)
          err = ERR_DISK_FULL;
        left -= n;
      }
      f.close();
      if (err)
        SD.remove(_new_name);
    }
  }

  if (err)
    _new_name[0] = 0;
  _sd_busy = false;  //antes que el resultado: la ROM escribe apenas lo lee
  _new_result = err;
}

// Contesta CMD_DSKFMT: el media del formato que le corresponde a la imagen del
// drive por su largo, o 0 si no hay imagen. La ROM formatea con eso, y el
// proximo DSKCHG de ese drive le avisa al DOS que la relea.
uint8_t formatMedia(uint8_t drive)
{
  if (drive > NEW_DRIVE || !sdReady() || diskFile(drive)[0] == 0)
    return 0;

  uint32_t size = 0;
  File f = SD.open(diskFile(drive), O_READ);
  if (f)
  {
    size = f.fileSize();
    f.close();
  }
  if (drive < NEW_DRIVE)
    _dsk_changed[drive] = 1;
  if (size == DSK_720K_SIZE)
    return DSK_720K_MEDIA;
  if (size == DSK_360K_SIZE)
    return DSK_360K_MEDIA;
  return 0;
}

// Un intento de inicializar la SD, desde loop(). Si anda, abre la raiz,
// recupera las imagenes montadas y recien ahi le habilita la SD a la ISR.
void initSD()
{
  if (!SD.begin(CS, SPI_FULL_SPEED))
  {
    _sd_error = SD.sdErrorCode(); //0 si la tarjeta contesta pero no hay FAT16/FAT32
    return;
  }
  dir = SD.open("/");
  loadMounted();
  _dsk_changed[0] = 1; //si el DOS arranco antes que la SD, que descarte lo que
  _dsk_changed[1] = 1; //tenga en buffers de los dos drives
  _sd_ok = true;
}

// Inicialmente se usaron los pines 0-1 del puerto B para los bits 0-1 del bus de datos
// y los pines 2-7 del puerto D para los bits 2-7 con el objetivo de liberar los pines
// 0 y 1 del puerto D que corresponden a RX y TX, permitiendo hacer debug.
// Ya no es necesario, por eso ahora se usa el puerto D completo para el bus de datos
// facilitando la lectura y escritura de los datos con menos instrucciones.
// Igualmente dejo comentado el codigo original por si fuese necesario usar el puerto
// serie en el futuro.

inline void configDataBusAsInput()
{
  //DDRB = DDRB & 0xfc; //puts bits 0-1 as inputs
  //DDRD = DDRD & 0x03; //puts bits 2-7 as inputs
  DDRD = 0; //puts bits 0-7 as inputs
}

inline void configDataBusAsOutput()
{
  //DDRB = DDRB | 0x03; //puts bits 0-1 as outputs
  //DDRD = DDRD | 0xfc; //puts bits 2-7 as outputs
  DDRD = 0xff; //puts bits 0-7 as outputs
}

/* * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * *
 * Dispositivos de BASIC (OPEN "RTC:") y reloj DS1307                        *
 * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * *
 *
 * La ROM es un tubo: pregunta si un nombre es nuestro (CMD_DEVNAME) y despues
 * pasa bytes. Todo lo que distingue a un dispositivo de otro esta aca, asi que
 * agregar "I2C:" o "GPIO:" es firmware y no obliga a regrabar la EEPROM.
 *
 * REGLA: nada de I2C dentro de la ISR. El MSX espera en /WAIT mientras corre,
 * y una transaccion I2C son cientos de microsegundos sin refresco de DRAM.
 *  - lectura: loop() relee el DS1307 cuatro veces por segundo a una copia en
 *    RAM y la ISR arma la linea desde ahi, sin dividir ni llamar a sprintf.
 *  - escritura: la ISR deja el pedido y contesta DEV_BUSY hasta que loop()
 *    termina, igual que CALL SDFNEW.
 */

// Los siete registros del reloj, en BCD tal como salen del chip: asi armar la
// linea es partir nibbles, sin una sola division.
struct RtcRegs { uint8_t s, mi, h, wd, d, mo, y; };

// Copia doble: loop() llena la que la ISR no esta mirando y recien despues
// cambia el indice. El cambio es de un byte, o sea atomico en el AVR, asi que
// la ISR nunca ve media hora vieja y media nueva.
RtcRegs _rtc_buf[2];
volatile uint8_t _rtc_cur = 0;
volatile bool _rtc_ok = false;   //el chip contesta
volatile bool _rtc_set = false;  //...y ademas esta andando (CH en 0)
uint32_t _rtc_next_ms = 0;

RtcRegs _rtc_pending;                   //lo que pidio PRINT #, para loop()
volatile bool _rtc_write_req = false;

// Reloj del kernel (CHKCLK/$GETTI/$SETDA/$SETTI parcheados en la ROM con JP
// a rutinas propias, camino A de reloj-cuatro-caminos.md). El camino B
// (emular el RP-5C01 completo en los puertos 0/1) se probo primero y fallaba
// por timing: el bucle de deteccion de CHKCLK hace un OUT seguido de un IN
// con solo 8 T-states de separacion (~2,2us a 3,58MHz), muy por debajo de
// los 6us que tarda el firmware en rehabilitar el decoder entre accesos
// (MSX_REENABLE_DELAY_US). El camino A no tiene ese problema: la ROM habla
// con el firmware por el protocolo normal de comando+datos (el mismo que ya
// usan CMD_SDFMOUNT, CMD_DEVIN, etc.), con el margen de los EX (SP),HL de
// WriteByte/WriteCommand/ReadByte.
volatile uint8_t _rtcget_buf[7];   //anio-1980,mes,dia,hora,min,seg,diasem: CMD_RTCGET
volatile uint8_t _rtcget_idx = 0;
volatile uint8_t _rtcset_buf[3];   //CMD_RTCSETDA (anio,mes,dia) o CMD_RTCSETTI (h,m,s)
volatile uint8_t _rtcset_idx = 0;

// Estado del tubo. Entrada y salida son independientes: se puede tener un
// archivo FOR INPUT y otro FOR OUTPUT sobre el mismo dispositivo.
volatile uint8_t _dev_in_id = DEV_NONE, _dev_out_id = DEV_NONE;
volatile uint8_t _dev_in_fmt = RTC_FMT_FULL;
char _dev_line[DEV_LINE_MAX];           //linea armada para el MSX
volatile uint8_t _dev_in_idx = 0xFF;    //0xFF = hay que armarla de nuevo
char _dev_out[DEV_LINE_MAX];            //linea que llega de PRINT #
volatile uint8_t _dev_out_idx = 0;
volatile uint8_t _dev_out_res = 0;
char _dev_name[DEV_NAME_MAX + 1];       //nombre del dispositivo, o texto tras ":"
volatile uint8_t _dev_name_idx = 0;
volatile uint8_t _dev_open_id, _dev_open_mode, _dev_open_len, _dev_open_res;

inline uint8_t bcd2bin(uint8_t b) { return (b >> 4) * 10 + (b & 0x0F); }
inline uint8_t bin2bcd(uint8_t b) { return ((b / 10) << 4) | (b % 10); }

// ---- I2C a mano, sin la libreria Wire ---------------------------------
//
// Wire cuesta 324 bytes de RAM (cinco buffers de 32 y el objeto) y ~2 KB de
// flash, y aca se usa para leer siete registros cuatro veces por segundo. Con
// el TWI en crudo no hace falta un solo byte de buffer. La RAM importa: el
// peor caso de stack es la ISR entrando sobre initSD (GUIA §2).
//
// Solo corre desde loop(). La ISR del MSX puede interrumpirlo: no toca ningun
// registro del TWI, asi que lo unico que pasa es que una transaccion tarde
// unos microsegundos mas.

#define TWI_SCL_HZ    100000UL
#define TWI_TIMEOUT   10000     //vueltas de espera, unos pocos ms

void twiInit()
{
  PORTC |= _BV(PC4) | _BV(PC5);  //pull-ups internos, por si el modulo no trae
  TWSR = 0;                      //preescaler 1
  TWBR = ((F_CPU / TWI_SCL_HZ) - 16) / 2;
  TWCR = _BV(TWEN);
}

static bool twiWait()
{
  for (uint16_t n = 0; n < TWI_TIMEOUT; n++)
    if (TWCR & _BV(TWINT))
      return true;
  return false;                  //bus trabado: mejor salir que colgar loop()
}

// Vale para el START y para el START repetido. addr_rw es la direccion ya
// corrida un bit, con el bit 0 en 1 para leer.
static bool twiStart(uint8_t addr_rw)
{
  TWCR = _BV(TWINT) | _BV(TWSTA) | _BV(TWEN);
  if (!twiWait())
    return false;
  TWDR = addr_rw;
  TWCR = _BV(TWINT) | _BV(TWEN);
  if (!twiWait())
    return false;
  uint8_t st = TWSR & 0xF8;
  return st == 0x18 || st == 0x40;  //SLA+W o SLA+R contestados con ACK
}

static bool twiWrite(uint8_t b)
{
  TWDR = b;
  TWCR = _BV(TWINT) | _BV(TWEN);
  if (!twiWait())
    return false;
  return (TWSR & 0xF8) == 0x28;
}

static uint8_t twiRead(bool ack)
{
  TWCR = _BV(TWINT) | _BV(TWEN) | (ack ? _BV(TWEA) : 0);
  if (!twiWait())
    return 0xFF;
  return TWDR;
}

static void twiStop()
{
  TWCR = _BV(TWINT) | _BV(TWSTO) | _BV(TWEN);
}

// ---- El reloj, solo desde loop() --------------------------------------

void rtcPoll()
{
  if ((int32_t)(millis() - _rtc_next_ms) < 0)
    return;
  _rtc_next_ms = millis() + RTC_POLL_MS;

  if (!twiStart(DS1307_ADDR << 1) || !twiWrite(0) ||
      !twiStart((DS1307_ADDR << 1) | 1))
  {
    twiStop();
    _rtc_ok = false;
    return;
  }

  uint8_t next = 1 - _rtc_cur;
  RtcRegs &r = _rtc_buf[next];
  uint8_t s = twiRead(true);
  r.s  = s & 0x7F;
  r.mi = twiRead(true) & 0x7F;
  uint8_t h = twiRead(true);
  if (h & 0x40)  //el chip quedo en 12 horas: lo paso a 24 para armar la linea
    r.h = bin2bcd(bcd2bin(h & 0x1F) % 12 + ((h & 0x20) ? 12 : 0));
  else
    r.h = h & 0x3F;
  r.wd = twiRead(true) & 0x07;
  r.d  = twiRead(true) & 0x3F;
  r.mo = twiRead(true) & 0x1F;
  r.y  = twiRead(false);
  twiStop();

  _rtc_cur = next;
  _rtc_ok = true;
  _rtc_set = !(s & 0x80);  //CH en 1: el reloj esta parado, nunca lo pusieron
}

// Dia de la semana (1 domingo .. 7 sabado) por Sakamoto. Va en loop() y no en
// la ISR porque son cuatro divisiones.
uint8_t rtcWeekday(uint8_t y2, uint8_t m, uint8_t d)
{
  static const uint8_t t[] = { 0, 3, 2, 5, 0, 3, 5, 1, 4, 6, 2, 4 };
  uint16_t y = 2000 + y2;
  if (m < 3) y--;
  return (y + y / 4 - y / 100 + y / 400 + t[m - 1] + d) % 7 + 1;
}

void rtcWrite()
{
  RtcRegs r;
  noInterrupts();            //la ISR podria estar dejando otro pedido
  r = _rtc_pending;
  _rtc_write_req = false;
  interrupts();

  r.wd = rtcWeekday(bcd2bin(r.y), bcd2bin(r.mo), bcd2bin(r.d));

  bool ok = twiStart(DS1307_ADDR << 1) &&
            twiWrite(0) &&
            twiWrite(r.s) &&   //con el bit 7 en 0 el reloj arranca
            twiWrite(r.mi) &&
            twiWrite(r.h) &&   //con el bit 6 en 0 queda en 24 horas
            twiWrite(r.wd) &&
            twiWrite(r.d) &&
            twiWrite(r.mo) &&
            twiWrite(r.y);
  twiStop();
  _dev_out_res = ok ? 0 : ERR_DEVICE_IO;

  _rtc_next_ms = 0;  //que la copia se actualice ya, sin esperar el poll
}

// ---- Reloj del kernel (CHKCLK/$GETTI/$SETDA/$SETTI parcheados) ---------
//
// CMD_RTCCHK/GET/SETDA/SETTI: protocolo normal, nada de puertos crudos. Los
// valores viajan siempre en binario (no BCD), porque asi los espera el
// kernel (SETYEA, y SETTIM compara contra 18h/3Bh en decimal - ver el
// desensamblado en la nota del proyecto). bcd2bin/bin2bcd ya existen mas
// arriba para el dispositivo BASIC RTC:.

// CMD_RTCGET: arma los 7 bytes de respuesta desde la copia actual del
// DS1307. El DS1307 guarda el anio como 2 digitos asumiendo 20xx (ver
// rtcWeekday); el kernel lo quiere contado desde 1980, de ahi el +20.
//
// OJO: esta funcion (y rtcApplyDate/rtcApplyTime) se llaman desde
// processCommand()/processData(), que a su vez corren DENTRO de
// ISR(PCINT1_vect) - a diferencia de rtcWrite(), que solo se llama desde
// loop(). Por eso NO llevan noInterrupts()/interrupts(): las interrupciones
// ya estan deshabilitadas por estar dentro de una ISR, y un interrupts()
// aca adentro las reactivaria antes de que la ISR del MSX termine, dejando
// que se anide otra interrupcion sobre esta. Leer _rtc_cur/_rtc_buf sin el
// guard es seguro: la ISR nunca corre en paralelo con loop() (es la misma
// CPU), solo puede interrumpirla, y _rtc_cur cambia de a un byte (atomico).
void rtcBuildGetBuf()
{
  RtcRegs r = _rtc_buf[_rtc_cur];

  _rtcget_buf[0] = bcd2bin(r.y) + 20;  //anio-1980 (DS1307: 20xx)
  _rtcget_buf[1] = bcd2bin(r.mo);
  _rtcget_buf[2] = bcd2bin(r.d);
  _rtcget_buf[3] = bcd2bin(r.h);
  _rtcget_buf[4] = bcd2bin(r.mi);
  _rtcget_buf[5] = bcd2bin(r.s);
  _rtcget_buf[6] = r.wd;
}

// CMD_RTCSETDA/CMD_RTCSETTI: arman _rtc_pending a partir de la copia actual
// (para no pisar la mitad que no cambio) y de los 3 bytes que acaban de
// llegar, y disparan la escritura igual que PRINT #1 sobre "RTC:" (ver
// rtcWrite(), llamada desde loop()). Mismo comentario que rtcBuildGetBuf()
// sobre por que no llevan noInterrupts()/interrupts().
void rtcApplyDate()  //CMD_RTCSETDA: _rtcset_buf = anio-1980,mes,dia
{
  if (_rtc_write_req) return;  //ya hay una escritura en camino, no pisarla
  RtcRegs r = _rtc_buf[_rtc_cur];
  uint8_t y2 = (_rtcset_buf[0] >= 20) ? (_rtcset_buf[0] - 20) : 0;  //-> 20xx
  r.y  = bin2bcd(y2);
  r.mo = bin2bcd(_rtcset_buf[1]);
  r.d  = bin2bcd(_rtcset_buf[2]);
  _rtc_pending = r;
  _rtc_write_req = true;
}

void rtcApplyTime()  //CMD_RTCSETTI: _rtcset_buf = hora,minuto,segundo
{
  if (_rtc_write_req) return;
  RtcRegs r = _rtc_buf[_rtc_cur];
  r.h  = bin2bcd(_rtcset_buf[0]);
  r.mi = bin2bcd(_rtcset_buf[1]);
  r.s  = bin2bcd(_rtcset_buf[2]);
  _rtc_pending = r;
  _rtc_write_req = true;
}

// ---- Armar la linea, desde la ISR -------------------------------------

inline char *put2(char *p, uint8_t bcd)          //siempre dos digitos
{
  *p++ = '0' + (bcd >> 4);
  *p++ = '0' + (bcd & 0x0F);
  return p;
}

inline char *putN(char *p, uint8_t bcd)          //sin ceros a la izquierda
{
  if (bcd >> 4)
    *p++ = '0' + (bcd >> 4);
  *p++ = '0' + (bcd & 0x0F);
  return p;
}

// La linea SIEMPRE termina en CR: es lo que cierra el LINE INPUT. Nunca se
// devuelve fin de datos, asi que RTC: es un chorro infinito de lineas y cada
// LINE INPUT trae la hora de ese momento.
void rtcLine(char *p, uint8_t fmt)
{
  static const char dias[] = "DOMLUNMARMIEJUEVIESAB";
  RtcRegs r = _rtc_buf[_rtc_cur];

  if (fmt == RTC_FMT_NUM)
  {
    *p++ = '2'; *p++ = '0';
    p = put2(p, r.y);   *p++ = ',';
    p = putN(p, r.mo);  *p++ = ',';
    p = putN(p, r.d);   *p++ = ',';
    p = putN(p, r.h);   *p++ = ',';
    p = putN(p, r.mi);  *p++ = ',';
    p = putN(p, r.s);   *p++ = ',';
    *p++ = '0' + (r.wd & 7);
  }
  else if (fmt == RTC_FMT_WDAY)
  {
    uint8_t w = (r.wd >= 1 && r.wd <= 7) ? r.wd - 1 : 0;
    for (uint8_t i = 0; i < 3; i++)
      *p++ = dias[w * 3 + i];
  }
  else
  {
    if (fmt != RTC_FMT_TIME)
    {
      *p++ = '2'; *p++ = '0';
      p = put2(p, r.y);   *p++ = '-';
      p = put2(p, r.mo);  *p++ = '-';
      p = put2(p, r.d);
    }
    if (fmt == RTC_FMT_FULL)
      *p++ = ' ';
    if (fmt != RTC_FMT_DATE)
    {
      p = put2(p, r.h);   *p++ = ':';
      p = put2(p, r.mi);  *p++ = ':';
      p = put2(p, r.s);
    }
  }
  *p++ = 13;
  *p = 0;
}

// ---- El tubo, desde la ISR --------------------------------------------

uint8_t rtcFormat(const char *txt)
{
  switch (txt[0])
  {
    case 0:   return RTC_FMT_FULL;
    case 'D': case 'd': return RTC_FMT_DATE;
    case 'T': case 't': return RTC_FMT_TIME;
    case 'N': case 'n': return RTC_FMT_NUM;
    case 'W': case 'w': return RTC_FMT_WDAY;
  }
  return RTC_FMT_BAD;
}

// El formato tambien se puede pedir pegado al nombre ("RTCN:"), no solo
// despues de los dos puntos ("RTC:N"). El nombre llega siempre en PROCNM; que
// FILNAM ya tenga el texto cuando BASIC llama al OPEN esta SIN VERIFICAR, asi
// que la forma pegada es la que no depende de eso.
volatile uint8_t _dev_find_fmt = RTC_FMT_FULL;

uint8_t devFind()
{
  if (strncmp(_dev_name, "RTC", 3) == 0)
  {
    uint8_t f = rtcFormat(&_dev_name[3]);
    if (f == RTC_FMT_BAD)
      return DEV_NONE;       //"RTCX" no es de nadie
    _dev_find_fmt = f;
    return DEV_RTC;
  }
  return DEV_NONE;
}


void devOpen()
{
  _cmd_st = CMD_DEVOPEN__RESULT;

  if (_dev_open_id != DEV_RTC)          { _dev_open_res = ERR_DEVICE_IO;     return; }
  if (_dev_open_mode & DEV_MODE_RANDOM) { _dev_open_res = ERR_BAD_FILE_MODE; return; }
  if (!_rtc_ok)                         { _dev_open_res = ERR_DEVICE_IO;     return; }

  //sin texto tras los ":", vale el que venia pegado al nombre
  uint8_t fmt = (_dev_name[0] == 0) ? _dev_find_fmt : rtcFormat(_dev_name);
  if (fmt == RTC_FMT_BAD)               { _dev_open_res = ERR_BAD_FILE_NAME; return; }

  if (_dev_open_mode & (DEV_MODE_OUTPUT | DEV_MODE_APPEND))
  {
    _dev_out_id = DEV_RTC;
    _dev_out_idx = 0;
    _dev_out_res = 0;
  }
  else
  {
    _dev_in_id = DEV_RTC;
    _dev_in_fmt = fmt;
    _dev_in_idx = 0xFF;
  }
  _dev_open_res = 0;
}

uint8_t devInByte()
{
  if (_dev_in_id != DEV_RTC)
    return 0;                       //nada abierto: fin de datos
  if (_dev_in_idx == 0xFF)
  {
    rtcLine(_dev_line, _dev_in_fmt);
    _dev_in_idx = 0;
  }
  char c = _dev_line[_dev_in_idx];
  if (c == 0)
  {
    _dev_in_idx = 0xFF;
    return 0;
  }
  _dev_in_idx++;
  if (c == 13)
    _dev_in_idx = 0xFF;             //linea entregada: la proxima es hora nueva
  return c;
}

uint8_t devEof()
{
  //Ademas del fin de datos, avisa que lo que se lea no es confiable: el chip
  //no contesta, o el reloj nunca se puso.
  return (_rtc_ok && _rtc_set) ? 0 : 0xFF;
}

// Parsea la linea que mando PRINT #. Acepta cualquier separador que no sea un
// digito, asi entran "2026-09-20 14:35:07" y "2026,9,20,14,35,7" igual.
uint8_t devOutLine()
{
  uint16_t n[6];
  uint8_t cnt = 0;
  const char *p = _dev_out;

  while (*p && cnt < 6)
  {
    if (*p < '0' || *p > '9') { p++; continue; }
    uint16_t v = 0;
    while (*p >= '0' && *p <= '9')
    {
      v = v * 10 + (*p++ - '0');
      if (v > 9999)
        return ERR_ILLEGAL_FUNCTION;
    }
    n[cnt++] = v;
  }
  if (cnt < 6)
    return ERR_ILLEGAL_FUNCTION;

  uint16_t y = n[0];
  if (y >= 2000) y -= 2000;
  if (y > 99 || n[1] < 1 || n[1] > 12 || n[2] < 1 || n[2] > 31 ||
      n[3] > 23 || n[4] > 59 || n[5] > 59)
    return ERR_ILLEGAL_FUNCTION;

  _rtc_pending.y  = bin2bcd(y);
  _rtc_pending.mo = bin2bcd(n[1]);
  _rtc_pending.d  = bin2bcd(n[2]);
  _rtc_pending.h  = bin2bcd(n[3]);
  _rtc_pending.mi = bin2bcd(n[4]);
  _rtc_pending.s  = bin2bcd(n[5]);
  _rtc_pending.wd = 1;                //lo calcula loop()
  _rtc_write_req = true;
  return DEV_BUSY;                    //loop() deja el resultado en _dev_out_res
}

inline byte readDataBusByte()
{
  //return (PINB & 0x03) | (PIND & 0xfc);
  return PIND;
}

inline byte writeDataBusByte(register byte x)
{
  //PORTB = (PORTB & 0xfc) | (x & 0x03);
  //PORTD = (PORTD & 0x03) | (x & 0xfc);
  PORTD = x;
}

inline void processCommand(register uint8_t command)
{
  if (command == CMD_DEBUG)
  {
    _debug = true;
    return;
  }

  _cmd = command;

  //resincronizo datos a recibir
  //_idx = 0;

  switch (_cmd)
  {
    // 0xAx: reloj del kernel (CHKCLK/$GETTI/$SETDA/$SETTI parcheados)
    case CMD_RTCGET:
      rtcBuildGetBuf();
      _rtcget_idx = 0;
      break;
    case CMD_RTCSETDA:
    case CMD_RTCSETTI:
      _rtcset_idx = 0;
      break;
    case CMD_SDFDEBUG:
      _dbg_idx = 0;
      break;
    case CMD_SDFTEST:
      //Serial.println("CMD_SDFTEST");
      buildTestMsg();
      _test_idx = 0xff; //la primera lectura manda la version del protocolo
      break;
    case CMD_SENDSTR:
      break;
    case CMD_SDFMOUNT:
      //Serial.println("CMD_SDFMOUNT");
      _cmd_st = CMD_SDFMOUNT__DRIVE;
      break;
    case CMD_SDFFILES:
      //Serial.println("CMD_SDFFILES");
      if (sdReady())
        dir.rewindDirectory();
      _list_idx = 0xff; //la primera lectura busca el primer nombre
      break;
    case CMD_SDFUMOUNT:
      //Serial.println("CMD_SDFUMOUNT");
      _cmd_st = CMD_SDFUMOUNT__DRIVE;
      break;
    case CMD_SDFNEW:
      _cmd_st = CMD_SDFNEW__MEDIA;
      break;
    case CMD_WRITE:
      _cmd_st = CMD_PARAM__DRIVE_NUMBER;
      //Serial.println("CMD_WRITE");
      break;
    case CMD_READ:
      _cmd_st = CMD_PARAM__DRIVE_NUMBER;
      //Serial.println("CMD_READ");
      break;
    case CMD_INIHRD:
      //Serial.println("CMD_INIHRD");
      break;
    case CMD_INIENV:
      //Serial.println("CMD_INIENV");
      break;
    case CMD_DRIVES:
      //Serial.println("CMD_DRIVES");
      break;
    case CMD_DSKCHG:
      //Serial.println("CMD_DSKCHG");
      _cmd_st = CMD_DSKCHG__DRIVE_NUMBER;
      break;
    case CMD_CHOICE:
      //Serial.println("CMD_CHOICE");
      break;
    case CMD_DSKFMT:
      //Serial.println("CMD_DSKFMT");
      _cmd_st = CMD_DSKFMT__DRIVE;
      break;
    case CMD_OEMSTAT:
      //Serial.println("CMD_OEMSTAT");
      break;
    case CMD_MTOFF:
      //Serial.println("CMD_MTOFF");
      break;
    case CMD_GETDPB:
      //Serial.println("CMD_GETDPB");
      break;
    //0x9x: tubo de dispositivos de BASIC. DEVIN y DEVEOF no llevan parametros
    //y la ROM los repite antes de cada byte: no pueden reiniciar nada.
    case CMD_DEVNAME:
      _dev_name_idx = 0;
      _dev_name[0] = 0;
      _cmd_st = CMD_DEVNAME__NAME;
      break;
    case CMD_DEVOPEN:
      _cmd_st = CMD_DEVOPEN__ID;
      break;
    case CMD_DEVCLOSE:
      _cmd_st = CMD_DEVCLOSE__ID;
      break;
    case CMD_DEVOUT:
      _cmd_st = CMD_DEVOUT__LINE;
      break;
    //default:
      //Serial.println("UNKNOWN COMMAND "+String(hexByte(_cmd)));
  }
}

inline void processData(register uint8_t data)
{
  if (_debug)
  {
    //Serial.println("DEBUG="+hexByte(data));
    _debug = false;
    return;
  }
  
  switch (_cmd)
  {
    // 0xAx: reloj del kernel. CMD_RTCSETDA=anio-1980,mes,dia (3 bytes);
    // CMD_RTCSETTI=hora,minuto,segundo (3 bytes). Al completarse los 3,
    // arman _rtc_pending y disparan la escritura (ver rtcApplyDate/Time).
    case CMD_RTCSETDA:
      if (_rtcset_idx < 3)
        _rtcset_buf[_rtcset_idx++] = data;
      if (_rtcset_idx == 3)
        rtcApplyDate();
      break;
    case CMD_RTCSETTI:
      if (_rtcset_idx < 3)
        _rtcset_buf[_rtcset_idx++] = data;
      if (_rtcset_idx == 3)
        rtcApplyTime();
      break;

    case CMD_SENDSTR:
      //Serial.print(char(data));
      break;
    case CMD_DEVNAME:
      //llega el nombre como ASCIIZ; uno mas largo que el buffer no es de nadie
      if (_dev_name_idx < DEV_NAME_MAX)
        _dev_name[_dev_name_idx++] = data;
      _dev_name[_dev_name_idx] = 0;
      break;
    case CMD_DEVOPEN:
      switch (_cmd_st)
      {
        case CMD_DEVOPEN__ID:
          _dev_open_id = data;
          _cmd_st = CMD_DEVOPEN__MODE;
          break;
        case CMD_DEVOPEN__MODE:
          _dev_open_mode = data;
          _cmd_st = CMD_DEVOPEN__LENGTH;
          break;
        case CMD_DEVOPEN__LENGTH:
          //el texto que va despues de los ":", ya sin los espacios de FILNAM
          _dev_name_idx = 0;
          _dev_name[0] = 0;
          _dev_open_len = data;
          if (data == 0)
            devOpen();
          else
            _cmd_st = CMD_DEVOPEN__NAME;
          break;
        case CMD_DEVOPEN__NAME:
          if (_dev_name_idx < DEV_NAME_MAX)
            _dev_name[_dev_name_idx++] = data;
          _dev_name[_dev_name_idx] = 0;
          if (--_dev_open_len == 0)
            devOpen();
          break;
      }
      break;
    case CMD_DEVCLOSE:
      //llegan el ID y el modo: sin el modo, cerrar el archivo de entrada
      //cerraria tambien el de salida del mismo dispositivo
      if (_cmd_st == CMD_DEVCLOSE__ID)
      {
        _dev_open_id = data;
        _cmd_st = CMD_DEVCLOSE__MODE;
      }
      else if (_cmd_st == CMD_DEVCLOSE__MODE)
      {
        if (data & (DEV_MODE_OUTPUT | DEV_MODE_APPEND))
        {
          if (_dev_open_id == _dev_out_id) { _dev_out_id = DEV_NONE; _dev_out_idx = 0; }
        }
        else if (_dev_open_id == _dev_in_id) { _dev_in_id = DEV_NONE; _dev_in_idx = 0xFF; }
        _cmd_st = 0;
      }
      break;
    case CMD_DEVOUT:
      if (_dev_out_id == DEV_NONE)
      {
        _dev_out_res = ERR_DEVICE_IO;
        break;
      }
      if (data == 10)
        break;                        //el LF que PRINT # manda detras del CR
      if (data != 13)
      {
        if (_dev_out_idx < DEV_LINE_MAX - 1)
          _dev_out[_dev_out_idx++] = data;
        break;
      }
      _dev_out[_dev_out_idx] = 0;     //CR: la linea esta completa
      _dev_out_idx = 0;
      _dev_out_res = devOutLine();
      break;
    case CMD_SDFMOUNT:
      switch (_cmd_st)
      {
        case CMD_SDFMOUNT__DRIVE:
          _mount_drive = data;
          _cmd_st = CMD_SDFMOUNT__LENGTH;
          break;
        case CMD_SDFMOUNT__LENGTH:
          _mount_len = data;
          _mount_idx = 0;
          if (_mount_len == 0)
            mountDisk();
          else
            _cmd_st = CMD_SDFMOUNT__NAME;
          break;
        case CMD_SDFMOUNT__NAME:
          if (_mount_idx < sizeof(_mount_name) - 1)
            _mount_name[_mount_idx] = data; //si es mas largo, mountDisk lo rechaza
          _mount_idx++;
          if (_mount_idx == _mount_len)
            mountDisk();
          break;
      }
      break;
    case CMD_SDFUMOUNT:
      if ( _cmd_st == CMD_SDFUMOUNT__DRIVE )
        umountDisk(data);
      break;
    case CMD_SDFNEW:
      switch (_cmd_st)
      {
        case CMD_SDFNEW__MEDIA:
          _new_media = data;
          _cmd_st = CMD_SDFNEW__LENGTH;
          break;
        case CMD_SDFNEW__LENGTH:
          _mount_len = data;
          _mount_idx = 0;
          if (_mount_len == 0)
            requestNewDisk();
          else
            _cmd_st = CMD_SDFNEW__NAME;
          break;
        case CMD_SDFNEW__NAME:
          if (_mount_idx < sizeof(_mount_name) - 1)
            _mount_name[_mount_idx] = data; //si es mas largo, requestNewDisk lo rechaza
          _mount_idx++;
          if (_mount_idx == _mount_len)
            requestNewDisk();
          break;
      }
      break;
    case CMD_DSKFMT:
      if ( _cmd_st == CMD_DSKFMT__DRIVE )
      {
        _fmt_media = formatMedia(data);
        _cmd_st = CMD_DSKFMT__MEDIA;
      }
      break;
    case CMD_DSKCHG:
      if ( _cmd_st == CMD_DSKCHG__DRIVE_NUMBER )
      {
        _drive_number = data;
        _cmd_st = CMD_DSKCHG__STATUS;
      }
      break;
    case CMD_WRITE:
    case CMD_READ:
      //switch (_idx++)
      switch (_cmd_st)
      {
        case CMD_PARAM__DRIVE_NUMBER: 
          //Serial.println("DRIVE NUMBER="+hexByte(data));
          _drive_number = data; 
          _cmd_st = CMD_PARAM__N_SECTORS; 
          break;
        case CMD_PARAM__N_SECTORS: 
          _n_sectors = data; 
          _cmd_st = CMD_PARAM__MEDIA; 
          break;
        case CMD_PARAM__MEDIA: 
          _media = data; 
          _cmd_st = CMD_PARAM__ADDR_H;
          break;
        case CMD_PARAM__ADDR_H: 
          _addr_H = data;    
          _cmd_st = CMD_PARAM__ADDR_L;
          break;
        case CMD_PARAM__ADDR_L: 
          _addr_L = data;    
          _cmd_st = CMD_PARAM__SECTOR_H;
          break;
        case CMD_PARAM__SECTOR_H: 
          //Serial.println("SECTOR(H)="+hexByte(data));
          _sec_H = data;    
          _cmd_st = CMD_PARAM__SECTOR_L;
          break;
        case CMD_PARAM__SECTOR_L: 
          //Serial.println("SECTOR(L)="+hexByte(data));
          _sec_L = data;
          _sector = (uint16_t)_sec_H<<8 | _sec_L;
          _address = (uint16_t)_addr_H<<8 | _addr_L;
          _sector_pos = (uint32_t)_sector * 512; 
          _total = (uint32_t)_n_sectors * 512; 
          _idx_sec = 0;
          //Serial.println(" I/O drive="+String(_drive_number)+" ns="+String(_n_sectors)+" media="+String(_media,HEX));
          //Serial.println("     sector="+String(_sector)+ "["+ hexByte(_sec_H)+ hexByte(_sec_L) +"] total="+ String(_total)+ " cmd="+hexByte(_cmd));
          //Serial.println("     address="+String(_address)+ "["+ hexByte(_addr_H)+ hexByte(_addr_L) +"] sector pos="+String(_sector_pos));

          //if (_drive_number != _last_drv)
          //{
          //  _last_drv = _drive_number;
          //  dsk.close();
          //  dsk = SD.open(diskFile(_drive_number), O_RDWR);
          //}
          

          //antes de los sectores el MSX lee un byte de estado, ver dataToSend
          _cmd_st = CMD_ST__IO_STATUS;
          if (!sdReady() || diskFile(_drive_number)[0] == 0)
          {
            _io_status = DSKIO_ERR_NOT_READY; //sin SD (o ocupada), o drive sin imagen montada
            break;
          }

          if ( _cmd == CMD_READ )
            dsk = SD.open(diskFile(_drive_number), O_READ); //abro imagen para lectura
          else
            dsk = SD.open(diskFile(_drive_number), O_RDWR); //abro imagen para lectura+escritura

          if (!dsk)
            _io_status = DSKIO_ERR_NOT_READY;        //la imagen ya no esta en la SD
          else if (_sector_pos + _total > dsk.fileSize())
            _io_status = DSKIO_ERR_RECORD_NOT_FOUND; //no dejo que la imagen crezca
          else if (_cmd == CMD_WRITE && _media == DSK_720K_MEDIA && dsk.fileSize() != DSK_720K_SIZE)
            _io_status = DSKIO_ERR_WRITE_FAULT;      //DPB de 720 KB sobre una imagen de 360:
                                                     //con una ROM que no elige el DPB por la
                                                     //FAT, escribir corromperia la imagen
          else
          {
            _io_status = 0;
            dsk.seek(_sector_pos);
          }
          if (_io_status)
            dsk.close();
          //Serial.println(dsk.name());
          break;
        case CMD_ST__WRITING_SEC:
          //write byte to SD
          dsk.write(data);
          //Serial.print(hexByte(data));
          _total--;
          //if (_total % 32 == 0)
          //  Serial.println();
          
          if (_total == 0)
          {
            _cmd = 0;
            _cmd_st = 0;
            dsk.close();
          }
          
          _idx_sec++;
          if ( _idx_sec == 512 )
          {
            _idx_sec = 0;
            //Serial.println("CHECKSUM=...TODO");
            //_checksum = 0;
            //_cmd_st = CMD_ST__READ_CRC;
            dsk.flush();
          }
          break;
      }
      break;
  }
}

inline uint8_t dataToSend()
{
  switch(_cmd)
  {
    // 0xAx: reloj del kernel. CMD_RTCCHK: 1 byte (0FFh=hay reloj, 0=no).
    // CMD_RTCGET: 7 bytes armados por rtcBuildGetBuf() en processCommand().
    case CMD_RTCCHK:
      _cmd = 0;
      return _rtc_ok ? 0xFF : 0x00;
    case CMD_RTCGET:
      if (_rtcget_idx < 7)
        return _rtcget_buf[_rtcget_idx++];
      _cmd = 0;
      return 0;

    case CMD_SDFTEST:
      //primero la version del protocolo, despues _test_msg como ASCIIZ
      if ( _test_idx == 0xff )
      {
        _test_idx = 0;
        return PROTOCOL_VERSION;
      }
      if ( _test_msg[_test_idx] == 0 )
      {
        _cmd = 0;
        return 0;
      }
      return _test_msg[_test_idx++];
    case CMD_SDFDEBUG:
      //un byte por lectura hasta el 0; al terminar el buffer queda vacio
      if (_dbg_idx < _dbg_len)
        return _dbg_msg[_dbg_idx++];
      _dbg_idx = 0;
      _dbg_len = 0;
      _cmd = 0;
      return 0;
    case CMD_DEVNAME:
      _cmd = 0;
      return devFind();
    case CMD_DEVOPEN:
      _cmd = 0;
      return _dev_open_res;
    case CMD_DEVIN:
      return devInByte();
    case CMD_DEVOUT:
      return _dev_out_res;            //DEV_BUSY hasta que loop() escriba el chip
    case CMD_DEVEOF:
      _cmd = 0;
      return devEof();
    case CMD_SDFFILES:
      //un byte por lectura: los nombres como ASCIIZ y un nombre vacio al final
      if ( _list_idx == 0xff )
      {
        if (!sdReady() || !nextDskName()) //sin SD (o ocupada), la lista vacia
        {
          _cmd = 0;
          return 0;
        }
        _list_idx = 0;
      }
      if ( _list_name[_list_idx] == 0 )
        _list_idx = 0xff; //mando el 0 y la proxima lectura busca otro nombre
      else
        return _list_name[_list_idx++];
      return 0;
    case CMD_SDFMOUNT:
      if ( _cmd_st == CMD_SDFMOUNT__RESULT )
      {
        _cmd = 0;
        return _mount_result;
      }
      break;
    case CMD_SDFUMOUNT:
      if ( _cmd_st == CMD_SDFUMOUNT__RESULT )
      {
        _cmd = 0;
        return _mount_result;
      }
      break;
    case CMD_SDFNEW:
      if ( _cmd_st == CMD_SDFNEW__RESULT )
      {
        if (_new_result == SDFNEW_BUSY)
          return SDFNEW_BUSY; //loop() todavia esta creando la imagen
        _cmd = 0;
        return _new_result;
      }
      break;
    case CMD_DSKFMT:
      if ( _cmd_st == CMD_DSKFMT__MEDIA )
      {
        _cmd = 0;
        return _fmt_media;
      }
      break;
    case CMD_DSKCHG:
      if ( _cmd_st == CMD_DSKCHG__STATUS )
      {
        _cmd = 0;
        if (_drive_number < 2 && _dsk_changed[_drive_number])
        {
          _dsk_changed[_drive_number] = 0;
          return 0xff; //disk changed
        }
        return 0x01;   //disk unchanged
      }
      break;
    case CMD_WRITE:
    case CMD_READ:
      if ( _cmd_st == CMD_ST__IO_STATUS )
      {
        if (_io_status)
          _cmd = 0;                      //error: el MSX no manda ni pide sectores
        else if ( _cmd == CMD_READ )
          _cmd_st = CMD_ST__READING_SEC;
        else
          _cmd_st = CMD_ST__WRITING_SEC;
        return _io_status;
      }
      if ( _cmd_st == CMD_ST__READING_SEC )
      {
        //read byte from SD
        uint8_t b = dsk.read();
        _checksum = _checksum ^ b;
        //Serial.print(hexByte(b));
        _total--;
        //if (_total % 32 == 0)
        //  Serial.println();
        
        if (_total == 0)
        {
          _cmd = 0;
          dsk.close();
        }
        
        _idx_sec++;
        if ( _idx_sec == 512 )
        {
          _idx_sec = 0;
          //Serial.println("CHECKSUM="+hexByte(_checksum));
          _checksum = 0;
          //_cmd_st = CMD_ST__READ_CRC;
        }
        return b;
      }
      //else if ( _cmd_st == CMD_ST__READ_CRC )
      //{
      //  if ( _total == 0)
      //  {
      //    _cmd = 0;
      //    _cmd_st = 0;
      //  }
      //  else
      //  {
      //    _cmd_st = CMD_ST__READING_SEC;
      //  }
      //  return _crc;
      //}
  }
  return 0;
}

// MSX_CS_PIN es PC0, o sea PCINT8, que pertenece al grupo PCINT1 (puerto C).
// De ahi el nombre del vector. No se puede usar INT0/INT1 en su lugar: esos
// son PD2 y PD3, que estan en el bus de datos.
//
// El PCINT dispara en los dos flancos, igual que el CHANGE de la libreria.
//
// Version anterior, con YetAnotherPcInt: ver el historial de git. La libreria
// costaba ~54 ciclos por interrupcion en despachar (reconstruia que pin habia
// cambiado y llamaba por puntero a funcion), mas 1,4 KB de flash y 105 bytes
// de RAM, porque emite las tres ISR de PCINT y guarda estado de los tres
// puertos aunque aca se use un solo pin.
ISR(PCINT1_vect)
{
  // UNA sola lectura del puerto. Antes CS, A0 y RD se leian por separado y
  // con varios microsegundos de diferencia: podian no corresponder al mismo
  // ciclo de bus. Con una lectura unica son coherentes por construccion.
  register uint8_t pc = PINC;

  if (pc & MSX_CS_MASK)
  {
    //La interrupción se produjo porque el decoder deja de seleccionar la interfaz
    configDataBusAsInput();
    _delay_us(MSX_REENABLE_DELAY_US); //dejo que el Z80 cierre el ciclo — ver defs.h
    PORTC |= MSX_EN_MASK;             //habilito
    return;
  }

  //La interrupción se produjo porque el decoder selecciona la interfaz
  if (!(pc & MSX_A0_MASK))
  {
    //se accede al DATA REGISTER
    if (!(pc & MSX_RD_MASK))
    {
      //MSX lee un byte
      configDataBusAsOutput();
      writeDataBusByte(dataToSend());
    }
    else
    {
      //MSX envía un byte
      processData(readDataBusByte());
    }
  }
  else
  {
    //se accede al COMMAND/STATUS REGISTER
    if (!(pc & MSX_RD_MASK))
    {
      //MSX lee registro de estado
      configDataBusAsOutput();
      writeDataBusByte(_stat); //en _stat deberia indicarse lo necesario para el driver en MSX. no se usa por ahora
    }
    else
    {
      //MSX envía un comando
      processCommand(readDataBusByte());
    }
  }
  PORTC &= ~MSX_EN_MASK; //suelto WAIT
}

/*
void findDsk()
{
  while (true) {
    File entry =  dir.openNextFile();
    if (entry)
    {
      char name[14];
      memset(name, 0, sizeof(name));
      entry.getName(name, sizeof(name));
      entry.close();

      String s = String(name);
      s.toUpperCase();
      if (s.endsWith(".DSK"))
      {
        strcpy(curFile, name);
        u8x8.drawString(0,0,"              ");
        u8x8.drawString(0,0,name);
        break;
      }
    }
    else
      dir.rewindDirectory();
  }
  //dir.close();
}
*/

void setup() {
  pinMode(BOTON1, INPUT_PULLUP);
  pinMode(BOTON2, INPUT_PULLUP);
  pinMode(MSX_CS_PIN, INPUT);
  pinMode(MSX_A0_PIN, INPUT);
  pinMode(MSX_RD_PIN, INPUT);
  pinMode(MSX_EN_PIN, OUTPUT);
  pinMode(CS, OUTPUT);

  //u8x8.begin();
  //u8x8.setPowerSave(0);
  //u8x8.setFont(u8x8_font_chroma48medium8_r);

  configDataBusAsInput();

  // La SD no se espera aca: la inicializa loop(). Antes setup() giraba hasta
  // que la SD anduviera y mientras tanto nadie atendia al MSX: CALL SDFTEST
  // leia el bus flotando y no habia forma de saber por que.

  // El DS1307 vive en PC4/PC5, que el perfil DSK deja libres. Va antes de
  // habilitar el PCINT: twiInit() hace lectura-modificacion-escritura sobre
  // PORTC, que es el mismo puerto del que la ISR maneja el /WAIT.
  twiInit();

  dbg("sdf-1 arranco\r\n");  //ejemplo de dbg(): borralo cuando pongas los tuyos

  digitalWrite(MSX_EN_PIN, LOW); //deshabilito el decoder

  // Armo el pin-change de PC0 (PCINT8) a mano. El orden importa: primero
  // elijo el pin, despues descarto un flag que pueda haber quedado pendiente
  // de antes, y recien ahi habilito el grupo. Al reves entraria a la ISR
  // apenas se habilita, por un flanco viejo.
  PCMSK1 = _BV(PCINT8); //solo PC0; los otros 7 pines del puerto C no interrumpen
  PCIFR  = _BV(PCIF1);  //escribir 1 limpia el flag
  PCICR |= _BV(PCIE1);

  digitalWrite(MSX_EN_PIN, HIGH); //habilito el decoder
}
  
void loop()
{
  // Mientras no haya SD, reintento. La ISR ya atiende al MSX: a los comandos
  // que necesitan la SD les contesta que no esta, y SDFTEST muestra por que.
  if (!_sd_ok)
  {
    initSD();
    if (!_sd_ok)
      delay(SD_RETRY_MS);
  }

  // El reloj: releerlo aca y no en la ISR. La misma copia la lee OPEN "RTC:"
  // y CMD_RTCGET (rtcBuildGetBuf(), llamada desde processCommand() en la ISR
  // - solo lectura de la copia, no I2C).
  rtcPoll();
  if (_rtc_write_req)
    rtcWrite();

  // CALL SDFNEW: crear y llenar la imagen tarda segundos, asi que no puede ir
  // en la ISR, que tiene al MSX en /WAIT. Mientras tanto la ROM lee SDFNEW_BUSY.
  if (_new_request)
  {
    _new_request = false;
    createNewDisk();
  }

  // Grabo en la EEPROM lo que monto CALL SDFMOUNT. Va aca y no en la ISR:
  // cada byte tarda ~3,4 ms y durante la ISR el MSX esta en /WAIT, sin
  // refresco de la DRAM. update() solo escribe los bytes que cambiaron.
  if (_save_mounted)
  {
    char copy[2][DSK_NAME_LEN];
    noInterrupts(); //la ISR podria montar otra imagen mientras copio
    memcpy(copy, _mounted, sizeof(copy));
    _save_mounted = false;
    interrupts();

    EEPROM.update(EEPROM_MAGIC_ADDR, EEPROM_MAGIC);
    for (uint8_t d = 0; d < 2; d++)
      for (uint8_t i = 0; i < DSK_NAME_LEN; i++)
        EEPROM.update(EEPROM_MOUNTED_ADDR + d * DSK_NAME_LEN + i, copy[d][i]);
  }

  /*
  if (SD.card()->errorCode() == SD_CARD_ERROR_NOT_PRESENT) {
    u8x8.drawString(0,0,"out");
  }
  */

  /*
  if (digitalRead(BOTON1)==LOW)
  {
    delay(100);
    while (digitalRead(BOTON1)==LOW);
    //u8x8.drawString(0,0,String(_disk_number++).c_str());
    findDsk();
    delay(100);
  }

  if (digitalRead(BOTON2)==LOW)
  {
    delay(100);
    while (digitalRead(BOTON2)==LOW);
    if (_disk_number > 0)
      u8x8.drawString(0,0,String(_disk_number--).c_str());
    delay(100);
  }
  */
}
