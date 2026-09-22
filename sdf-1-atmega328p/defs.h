/*********************************************
 * Proyecto: MSX-SDF-1                       *
 * Autor: Carlos Escobar                     *
 * Abr-2023                                  *
 *********************************************/

#ifndef _DEFS_H
#define _DEFS_H

//Wiring
// Old version: DATA BUS: PB0, PB1, PD2, PD3, PD4, PD5, PD6, PD7 
// Current version DATA BUS: PD0, PD1, PD2, PD3, PD4, PD5, PD6, PD7 
// CONTROL: PC0, PC1, PC2, PC3
// De esta manera se dejan libres los puertos I2C y SPI

#define MSX_CS_PIN A0
#define MSX_A0_PIN A1
#define MSX_RD_PIN A2
#define MSX_EN_PIN A3
#define BOTON1 8
#define BOTON2 9
#define CS 10

// Los mismos cuatro pines de control, como mascaras de bit del puerto C.
// A0/A1/A2/A3 de Arduino son PC0/PC1/PC2/PC3: son los MISMOS pines, nombrados
// de las dos maneras. Las mascaras son las que usa la ISR; los A0..A3 quedan
// para los pinMode() del setup(), que no son criticos.
#define MSX_CS_MASK  _BV(PC0)   // U5.Y0: el decoder nos selecciona (PCINT8)
#define MSX_A0_MASK  _BV(PC1)   // A0 del MSX: 0=registro de DATOS, 1=COMANDO/ESTADO
#define MSX_RD_MASK  _BV(PC2)   // /RD del MSX: 0=el MSX lee, 1=el MSX escribe
#define MSX_EN_MASK  _BV(PC3)   // hacia G1 de U5: nuestra llave del /WAIT

// Retardo antes de rehabilitar U5 al terminar un acceso.
//
// Tras soltar el /WAIT el Z80 todavia necesita 1-2 estados T (~300-600 ns a
// 3,58 MHz) para cerrar el ciclo de I/O, con /IORQ y la direccion aun validas.
// Si rehabilitamos antes de eso, la decodificacion vuelve a dar positivo sobre
// el mismo ciclo y se dispara un acceso fantasma.
//
// Con la libreria de interrupciones esto tardaba ~6 us por si solo y el
// problema quedaba tapado por accidente. La ISR nativa baja eso a menos de
// 1 us, asi que el margen hay que ponerlo explicito: es este numero.
//
// Arranca en 6 para reproducir el comportamiento actual: el primer build
// nativo gana velocidad en la rama de datos sin mover nada en la rama que
// puede romper. Bajalo de a un paso CON EL ANALIZADOR ENCHUFADO, midiendo
// antes y despues. No lo toques a ojo.
#define MSX_REENABLE_DELAY_US 6


// Algunas definiciones para comunicar con el driver en MSX
//
// Los codigos se agrupan por funcion, un nibble alto por grupo. Un grupo
// nuevo toma un nibble libre; no se mezclan comandos de grupos distintos.
//   0x9x  dispositivos de BASIC (OPEN "RTC:"...)
//   0xAx  reloj del kernel (CHKCLK/$GETTI/$SETDA/$SETTI parcheados, camino A
//         de reloj-cuatro-caminos.md: nada de puertos crudos, protocolo
//         normal de comando+datos como el resto del sistema)
//   0xDx  diagnostico
//   0xEx  manejo de la SD y de las imagenes DSK (CALL SDF...)
//   0xFx  driver de disco de MSX-DOS (las rutinas de DSKDRV.MAC)
// Tienen que coincidir con los EQU de diskrom/PROTOCOL.INC, que incluyen las
// dos paginas de la ROM.

// Version del firmware, la muestra CALL SDFTEST. La fecha de compilacion
// distingue dos grabaciones de la misma version.
#define FW_VERSION        "0.2 " __DATE__

// CALL SDFDEBUG imprime lo que el firmware haya dejado con dbg() y lo vacia.
// Son 64 bytes de RAM: si hacen falta para otra cosa, bajarlo.
#define DEBUG_MSG_MAX     64

// CALL SDFTEST muestra la version y, en otra linea, el estado de la SD. La
// ROM lee a lo sumo 32 caracteres: si no entran, el sketch no compila.
#define TEST_MSG_MAX      32

// Version del protocolo con la ROM, tambien la compara CALL SDFTEST. Sube
// cuando un cambio obliga a grabar ROM y firmware juntos; tiene que coincidir
// con PROTOCOL en diskrom/PROTOCOL.INC.
//   1 = byte de estado en READ/WRITE, SDFFILES/SDFMOUNT/SDFUMOUNT, SDFTEST
//   2 = imagenes de 360 KB: GETDPB elige el DPB por el primer byte de la FAT
//   3 = CALL SDFNEW, y DSKFMT formatea segun el largo de la imagen
//   4 = tubo de dispositivos de BASIC (0x9x) y RTC: sobre el DS1307
//   5 = CALL SDFDEBUG y CALL RTC; fuera CALL FFILES
//   6 = CHKCLK/$GETTI/$SETDA/$SETTI parcheados sobre el DS1307 (0xAx)
#define PROTOCOL_VERSION  6

