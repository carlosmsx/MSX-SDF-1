# MSX-SDF-1

Lector de memorias SD para MSX, en formato cartucho. Compatible de MSX1 en
adelante.

La idea es una interfaz sencilla, con componentes que se consigan en cualquier
tienda de electrónica, económicamente accesibles y **THT** (*thru hole*), de
manera de facilitar el ensamblaje al hobbista. Igual siempre se puede hacer un
PCB en SMT si alguien lo prefiere.

El cartucho presenta al MSX una unidad de disco estándar: arranca MSX-DOS y
Disk BASIC desde imágenes `.DSK` guardadas en la tarjeta. Para el MSX es una
disketera común.

> **Estado: en desarrollo.** La rev1 está fabricada y **requiere correcciones a
> mano** antes de funcionar — ver [hardware/rev1/BODGES.md](hardware/rev1/BODGES.md).
> No es todavía un proyecto "armalo y andá".

## Cómo está hecho

El cartucho tiene dos mitades que se hablan por un puerto de I/O:

**La DiskROM (Z80)** — una EEPROM W27C512 con la DiskROM de 16 K mapeada en la
página 1 (`0x4000`). El driver de disco es propio y está en
[`diskrom/DSKDRV.MAC`](diskrom/DSKDRV.MAC); se linkea contra el kernel MSX-DOS
de ASCII, como cualquier interfaz de disco de MSX. La imagen ocupa 64 K planos
a propósito: sobra lugar y la deja reprogramable con un adaptador común.

**El firmware (ATmega328P)** — [`sdf-1-atmega328p/`](sdf-1-atmega328p/). Un
ATmega328P a 20 MHz con cristal externo hace de controlador: recibe comandos del
Z80, lee y escribe sectores de la imagen `.DSK` en la SD por SPI, y sostiene el
`/WAIT` del MSX mientras tanto. Las interrupciones son PCINT nativo, sin
librería — los tiempos del protocolo dependen de eso.

**La lógica de decodificación es 74HCxx discreta**, sin GAL ni CPLD. Es más
chips, pero cualquiera la puede reproducir sin programador de PLDs. Los `.PLD`
que hayas visto en versiones viejas son históricos.

Si tocás los códigos de comando, tocás **las dos** mitades: el protocolo está
definido en [`sdf-1-atmega328p/defs.h`](sdf-1-atmega328p/defs.h) y usado desde
los dos lados.

## El repo

| Directorio | Qué hay |
|---|---|
| [`diskrom/`](diskrom/) | El driver de disco, en ensamblador Z80 |
| [`sdf-1-atmega328p/`](sdf-1-atmega328p/) | Firmware del ATmega328P |
| [`firmware/`](firmware/) | El `.hex` compilado, para grabar sin toolchain |
| [`hardware/`](hardware/) | Una carpeta por revisión de PCB: fuente EasyEDA, Gerbers, esquemático, BOM |
| [`tools/`](tools/) | `mkrom.py`, que arma la imagen de EEPROM |
| `build/` | **No está en el repo** — ver abajo |

## Compilar

    make            las dos cosas
    make rom        solo la DiskROM     -> out/sdf1.rom
    make firmware   solo el firmware    -> out/firmware/
    make check      verifica que estén las herramientas

### Para la DiskROM

Hacen falta **N80 y LK80** de [Nestor80](https://github.com/Konamiman/Nestor80/releases),
descomprimidos en `tools/bin/`. Son el reemplazo moderno de M80 y L80: leen y
escriben el mismo formato `.REL`, corren nativo en Windows, Linux y macOS, y son
MIT. Ya no hace falta un emulador de CP/M.

### Y hace falta el MSX-DOS kit, que no está acá

`build/` tiene que contener tu propia copia del **MSX-DOS kit de ASCII**
(1984-85): los ocho `.REL` del kernel contra los que se linkea el driver.

**No están redistribuidos en este repo.** Son código de terceros y no tengo los
derechos para publicarlos, así que el repo trae la receta y no el material — el
mismo criterio que los emuladores que se distribuyen sin las BIOS. Por la misma
razón tampoco se publica `out/sdf1.rom`: es ese kernel ya linkeado.

El encabezado del [`Makefile`](Makefile) lista qué archivos van y en qué orden;
`make check` te avisa si falta alguno. Con el kit en su lugar, `make rom`
reconstruye la imagen completa.

### Para el firmware

No hace falta instalar `arduino-cli`: el Arduino IDE 2 trae el suyo adentro y el
`Makefile` lo usa, con los mismos cores y librerías que ya tenés. Necesitás
[MiniCore](https://github.com/MCUdude/MiniCore) y el fork de
[SdFat de Adafruit](https://github.com/adafruit/SdFat) — **no** el de greiman,
el sketch usa `SPI_FULL_SPEED` y el typedef `File`.

## Grabar

    make fuses      una vez por chip
    make flash      el firmware, por ICSP

Requiere el cable de RESET del paso 5 de
[BODGES.md](hardware/rev1/BODGES.md), y desenchufar el módulo de SD antes de
grabar. Los detalles están en [firmware/README.md](firmware/README.md).

## Licencia

MIT — ver [LICENSE](LICENSE).

Aplica al código, el diseño de hardware y la documentación **de este repo**. El
MSX-DOS kit de ASCII no está incluido ni cubierto por esta licencia; si lo
conseguís por tu cuenta, se rige por sus propios términos.
