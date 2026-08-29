/* * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * *
 * Proyecto: MSX-SDF-1                                                       *
 * Autor: Carlos Escobar                                                     *
 * Abr-2023                                                                  *
 * Board ATmega328 20mhz  (el cristal es de 20 MHz; el BOM v1.1 esta mal,    *
 *                         ver hardware/rev1/BODGES.md paso 7)               *
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
#include <util/delay.h>

#define SCREEN_WIDTH 128 // Ancho del display OLED
#define SCREEN_HEIGHT 64 // Alto del display OLED
#define i2c_Address 0x3c //initialize with the I2C addr 0x3C Typically eBay OLED's
#define OLED_RESET -1   //   QT-PY / XIAO

//U8X8_SH1106_128X64_NONAME_HW_I2C u8x8(/* reset=*/ U8X8_PIN_NONE);

SdFat SD;
char curFile[14];
File dsk;
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

String diskFile(uint8_t drive)
{
  //Por ahora solo se acceden a dos archivos DSK para prueba de concepto
  if (drive == 0)
    return "4K720.DSK";
    //return curFile;
  else
    return "TPASCAL.DSK";
}

void listFiles()
{
  File dir = SD.open("/");
  while (true) {
    File entry =  dir.openNextFile();
    if (! entry) {
      // no more files
      break;
    }

    entry.close();
  }
  dir.close();
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
    case CMD_SENDSTR:
      break;
    case CMD_FSAVE:
      //Serial.println("CMD_FSAVE");
      listFiles();
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
    case CMD_FSAVE:
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
          

          if ( _cmd == CMD_READ )
          {
            dsk = SD.open(diskFile(_drive_number), O_READ); //abro imagen para lectura
            _cmd_st = CMD_ST__READING_SEC;
          }
          else
          {
            dsk = SD.open(diskFile(_drive_number), O_RDWR); //abro imagen para lectura+escritura
            _cmd_st = CMD_ST__WRITING_SEC;
          }
          dsk.seek(_sector_pos);
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
    case CMD_READ:
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

File dir;

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
