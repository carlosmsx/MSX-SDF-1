# Firmware compilado

`sdf-1-atmega328p.hex` es el firmware del ATmega328P listo para grabar, para
quien quiera armar la placa sin instalar la toolchain de Arduino.

Es codigo 100% propio: sale de `../sdf-1-atmega328p/` y de nada mas.

Version **0.3** (protocolo 6): la SD sale de la interrupcion que atiende al
MSX, las imagenes montadas quedan abiertas y, si ocupan sectores contiguos en
la SD, se leen directo de la tarjeta sin pasar por SdFat. En un DPC-200 la
lectura pasa de 13,6 a 28,6 KB/s y un acceso al azar de 62 a 31 ms (medido con
`../pruebas/velocidad/SDFBENCH.BAS`). La escritura sigue por SdFat. `CALL SDFTEST`
muestra la version con el commit (`0.3-0-...`) y `CALL SDFDEBUG`, estadisticas
del disco.

Usa la misma ROM que la 0.2: el protocolo no cambio. Para que MSX-DOS tome la
fecha del reloj, la ROM se arma con `make rom-rtc` en vez de `make rom`.

## Grabarlo

Por ICSP, con el cable de RESET del paso 1 de `../hardware/rev1/CORRECCIONES.md`.
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
