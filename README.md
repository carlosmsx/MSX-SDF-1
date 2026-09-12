# MSX-SDF-1

Un **kit de desarrollo para MSX**: una placa de I/O universal y experimental en
formato cartucho, con área de islas perforadas para montar tu propio hardware.
Compatible de MSX1 en adelante.

La idea es una interfaz sencilla, con componentes que se consigan en cualquier
tienda de electrónica, económicamente accesibles y **THT** (*thru hole*), de
manera de facilitar el ensamblaje al hobbista. Igual siempre se puede hacer un
PCB en SMT si alguien lo prefiere.

Su **primer proyecto de ejemplo** —y lo único terminado hasta hoy— es un **lector
de memorias SD**: el cartucho le presenta al MSX una unidad de disco estándar,
arranca MSX-DOS y Disk BASIC desde imágenes `.DSK` guardadas en la tarjeta, y
para el MSX es una disketera común.

> **Estado: en desarrollo.** La rev1 está fabricada y **requiere correcciones a
> mano** antes de funcionar — ver [hardware/rev1/CORRECCIONES.md](hardware/rev1/CORRECCIONES.md).
> No es todavía un proyecto "armalo y andá".

## Las decisiones de diseño

El truco de fondo —bajar el `/WAIT` del Z80 con la misma señal que decodifica el
puerto, y contestar con un microcontrolador mientras la máquina espera congelada
a mitad del ciclo— lo tomé del [Virtual MSX Disk
Drive](https://codinglab.blogspot.com/2013/01/virtual-msx-disk-drive.html) que
Raul publicó en 2013. De ahí en más, todo lo demás está resuelto acá, y es donde
está el trabajo:

**Anda solo, sin una PC atrás.** En el original el Arduino no guarda nada: está
colgado del USB de una PC y un script de Python le va pasando los sectores de la
imagen de disco. Acá el ATmega lee el `.DSK` él mismo, de una microSD por SPI. El
cartucho se enchufa y funciona con la máquina sola.

**Decodificación completa, no una ventana.** El original engancha cualquier
puerto por debajo de 0x20. Acá dos 74LS138 decodifican `A7..A1` y dejan
seleccionados **sólo `0x00` y `0x01`**. Las salidas `Y1..Y7` del segundo '138
quedan libres (cubren `0x02`–`0x0F`) y no van a ningún lado: mover el par de
puertos es rutear otra salida. En la rev2 eso pasa a ser un jumper, para poder
esquivar un conflicto sin tocar el cobre.

**Dos puertos, o mejor dicho dos registros.** `A0` no se decodifica como
selección de chip — es el selector de registro. Un puerto es para transferir
datos y el otro para gobernar el diálogo, y eso lo resuelve el hardware: el
protocolo nunca tiene que preguntarse si el byte que acaba de llegar es un dato
o una orden.

| Puerto | `A0` | Escritura (`OUT`) | Lectura (`IN`) |
|---|---|---|---|
| `0x00` | 0 | **Datos**, del MSX al micro | **Datos**, del micro al MSX |
| `0x01` | 1 | **Comando**: ejecutá esto | **Estado**: cómo venís |

El de datos es **bidireccional**: el mismo registro sirve para los 512 bytes que
el MSX escribe en un sector y para los 512 que lee. La dirección la resuelve
`/RD`, que llega al micro y también maneja el `DIR` del '245, así que ni el
driver ni el firmware tienen que negociar de qué lado va el bus.

El otro registro es **asimétrico a propósito**: escribirlo es *disparar* algo
(leer un sector, montar una imagen, lo que sume el firmware) y leerlo es
*preguntar*, sin efecto colateral. Ese lado de lectura es el que convierte el
handshake en un protocolo de verdad: permite contestar "ocupado", "hubo un
error" o "tengo tantos bytes para vos" **sin robarle bytes al canal de datos**,
que es exactamente lo que hace falta para poder soltar el `/WAIT` temprano en
vez de congelar la máquina durante milisegundos.

> Hoy el registro de estado está **cableado pero todavía no dice nada útil**: el
> firmware contesta un valor fijo. Llenarlo es el paso "Protocolo v2" del
> roadmap, y es lo que habilita todo lo demás.

**Las cuatro líneas que tienen que llegar al micro.** El post muestra el
esquemático en fotos pero nunca dice cuáles son. Además de los 8 bits de datos
(que pasan por el '245, con `DIR = /RD`, así que la dirección se resuelve sola),
al ATmega tienen que llegar exactamente cuatro señales:

| Pin | Señal | Para qué |
|---|---|---|
| PC0 | Selección decodificada (`U5.Y0`) | Despierta al micro — es `PCINT8`, el disparo del handshake |
| PC1 | `A0` del MSX | Distingue el registro de datos del de comando |
| PC2 | `/RD` del MSX | Dice si el ciclo es lectura o escritura |
| PC3 | Llave del `/WAIT` | **Salida**: es con lo que el micro libera el `/WAIT` |

**El `/WAIT`, en colector abierto.** `/WAIT` es una línea *wired-OR* del bus:
todos los cartuchos la comparten y ninguno debe manejarla en totem-pole. La
salida de un '138 atacándola directamente entra en contención con cualquier otra
placa que la tire a bajo. Acá el `/WAIT` sale por una compuerta NAND de colector
abierto (74LS03): el cartucho **sólo puede tirar la línea a bajo**, nunca
forzarla a alto. En la rev1 es la corrección del paso 4 de
[CORRECCIONES.md](hardware/rev1/CORRECCIONES.md); en la rev2 va en el PCB.

**El mismo micro que usa un Arduino, pero suelto y a 20 MHz.** No hay una placa
Arduino adentro del cartucho: es el ATmega328P pelado, soldado al PCB, con su
cristal y nada más. Y ese cristal es de **20 MHz**, no los 16 de una placa
Arduino: son **un 25 % más de instrucciones** justo en la ventana en la que el
Z80 está congelado esperando la respuesta. El handshake es un
`ISR(PCINT1_vect)` con acceso directo a `PIND` / `PORTD` — nada de
`attachInterrupt()` ni `digitalRead()` mientras la máquina espera.

**Y el código está publicado y explicado.** El original cerraba con un *"sorry I
didn't document the code so much"* y un link a Google Drive. Acá están las dos
mitades en el repo, bajo MIT, con el esquemático, los Gerbers, la BOM y la lista
de correcciones de la rev1 — y el protocolo definido en un solo lugar
([`defs.h`](sdf-1-atmega328p/defs.h)), usado desde los dos lados.

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

## La placa como kit

Nada de ese hardware es específico de leer disquetes. Lo que hay en el cartucho
es un ATmega328P asomado al bus del MSX por dos puertos de I/O decodificados, una
ROM de 16 K para poner rutinas del lado del Z80, y un handshake byte a byte con
`/WAIT`. El DSK es sólo *un* juego de comandos sobre eso: cambiando ROM +
firmware, y manteniendo la misma arquitectura, la misma placa puede ser otra
cosa.

Por eso **el área de islas perforadas es parte del diseño y no un sobrante**. El
ATmega no tiene que traer la periferia adentro; tiene que saber hablarle. Lo que
tu proyecto necesite se arma ahí y se cuelga del I2C o del SPI que salen por el
conector **H1**:

| H1 | 1 | 2 | 3 | 4 | 5 | 6 | 7 | 8 | 9 | 10 |
|---|---|---|---|---|---|---|---|---|---|---|
| | VCC | SCK | MISO | MOSI | CS | PB1 | PB0 | SDA | SCL | GND |

(En la rev1 ese header **no está serigrafiado**; el pinout sale del netlist y es
el de arriba.)

El mapa [`hardware/rev1/senales-rev1.svg`](hardware/rev1/senales-rev1.svg) muestra
qué señal llega a cada pad de la placa, H1 incluido. Con los zócalos vacíos, los
pads de los integrados sirven para tomar el bus del MSX: A0–A15, D0–D7, `/RD`,
`/IORQ`, `/M1`, `/SLTSL`, `/WAIT`, +5 V y GND. `/WR`, `/MREQ`, `/RESET`, `/INT`,
`/BUSDIR` y CLOCK **no llegan a ningún pad**: hay que cablearlas desde el conector.

La mejor prueba de que la placa es genérica está en la placa misma: **el propio
módulo de SD se conecta en la zona experimental**, colgado de H1 igual que lo
estaría un UART o un ESP32. El "drive" ya es, entonces, un perfil montado sobre
una placa genérica — y un armado **sin SD**, poblando sólo lo que tu proyecto
necesita, es un uso perfectamente legítimo y no una versión mutilada.

> **Ojo con lo que todavía no existe.** Lo único probado hoy es el lector de SD.
> La placa *permite* otros usos —puerto serie con un UART I2C/SPI, sensores,
> puente a un micro con WiFi por SPI— pero **todavía no los trae**: no hay
> firmware ni ROM publicados para ellos, ni está separado el "core" del protocolo
> de la parte específica del disco. Si querés hacer el tuyo, hoy el punto de
> partida es leer `defs.h` y el `.ino`, y sumarle comandos.

## El repo

| Directorio | Qué hay |
|---|---|
| [`diskrom/`](diskrom/) | El driver de disco, en ensamblador Z80 |
| [`sdf-1-atmega328p/`](sdf-1-atmega328p/) | Firmware del ATmega328P |
| [`firmware/`](firmware/) | El `.hex` compilado, para grabar sin toolchain |
| [`hardware/`](hardware/) | Una carpeta por revisión de PCB: fuente EasyEDA, Gerbers, esquemático, BOM |
| [`tools/`](tools/) | `mkrom.py`, que arma la imagen de EEPROM, y los scripts que dibujan los mapas del PCB (`make mapas`) |
| `build/` | **No está en el repo** — ver abajo |

## Compilar

    make            las dos cosas
    make rom        solo la DiskROM     -> out/sdf1.rom
    make firmware   solo el firmware    -> out/firmware/
    make check      verifica que estén las herramientas
    make mapas      los mapas SVG de hardware/rev1, desde los Gerbers

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

Requiere el cable de RESET del paso 1 de
[CORRECCIONES.md](hardware/rev1/CORRECCIONES.md), y desenchufar el módulo de SD
antes de grabar. Los detalles están en [firmware/README.md](firmware/README.md).

## Licencia

MIT — ver [LICENSE](LICENSE).

Aplica al código, el diseño de hardware y la documentación **de este repo**. El
MSX-DOS kit de ASCII no está incluido ni cubierto por esta licencia; si lo
conseguís por tu cuenta, se rige por sus propios términos.
