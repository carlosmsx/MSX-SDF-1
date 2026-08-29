# Firmware compilado

`sdf-1-atmega328p.hex` es el firmware del ATmega328P listo para grabar, para
quien quiera armar la placa sin instalar la toolchain de Arduino.

Es codigo 100% propio: sale de `../sdf-1-atmega328p/` y de nada mas.

## Grabarlo

Por ICSP, con el cable de RESET del paso 5 de `../hardware/rev1/BODGES.md`.
Desenchufa el modulo de SD antes de grabar.

Los fuses van UNA VEZ por chip, antes del primer grabado:

    make fuses
    make flash

Si preferis avrdude a mano, para este FQBN los fuses son
`lfuse=0xF7 hfuse=0xD7 efuse=0xFD` (cristal externo de 20 MHz, sin bootloader).

## Reconstruirlo

    make firmware

Sale en `out/firmware/`. Si lo regeneras y queres publicarlo, copialo aca.

## Y la DiskROM?

`sdf1.rom` no se publica: contiene el kernel MSX-DOS de ASCII linkeado con
nuestro driver. Ver el encabezado del `Makefile` — con tu propia copia del
MSX-DOS kit en `build/`, `make rom` la reconstruye.