// 0x9x: tubo de dispositivos de BASIC. La ROM no sabe que dispositivos hay:
// pregunta por el nombre y despues pasa bytes. Uno nuevo se agrega ACA, no en
// la ROM.
#define CMD_DEVNAME   0x90  //nombre ASCIIZ -> ID 0-3, o DEV_NONE
#define CMD_DEVOPEN   0x91  //ID, modo, largo y el texto que sigue a los ":"
#define CMD_DEVCLOSE  0x92
#define CMD_DEVIN     0x93  //un byte por lectura, 0 = fin de datos
#define CMD_DEVOUT    0x94  //bytes; el CR ejecuta la linea
#define CMD_DEVEOF    0x95

// 0xAx: reloj del kernel. Las 4 entradas parchadas (CHKCLK/$GETTI/$SETDA/
// $SETTI) hablan con el firmware por el protocolo normal (WriteCommand/
// WriteByte/ReadByte de DSKDRV.MAC), nada de puertos crudos como el camino B
// que se probo primero y fallo por timing (ver reloj-cuatro-caminos.md).
#define CMD_RTCCHK    0xA0  //CHKCLK: sin datos, contesta 1 byte (0FFh=hay reloj, 0=no)
#define CMD_RTCGET    0xA1  //$GETTI: contesta 7 bytes binarios: anio-1980,mes,dia,hora,min,seg,diasem
#define CMD_RTCSETDA  0xA2  //$SETDA: recibe 3 bytes: anio-1980,mes,dia
#define CMD_RTCSETTI  0xA3  //$SETTI: recibe 3 bytes: hora,min,seg

// 0xDx: diagnostico
#define CMD_DEBUG     0xD0
#define CMD_SDFTEST   0xD1
#define CMD_SDFDEBUG  0xD2  //texto libre del firmware: lo imprime CALL SDFDEBUG

// 0xEx: SD e imagenes DSK
#define CMD_SENDSTR   0xE0  //de antes de agrupar; hoy no lo usa nadie
#define CMD_SDFMOUNT  0xE1
#define CMD_SDFFILES  0xE2
#define CMD_SDFUMOUNT 0xE3
#define CMD_SDFNEW    0xE4

// 0xFx: driver de disco de MSX-DOS
#define CMD_WRITE     0xF0
#define CMD_READ      0xF1
#define CMD_INIHRD    0xF2
#define CMD_INIENV    0xF3
#define CMD_DRIVES    0xF4
#define CMD_DSKCHG    0xF5
#define CMD_CHOICE    0xF6
#define CMD_DSKFMT    0xF7
#define CMD_OEMSTAT   0xF8
#define CMD_MTOFF     0xF9
#define CMD_GETDPB    0xFA

#define CMD_PARAM__DRIVE_NUMBER   0
#define CMD_PARAM__N_SECTORS      1
#define CMD_PARAM__MEDIA          2
#define CMD_PARAM__SECTOR_H       3
#define CMD_PARAM__SECTOR_L       4
#define CMD_PARAM__ADDR_H         5
#define CMD_PARAM__ADDR_L         6
#define CMD_ST__READING_SEC       10
#define CMD_ST__READ_CRC          11
#define CMD_ST__IO_STATUS         12
#define CMD_ST__WRITING_SEC       20
#define CMD_SDFMOUNT__DRIVE       30
#define CMD_SDFMOUNT__LENGTH      31
#define CMD_SDFMOUNT__NAME        32
#define CMD_SDFMOUNT__RESULT      33
#define CMD_SDFUMOUNT__DRIVE      34
#define CMD_SDFUMOUNT__RESULT     35
#define CMD_SDFNEW__MEDIA         36
#define CMD_SDFNEW__LENGTH        37
#define CMD_SDFNEW__NAME          38
#define CMD_SDFNEW__RESULT        39
#define CMD_DSKCHG__DRIVE_NUMBER  40
#define CMD_DSKCHG__STATUS        41
#define CMD_DSKFMT__DRIVE         42
#define CMD_DSKFMT__MEDIA         43
#define CMD_DEVNAME__NAME         50
#define CMD_DEVOPEN__ID           51
#define CMD_DEVOPEN__MODE         52
#define CMD_DEVOPEN__LENGTH       53
#define CMD_DEVOPEN__NAME         54
#define CMD_DEVOPEN__RESULT       55
#define CMD_DEVCLOSE__ID          56
#define CMD_DEVCLOSE__MODE        58
#define CMD_DEVOUT__LINE          57

