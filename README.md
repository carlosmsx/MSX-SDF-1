# MSX-SDF-1

Un **kit de desarrollo para MSX**: una placa de I/O universal y experimental en
formato cartucho, con un área de islas perforadas para montar hardware propio.
Es compatible con MSX1 en adelante.

El objetivo es implementar una interfaz sencilla, con componentes que se puedan
conseguir en cualquier tienda de electrónica, económicamente accesibles y de
tipo **THT** (*thru hole*), de manera de facilitar el ensamblaje al hobbista.
Igualmente, siempre es posible hacer un PCB en SMT para quien lo prefiera.

El **primer proyecto de ejemplo** es un **lector de memorias SD**: el cartucho
presenta al MSX una unidad de disco estándar y arranca MSX-DOS y Disk BASIC desde
imágenes `.DSK` guardadas en la tarjeta. Para el MSX se trata de una disketera
común ([página del lector](https://dpc200.com.ar/sdf1-sd)).

El **segundo proyecto** se suma al primero: un **reloj con pila**, basado en un
DS1307 conectado al bus I2C de H1. MSX-DOS arranca con la fecha y la hora
correctas, `CALL RTC` permite consultarlas y ajustarlas, y `OPEN "RTC:"` permite
leerlas desde un programa ([página del reloj](https://dpc200.com.ar/sdf1-rtc)).
Estos son, por ahora, los dos únicos proyectos terminados.

> **Estado: en desarrollo.** La rev1 está fabricada y **requiere correcciones a
> mano** antes de funcionar; ver [hardware/rev1/CORRECCIONES.md](hardware/rev1/CORRECCIONES.md).
> Todavía no es un proyecto que se pueda armar y usar directamente.

## Las decisiones de diseño

Este proyecto está inspirado en el [Virtual MSX Disk
Drive](https://codinglab.blogspot.com/2013/01/virtual-msx-disk-drive.html) que
Raul publicó en 2013, del que toma la técnica de fondo: bajar el `/WAIT` del Z80
con la misma señal que decodifica el puerto, y responder con un microcontrolador
mientras la máquina permanece detenida a mitad del ciclo. Sobre esa base, el
SDF-1 incorpora algunos agregados y mejoras:

**Funciona de forma autónoma.** El ATmega lee el `.DSK` por sí mismo desde una
microSD, por SPI, de modo que el cartucho funciona con la máquina sola, sin una
PC conectada.

**Decodificación completa.** Dos 74LS138 decodifican `A7..A1` y seleccionan **únicamente `0x00` y `0x01`**. Las salidas `Y1..Y7` del segundo '138
quedan libres (cubren `0x02`–`0x0F`) y no están conectadas: trasladar el par de
puertos consiste en rutear otra salida. En la rev2 esto pasará a ser un jumper,
de modo que un conflicto se pueda evitar sin modificar el cobre.

**Dos puertos o, mejor dicho, dos registros.** `A0` no se decodifica como
selección de chip, sino que actúa como selector de registro. Un puerto se usa
para transferir datos y el otro para controlar el diálogo, y esa separación la
resuelve el hardware: el protocolo nunca necesita determinar si el byte recibido
es un dato o una orden.

| Puerto | `A0` | Escritura (`OUT`) | Lectura (`IN`) |
|---|---|---|---|
| `0x00` | 0 | **Datos**, del MSX al micro | **Datos**, del micro al MSX |
| `0x01` | 1 | **Comando**: orden a ejecutar | **Estado**: situación del controlador |

El registro de datos es **bidireccional**: el mismo registro sirve para los 512
bytes que el MSX escribe en un sector y para los 512 que lee. La dirección la
determina `/RD`, que llega al micro y además controla el `DIR` del '245, de modo
que ni el driver ni el firmware necesitan acordar el sentido del bus.

El otro registro es **asimétrico de manera intencional**: escribirlo *dispara*
una acción (leer un sector, montar una imagen o cualquier otra que agregue el
firmware), y leerlo es una *consulta* sin efectos secundarios. Ese lado de
lectura es el que convierte el handshake en un protocolo propiamente dicho:
permite informar "ocupado", "se produjo un error" o "hay tantos bytes
disponibles" **sin ocupar el canal de datos**, que es precisamente lo necesario
para liberar el `/WAIT` antes, en lugar de detener la máquina durante
milisegundos.

> Por ahora el registro de estado está **cableado, pero todavía no informa nada
> útil**: el firmware devuelve un valor fijo. Completarlo corresponde a la etapa
> "Protocolo v2" del roadmap, y es lo que habilita todo lo demás.

**Las cuatro líneas que deben llegar al micro.** Además de los 8 bits
de datos (que pasan por el '245, con `DIR = /RD`, de modo que la dirección se
resuelve sola), al ATmega deben llegar exactamente cuatro señales:

| Pin | Señal | Función |
|---|---|---|
| PC0 | Selección decodificada (`U5.Y0`) | Despierta al micro: es `PCINT8`, el disparo del handshake |
| PC1 | `A0` del MSX | Distingue el registro de datos del de comando |
| PC2 | `/RD` del MSX | Indica si el ciclo es de lectura o de escritura |
| PC3 | Llave del `/WAIT` | **Salida**: es la señal con la que el micro libera el `/WAIT` |

**El `/WAIT`, en colector abierto.** `/WAIT` es una línea *wired-OR* del bus:
todos los cartuchos la comparten y ninguno debe manejarla en totem-pole. Por
eso el `/WAIT` sale por una
compuerta NAND de colector abierto (74LS03): el cartucho **sólo puede llevar la
línea a nivel bajo**, nunca forzarla a nivel alto. En la rev1 corresponde al
paso 4 de [CORRECCIONES.md](hardware/rev1/CORRECCIONES.md); en la rev2 estará
incluido en el PCB.

**ATmega328P a 20 MHz.** El cartucho no lleva una placa Arduino, sino el
ATmega328P soldado directamente al PCB, con un cristal de **20 MHz**. Eso da
**un 25 % más de instrucciones** que los 16 MHz habituales, precisamente en el
intervalo en que el Z80 está detenido esperando la respuesta. El handshake es un `ISR(PCINT1_vect)` con acceso directo a `PIND` /
`PORTD`, sin `attachInterrupt()` ni `digitalRead()` mientras la máquina espera.

**Código publicado y documentado.** En este repositorio están las dos mitades, bajo licencia MIT, junto con el esquemático,
los Gerbers, la BOM y la lista de correcciones de la rev1, y el protocolo está
definido en un único lugar ([`defs.h`](sdf-1-atmega328p/defs.h)), que se usa
desde ambos lados.

## Cómo está hecho

El cartucho tiene dos mitades que se comunican por un puerto de I/O:

**La DiskROM (Z80)**: una EEPROM W27C512 con la DiskROM de 16 K mapeada en la
página 1 (`0x4000`). El driver de disco es propio y está en
[`diskrom/DSKDRV.MAC`](diskrom/DSKDRV.MAC); se linkea contra el kernel MSX-DOS
de ASCII, como en cualquier interfaz de disco de MSX. La imagen ocupa 64 K
planos de manera intencional: hay espacio de sobra, y así la EEPROM puede
reprogramarse con un adaptador común.

**El firmware (ATmega328P)**: [`sdf-1-atmega328p/`](sdf-1-atmega328p/). Un
ATmega328P a 20 MHz con cristal externo actúa como controlador: recibe comandos
del Z80, lee y escribe sectores de la imagen `.DSK` en la SD por SPI, y mantiene
el `/WAIT` del MSX mientras tanto. Las interrupciones son PCINT nativas, sin
librerías, ya que los tiempos del protocolo dependen de ello.

**La lógica de decodificación es 74HCxx discreta**, sin GAL ni CPLD. Implica más
integrados, pero cualquiera puede reproducirla sin un programador de PLDs. Los
archivos `.PLD` de versiones anteriores son históricos.

Cualquier cambio en los códigos de comando afecta a **las dos** mitades: el
protocolo está definido en [`sdf-1-atmega328p/defs.h`](sdf-1-atmega328p/defs.h)
y se usa desde ambos lados.

## La placa como kit

Nada de este hardware es específico para leer disquetes. El cartucho contiene
un ATmega328P conectado al bus del MSX a través de dos puertos de I/O
decodificados, una EEPROM de 64 K regrabable con un adaptador, destinada a las
rutinas del lado del Z80 (la DiskROM del lector de SD ocupa sólo 16 K de esos
64), y un handshake byte a byte con `/WAIT`. El DSK es sólo *un* conjunto de
comandos sobre esa base: cambiando la ROM y el firmware, y manteniendo la misma
arquitectura, la misma placa puede cumplir otra función.

Por eso **el área de islas perforadas es parte del diseño y no un espacio
sobrante**. El ATmega no necesita incorporar los periféricos, sino saber
comunicarse con ellos. Lo que cada proyecto necesite se arma en esa área y se
conecta al I2C o al SPI disponibles en el conector **H1**:

| H1 | 1 | 2 | 3 | 4 | 5 | 6 | 7 | 8 | 9 | 10 |
|---|---|---|---|---|---|---|---|---|---|---|
| | VCC | SCK | MISO | MOSI | CS | PB1 | PB0 | SDA | SCL | GND |

(En la rev1 este conector **no está serigrafiado**; el pinout surge del netlist
y es el indicado arriba.)

El mapa [`hardware/rev1/senales-rev1.svg`](hardware/rev1/senales-rev1.svg)
muestra qué señal llega a cada pad de la placa, incluido H1. Con los zócalos
vacíos, los pads de los integrados permiten acceder al bus del MSX: A0–A15,
D0–D7, `/RD`, `/IORQ`, `/M1`, `/SLTSL`, `/WAIT`, +5 V y GND. `/WR`, `/MREQ`,
`/RESET`, `/INT`, `/BUSDIR` y CLOCK **no llegan a ningún pad**, por lo que deben
cablearse desde el conector.

La mejor prueba de que la placa es genérica está en la propia placa: **el módulo
de SD se conecta en la zona experimental**, a través de H1, del mismo modo en
que se conectaría un UART o un ESP32. El lector de disco es, entonces, un perfil
montado sobre una placa genérica, y un armado **sin SD**, con sólo los
componentes que requiera cada proyecto, es un uso perfectamente válido y no una
versión reducida.

> **Alcance actual.** Lo probado hasta ahora es el lector de SD y el reloj
> DS1307 que se le suma. La placa *permite* otros usos (puerto serie mediante un
> UART I2C/SPI, sensores, puente a un microcontrolador con WiFi por SPI), pero
> **todavía no los incluye**: no hay firmware ni ROM publicados para ellos, y
> aún no está separado el núcleo del protocolo de la parte específica del disco.
> Para desarrollar un proyecto propio, el punto de partida hoy es leer `defs.h`
> y el `.ino`, y agregar los comandos necesarios.

## El repositorio

| Directorio | Contenido |
|---|---|
| [`diskrom/`](diskrom/) | El driver de disco, en ensamblador Z80 |
| [`sdf-1-atmega328p/`](sdf-1-atmega328p/) | Firmware del ATmega328P |
| [`firmware/`](firmware/) | El `.hex` compilado, para grabar sin toolchain |
| [`hardware/`](hardware/) | Una carpeta por revisión de PCB: fuente EasyEDA, Gerbers, esquemático, BOM |
| [`tools/`](tools/) | `mkrom.py`, que genera la imagen de la EEPROM, y los scripts que dibujan los mapas del PCB (`make mapas`) |
| `build/` | **No está incluido en el repositorio**; ver más abajo |

## Compilación

    make            ambas partes
    make rom        sólo la DiskROM     -> out/sdf1.rom
    make firmware   sólo el firmware    -> out/firmware/
    make check      verifica que estén las herramientas
    make mapas      los mapas SVG de hardware/rev1, a partir de los Gerbers

### DiskROM

Se necesitan **N80 y LK80** de [Nestor80](https://github.com/Konamiman/Nestor80/releases),
descomprimidos en `tools/bin/`. Son el reemplazo moderno de M80 y L80: leen y
escriben el mismo formato `.REL`, se ejecutan de forma nativa en Windows, Linux
y macOS, y tienen licencia MIT. Ya no es necesario un emulador de CP/M.

### El MSX-DOS kit, que no está incluido

`build/` debe contener una copia propia del **MSX-DOS kit de ASCII** (1984-85):
los ocho archivos `.REL` del kernel contra los que se linkea el driver.

**Estos archivos no se redistribuyen en este repositorio.** Son código de
terceros y no tengo los derechos para publicarlos, por lo que el repositorio
incluye la receta y no el material, con el mismo criterio que siguen los
emuladores que se distribuyen sin las BIOS. Por la misma razón tampoco se
publica `out/sdf1.rom`, ya que contiene ese kernel linkeado.

El encabezado del [`Makefile`](Makefile) indica qué archivos se requieren y en
qué orden; `make check` avisa si falta alguno. Con el kit en su lugar,
`make rom` reconstruye la imagen completa.

### Firmware

No es necesario instalar `arduino-cli`: el Arduino IDE 2 incluye el suyo, y el
`Makefile` lo utiliza con los mismos cores y librerías ya instalados. Se
requieren [MiniCore](https://github.com/MCUdude/MiniCore) y el fork de
[SdFat de Adafruit](https://github.com/adafruit/SdFat) (**no** el de greiman),
ya que el sketch usa `SPI_FULL_SPEED` y el typedef `File`.

## Grabación

    make fuses      una vez por chip
    make flash      el firmware, por ICSP

Requiere el cable de RESET del paso 1 de
[CORRECCIONES.md](hardware/rev1/CORRECCIONES.md), y desconectar el módulo de SD
antes de grabar. Los detalles están en [firmware/README.md](firmware/README.md).

## Licencia

MIT; ver [LICENSE](LICENSE).

Aplica al código, al diseño de hardware y a la documentación **de este
repositorio**. El MSX-DOS kit de ASCII no está incluido ni cubierto por esta
licencia; quien lo obtenga por su cuenta deberá atenerse a sus propios términos.
