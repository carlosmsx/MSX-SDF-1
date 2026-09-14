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

const char* diskFile(uint8_t drive)
{
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
// Desmontar un drive que ya estaba vacio no es error.
void umountDisk(uint8_t drive)
{
  _cmd_st = CMD_SDFUMOUNT__RESULT;

  if (drive > 1)
  {
    _mount_result = ERR_BAD_DRIVE_NAME;
    return;
  }
  _mounted[drive][0] = 0;
  _dsk_changed[drive] = 1; //el DOS descarta lo que tenga en buffers de ese drive
  _save_mounted = true;
  _mount_result = 0;
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
    case CMD_SDFTEST:
      //Serial.println("CMD_SDFTEST");
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
      dir.rewindDirectory();
      _list_idx = 0xff; //la primera lectura busca el primer nombre
      break;
    case CMD_SDFUMOUNT:
      //Serial.println("CMD_SDFUMOUNT");
      _cmd_st = CMD_SDFUMOUNT__DRIVE;
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
    case CMD_SENDSTR:
      //Serial.print(char(data));
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
          if (diskFile(_drive_number)[0] == 0)
          {
            _io_status = DSKIO_ERR_NOT_READY; //drive sin imagen montada
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
    case CMD_SDFTEST:
      //primero la version del protocolo, despues FW_VERSION como ASCIIZ
      if ( _test_idx == 0xff )
      {
        _test_idx = 0;
        return PROTOCOL_VERSION;
      }
      if ( FW_VERSION[_test_idx] == 0 )
      {
        _cmd = 0;
        return 0;
      }
      return FW_VERSION[_test_idx++];
    case CMD_SDFFILES:
      //un byte por lectura: los nombres como ASCIIZ y un nombre vacio al final
      if ( _list_idx == 0xff )
      {
        if (!nextDskName())
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

  while (!SD.begin(CS, SPI_FULL_SPEED))
  {
    //u8x8.drawString(0,0,"Inserte SD");
  }
  //u8x8.drawString(0,0,"SD OK");
  
  dir = SD.open("/");
  loadMounted();
  //findDsk();
  
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