// Respuesta de CMD_SDFMOUNT, CMD_SDFUMOUNT y CMD_SDFNEW: 0 si anduvo, si no el
// numero de error de BASIC que muestra el MSX. Asi la ROM no necesita una tabla
// de mensajes.
#define ERR_FILE_NOT_FOUND        53
#define ERR_BAD_FILE_NAME         56
#define ERR_BAD_FILE_MODE         61  //la imagen no es de 360 ni de 720 KB
#define ERR_BAD_DRIVE_NAME        62
#define ERR_FILE_ALREADY_EXISTS   65
#define ERR_DISK_FULL             66
#define ERR_DISK_IO               69
#define ERR_DISK_OFFLINE          70  //no hay SD, todavia no inicializo, o esta ocupada
#define ERR_ILLEGAL_FUNCTION       5  //la linea que mando PRINT # no se entiende
#define ERR_DEVICE_IO             19  //el chip no contesta

// Formatos que conoce GETDPB en la ROM, los dos de 3,5" y 80 pistas. La ROM
// elige el DPB por el primer byte de la FAT; el firmware solo mira el largo.
#define DSK_720K_SIZE             737280UL  //doble faz, media F9
#define DSK_360K_SIZE             368640UL  //una cara, media F8
#define DSK_720K_MEDIA            0xF9
#define DSK_360K_MEDIA            0xF8
#define DSK_NAME_LEN              13  //nombre 8.3 mas el 0 final

// CALL SDFNEW. Mientras loop() crea la imagen, el MSX lee SDFNEW_BUSY en vez
// del resultado: no choca con los errores de BASIC ni con el bus flotando (FF).
// La imagen recien creada se escribe como un tercer drive, NEW_DRIVE, que el
// DOS no conoce. Los ceros se escriben de a NEW_ZERO_CHUNK bytes.
#define SDFNEW_BUSY               0xFE
#define NEW_DRIVE                 2
#define NEW_ZERO_CHUNK            64

// ---- Dispositivos de BASIC ----
//
// Mientras el firmware ejecuta una linea de CMD_DEVOUT contesta DEV_BUSY en vez
// del resultado, igual que SDFNEW_BUSY: escribir el DS1307 es I2C y el I2C no
// se toca desde la ISR, con el MSX en /WAIT.
#define DEV_BUSY          0xFE
#define DEV_NONE          0xFF  //tambien es lo que lee la ROM con el bus flotando

#define DEV_RTC           0     //OPEN "RTC:"

#define DEV_LINE_MAX      32    //la linea mas larga que se arma o se acepta
#define DEV_NAME_MAX      8     //nombre del dispositivo, y texto tras los ":"

// Modos de OPEN de BASIC, tal como llegan en el parametro del CMD_DEVOPEN.
#define DEV_MODE_INPUT    1
#define DEV_MODE_OUTPUT   2
#define DEV_MODE_RANDOM   4
#define DEV_MODE_APPEND   8

// ---- RTC DS1307 ----
//
// Direccion fija del chip. La comparte con el MPU-6050, que se corre a 0x69
// con AD0 a VCC.
#define DS1307_ADDR       0x68
#define RTC_POLL_MS       250   //cada cuanto loop() relee el chip

// Formatos de RTC:, elegidos con el texto que va despues de los dos puntos.
#define RTC_FMT_FULL      0     //"RTC:"   2026-09-20 14:35:07
#define RTC_FMT_DATE      1     //"RTC:D"  2026-09-20
#define RTC_FMT_TIME      2     //"RTC:T"  14:35:07
#define RTC_FMT_NUM       3     //"RTC:N"  2026,9,20,14,35,7,1  para INPUT #
#define RTC_FMT_WDAY      4     //"RTC:W"  DOM
#define RTC_FMT_BAD       0xFF

// SD: pausa entre intentos de inicializarla, y valor de _sd_error antes del
// primer intento. Los codigos de error de SdFat son chicos: 0xFF no choca.
#define SD_RETRY_MS               250
#define SD_NOT_TRIED              0xFF

// Byte de estado de CMD_READ y CMD_WRITE, antes de los sectores: 0 si se
// puede, si no el codigo de error que DSKIO le devuelve al DOS.
#define DSKIO_ERR_NOT_READY         2 //drive sin imagen, o la imagen ya no esta
#define DSKIO_ERR_RECORD_NOT_FOUND  8 //sector fuera de la imagen
#define DSKIO_ERR_WRITE_FAULT      10 //escritura con DPB de 720 KB en una imagen de 360

// EEPROM: las imagenes montadas en A y B, para que sobrevivan al apagado.
#define EEPROM_MAGIC_ADDR         0
#define EEPROM_MAGIC              0x5D  //si cambia el formato, cambiar esto
#define EEPROM_MOUNTED_ADDR       1     //DSK_NAME_LEN bytes por drive, A y B

#endif
