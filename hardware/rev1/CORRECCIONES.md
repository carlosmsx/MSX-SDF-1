# Correcciones a mano — placa rev1 (v1.1)

La rev1 se fabricó antes de la revisión de esquemático. Las correcciones se
aplican a mano sobre la placa. Este documento las lista en el orden en que
conviene hacerlas.

Todas las referencias de pines salen del netlist
(`Netlist_SDF1_2026-08-28.net`), no del dibujo.

> **Antes de tocar el soldador:** verificá el paso 0. Si la firma de la ROM
> está mal, la placa puede estar perfecta y el MSX no va a hacer absolutamente
> nada — y vas a buscar el problema en el lugar equivocado.

---

## 0 · Firma de la ROM (no es un bodge, es el grabado)

El BIOS del MSX busca los bytes `41h 42h` ("AB") en `4000h` para reconocer un
cartucho. Si no están, saltea el slot y no llama a nada.

```bash
od -A x -t x1z -N 8 sdf1.rom.BIN
```

- Los dos primeros bytes de la imagen **deben** ser `41 42`.
- La imagen de 16 K va en el **offset 0x4000** del binario de 64 K.
- Los otros tres bloques de 16 K se rellenan con `FF`, para que la firma no
  aparezca espejada en `8000h` (el BIOS también escanea esa página, y si la
  encuentra dos veces llama a la rutina de init dos veces).

---

## 1 · Pull-down en `MSX_EN_PIN`

**Qué:** resistencia de 10 K entre el **pin 26 del ATmega** (`PC3`) y GND.

**Por qué:** desde el reset hasta que corre `setup()` pasan decenas de
milisegundos en los que los pines del ATmega son entradas en alta impedancia.
`PC3` maneja `G1` de U5, y una entrada TTL sin conectar flota **alto** — o sea
que el decodificador queda habilitado antes de que el firmware exista. El
primer acceso a los puertos 0x00/0x01 clava `/WAIT` y cuelga la máquina.

Con el pull-down, el cartucho es invisible hasta que el firmware decide lo
contrario.

---

## 2 · Sacar `EN` de la cadena de decodificación

**Cortes:**

| Cortar | Entre |
|---|---|
| C1 | `PC3` (ATmega pin 26) → `U5` pin 6 (`G1`) |
| C2 | `U5` pin 15 (`Y0`) → `CON1-7` (`/WAIT`) |

**Puentes:**

| Desde | Hasta |
|---|---|
| `U5` pin 6 (`G1`) | VCC |

