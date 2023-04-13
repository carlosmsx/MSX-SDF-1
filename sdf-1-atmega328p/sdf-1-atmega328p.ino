/*********************************************
 * Proyecto: MSX-SDF-1                       *
 * Autor: Carlos Escobar                     *
 * Abr-2023                                  *
 *********************************************/

#include <SdFat.h>
#include "defs.h"
#include <YetAnotherPcInt.h>

SdFat SD;
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

String diskFile(uint8_t drive)
{
  //Por ahora solo se acceden a dos archivos DSK para prueba de concepto
  if (drive == 0)
    return "4K720.DSK";
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
// y los pines 2-7 del puerto D para los bits 2-7 con el objetivo de lierar los pines
// 0 y 1 del puerto D que corresponden a RX y TX, permitiendo hacer debug.
// Ya no es necesario, por eso ahora se usa el puerto D completo para el bus de datos
// facilitando la lectura y escritura de los datos en menos instrucciones.
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

          /*
          if (_drive_number != _last_drv)
          {
            _last_drv = _drive_number;
            dsk.close();
            dsk = SD.open(diskFile(_drive_number), O_RDWR);
          }
          */

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
      /*
      else if ( _cmd_st == CMD_ST__READ_CRC )
      {
        if ( _total == 0)
        {
          _cmd = 0;
          _cmd_st = 0;
        }
        else
        {
          _cmd_st = CMD_ST__READING_SEC;
        }
        return _crc;
      }
      */
  }
  return 0;
}

//volatile int zz=0;
void pinChanged_sd(const char* message, bool pinstate)
{
  //zz++;
  //digitalWrite(LED, zz & 0x2 ? HIGH: LOW);
  
  if (pinstate)
  { 
    //La interrupción se produjo porque el decoder deja de seleccionar la interfaz
    configDataBusAsInput();
    digitalWrite(MSX_EN_PIN, HIGH); //habilito
  }
  else
  {
    //La interrupción se produjo porque el decoder selecciona la interfaz
    //delayMicroseconds(10);
    if (digitalRead(MSX_A0_PIN) == LOW)
    {
      //se accede al DATA REGISTER
      if (digitalRead(MSX_RD_PIN) == LOW)
      {
        //MSX lee un byte
        //delayMicroseconds(10);
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
      if (digitalRead(MSX_RD_PIN) == LOW)
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
    digitalWrite(MSX_EN_PIN, LOW); //suelto WAIT
  }
}


void setup() {
  pinMode(LED, OUTPUT);
  pinMode(MSX_CS_PIN, INPUT);
  pinMode(MSX_A0_PIN, INPUT);
  pinMode(MSX_RD_PIN, INPUT);
  pinMode(MSX_EN_PIN, OUTPUT);
  pinMode(CS, OUTPUT);

  configDataBusAsInput();

  //Serial.print("Init\nSD");
  while (!SD.begin(CS, SPI_FULL_SPEED))
  {
    digitalWrite(8, HIGH);
    delay(500);
    digitalWrite(8, LOW);
    delay(200);
  }

  digitalWrite(MSX_EN_PIN, LOW); //deshabilito el decoder
  PcInt::attachInterrupt(MSX_CS_PIN, pinChanged_sd, "", CHANGE);
  digitalWrite(MSX_EN_PIN, HIGH); //habilito el decoder
}
  
void loop()
{
  //TODO: aqui deeria usarse card.off() para determinar si la tarjeta fue extraida y volver al inicio, posilemente indicando un disk change
  //por ahora solo hago parpadear un led para indicar que esta funcionando correctamente
    digitalWrite(LED, HIGH);
    delay(100);
    digitalWrite(LED, LOW);
    delay(100);
}
