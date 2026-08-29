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
#define CMD_DEBUG     0xD0
#define CMD_SENDSTR   0xE0
#define CMD_FSAVE     0xE1
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
#define CMD_ST__WRITING_SEC       20

#endif