**Qué queda:** `U5.Y0` pasa a ser decodificación pura de bus — llamémosla
`/IOSEL`. Sigue yendo a `U2` pin 19 (`/OE` del '245) y a `U3` pin 23 (`PC0`),
que **no se tocan**. Lo único que se le saca es el `/WAIT`, que ahora sale del
integrado del paso 3.

**Por qué:** hoy `EN` está adentro de la decodificación, así que cuando el
firmware lo baja para soltar `/WAIT` apaga también el búfer de datos, en un
momento en que el Z80 todavía no muestreó el bus. Además fabrica él mismo el
flanco que dispara su propia interrupción, y deja una ventana en la que el
decodificador está apagado y un acceso del MSX se pierde en silencio.

---

## 3 · U6 — `74LS03` para `/WAIT` y `/BUSDIR`

Un DIP-14 al aire (*dead bug*) o en piggyback. El `74LS03` es NAND cuádruple
con **salida de colector abierto**, que es lo que piden las dos líneas.

**Alimentación:** pin 14 a VCC, pin 7 a GND, 100 nF entre ambos, lo más corto
posible.

**Las cuatro compuertas:**

| Compuerta | Entradas | Salida | Función |
|---|---|---|---|
| G1 | pines 1 y 2, juntos → `U5` pin 15 | pin 3 | inversor: `/IOSEL` → `IOSEL_H` |
| G2 | pines 4 y 5, juntos → `CON1-14` (`/RD`) | pin 6 | inversor: `/RD` → `RD_H` |
| G3 | pin 9 → pin 3 · pin 10 → `PC3` (ATmega 26) | pin 8 → `CON1-7` | `/WAIT` |
| G4 | pin 12 → pin 3 · pin 13 → pin 6 | pin 11 → `CON1-10` | `/BUSDIR` |

**Pull-ups:** 4K7 de VCC al pin 3, y 4K7 de VCC al pin 6. Los hacen falta
porque las salidas son de colector abierto y ahí alimentan entradas del mismo
chip. Las salidas G3 y G4 **no** llevan pull-up: la placa madre del MSX ya lo
tiene en `/WAIT` y en `/BUSDIR`.

**Por qué `/WAIT`:** es una línea compartida con pull-up en la máquina,
pensada para atacarse en colector abierto. La salida totem-pole del '138 la
fuerza activamente a alto cuando está inactiva, y pelea contra la máquina o
contra otro cartucho que quiera pedir espera.

**Por qué `/BUSDIR`:** cuando un cartucho pone datos en el bus durante una
lectura de I/O tiene que bajar `/BUSDIR` para que la máquina invierta su búfer
bidireccional. Sin eso, en las máquinas que tienen ese búfer la lectura
devuelve basura — y en las que no lo tienen anda igual. Es el clásico que hace
que el cartucho funcione en una máquina y no en otra sin cambiar nada.

> **No uses un diodo Schottky en lugar del '03.** El `V_OL` del '138 llega a
> 0,5 V y un Schottky cae 0,3–0,4 V: el nivel bajo en `/WAIT` queda en
> 0,8–0,9 V contra un `V_IL` máximo de 0,8 V. Queda justo del lado malo del
> umbral.

---

## 4 · RESET del MSX a `PB0`

**Qué:** cable de `CON1-15` (`/RESET` del MSX) a `H1-7` (`PB0`, ATmega pin 14).

**Por qué así y no al RESET del ATmega:** si reseteás el ATmega, se desmonta la
SD y hay que volver a inicializar SdFat, que en una tarjeta lenta puede tardar
cientos de milisegundos — justo cuando la máquina ya está buscando la DiskROM.
Llevándolo a un GPIO, el firmware resetea sólo su máquina de protocolo
(`_cmd_st`, transferencia parcial, soltar `/WAIT`) y conserva el montaje y la
imagen DSK seleccionada.

**Requiere firmware:** sin código que mire `PB0`, este cable no hace nada.

---

## 5 · `PC6` a un pinheader (ICSP)

**Qué:** cable del **pin 1 del ATmega** (`RESET`) a un pad libre o a un pin de
header accesible.

Con eso `H1` completa el ICSP: `1=VCC`, `2=SCK`, `3=MISO`, `4=MOSI`, `10=GND`,
más este `RESET`.

```bash
avrdude -c usbasp -p m328p -U flash:w:build/sdf-1-atmega328p.ino.hex:i
```

> **Desconectá el módulo de SD antes de programar.** Muchos módulos no liberan
> bien `MISO` con su `CS` inactivo y el programador lee basura. Si el ICSP sale
> intermitente, es lo primero a descartar.

---

## 6 · Cambios en el zócalo (sin soldadura)

| Ref | Sacar | Poner |
|---|---|---|
| U2 | `SN74LS245N` | `SN74HCT245N` |
| U4, U5 | `SN74LS138N` | `SN74HCT138N` |

**Por qué:** el `V_OH` mínimo garantizado de un '138 LS es 2,7 V, y el `V_IH`
del ATmega328P a 5 V es 3,0 V. `U5.Y0` maneja `PC0`, así que está fuera de
especificación. En la práctica una salida LS descargada se para en 3,4 V y
anda, pero es margen prestado. Los HCT tienen entradas compatibles con TTL y
salidas a nivel CMOS, mismo pinout y mismo precio, y de paso cargan el bus del
MSX con microamperes en vez de 0,4 mA por entrada.

---

## 7 · El cristal es de 20 MHz — el BOM está mal

**El cristal de la placa va a 20 MHz.** Decidido; no es algo a verificar.

Lo que está mal es el BOM: la fila de `X1` dice `20MHz` en Comment y en Value,
pero el Manufacturer Part es `HC-49/U-S16000000ABJB`, que es el de **16 MHz**.

```
"11","1","20MHz","X1","HC-49US_L11.5-W4.5-P4.88","20MHz","HC-49/U-S16000000ABJB","CITIZEN","","LCSC"
```

**No edites `sdf1-bom.csv` a mano**: es un derivado de `sdf1.epro2`. Hay que
corregir el Manufacturer Part en EasyEDA Pro y regenerar los derivados juntos,
como dice [../README.md](../README.md). Por el patrón de la familia el correcto
sería `HC-49/U-S20000000ABJB`, pero confirmalo en LCSC antes de pedirlo.

**Por qué importa más de lo que parece:** MiniCore usa los **mismos fuses**
para 16 y para 20 MHz externos — el `cksel_bits` es idéntico, lo único que
cambia es el `F_CPU` con el que se compila. Un cristal equivocado no produce
ningún síntoma diagnosticable: el chip arranca, la SD monta, y todas las
temporizaciones quedan 25 % corridas. Es de los errores más caros de encontrar
porque no falla, anda mal.

El firmware se compila con `clock=20MHz_external` fijo en el `Makefile`.

---

## Verificación, con el cartucho fuera del MSX

Antes de enchufarlo, con el téster en continuidad:

- [ ] `PC3` (ATmega 26) **ya no** tiene continuidad con `U5` pin 6
- [ ] `U5` pin 6 tiene continuidad con VCC
- [ ] `U5` pin 15 **ya no** tiene continuidad con `CON1-7`
- [ ] `U5` pin 15 **sí** tiene continuidad con `U2` pin 19 y con `U3` pin 23
- [ ] `U6` pin 8 tiene continuidad con `CON1-7`
- [ ] `U6` pin 11 tiene continuidad con `CON1-10`
- [ ] `U6` pin 14 a VCC, pin 7 a GND
- [ ] 10 K entre `PC3` y GND
- [ ] No hay continuidad entre VCC y GND

---

## Después de los bodges: firmware

Los parches de hardware no se lucen solos. Del lado del software:

1. **Reactivar el checksum** en `DSKIO` y devolver carry con `A=4` si falla.
   Hoy las dos ramas terminan en `SCF`/`CCF` — carry limpio siempre — y la
   comparación está comentada. Sin eso no tenés cómo saber si el bodge quedó
   bien: un error de hardware llega a MSX-DOS como datos corruptos, no como
   error de disco.
2. **Handshake en `INIHRD`:** que el firmware escriba un valor mágico en
   `_stat` al terminar de inicializar, y que `INIHRD` gire leyendo el puerto
   0x01 hasta verlo, con timeout. No uses "distinto de `FFh`": el bus flotando
   *suele* dar `FFh` pero no está garantizado.
3. **Recién después**, sacar los `EX (SP),HL` de las primitivas de
   transferencia. Son 38 estados de espera por byte que existen para tapar el
   problema del paso 2 de este documento. Sacalos **una vez que hayas
   verificado** que el bodge funciona, no antes, y con el checksum activo para
   que te avise si te pasaste.
