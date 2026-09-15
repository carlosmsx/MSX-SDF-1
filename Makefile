# Compilacion del SDF-1: la DiskROM (Z80) y el firmware (ATmega328P).
#
# Las dos mitades del cartucho se compilan por separado y no dependen una de
# la otra, pero el protocolo entre ambas si: si tocas los codigos de comando,
# tocas los dos lados. Por eso viven en el mismo Makefile.
#
# Salidas, todas en out/ (que esta en .gitignore):
#   out/sdf1.rom                        imagen de 64 KB para la W27C512
#   out/page2.bin                       rutinas de manejo, pagina 2 de la ROM
#   out/firmware/*.hex                  firmware del ATmega328P
#
# OJO: build/ NO es una carpeta de salida, es el MSX-DOS kit de ASCII. No
# apuntes ningun --output-dir ahi.
#
# Y OJO DE NUEVO: build/ NO VIENE EN ESTE REPO. Es codigo de terceros y no
# esta redistribuido aca. Tenes que poner tu propia copia del MSX-DOS kit en
# build/ antes de "make rom" — los ocho .REL que lista MODULES/MODULES2, el
# BOOT.Z80 (el sector de arranque que escriben DSKFMT y CALL SDFNEW) y el
# README del kit. "make check" te avisa si falta.
#
# Por lo mismo out/sdf1.rom NO se publica: contiene el kernel de ASCII
# linkeado con nuestro driver. Se compila localmente, no se distribuye.
# out/page2.bin, en cambio, es solo codigo propio (diskrom/PAGE2.MAC).
#
#
# ============================ DiskROM (Z80) ============================
#
# El driver se ensambla y se linkea contra los .REL del MSX-DOS kit de ASCII
# (build/), que son formato relocalizable de Microsoft. Historicamente eso
# obligaba a usar M80 y L80 dentro de un emulador CP/M.
#
# Nestor80 los reemplaza con binarios nativos: N80 es compatible con MACRO-80
# y LK80 con LINK-80, y leen y escriben el mismo formato .REL. Corren en
# Windows, Linux y macOS, y son MIT.
#
#   https://github.com/Konamiman/Nestor80/releases
#
# Bajar las variantes FrameworkDependant (necesitan .NET 6+) y descomprimir
# en tools/bin/. Esa carpeta esta en .gitignore: son binarios de terceros.
#
# Uso:
#   make            compila las dos cosas
#   make rom        solo la DiskROM        -> out/sdf1.rom
#   make firmware   solo el firmware       -> out/firmware/
#   make fuses      graba los fuses del ATmega (una vez por chip)
#   make flash      graba el firmware por ICSP
#   make clean      borra out/
#   make check      solo verifica que estan las herramientas
#   make icsp       comprueba la conexion ICSP con el ATmega, sin escribir
#   make mapas      regenera los SVG de hardware/rev1 desde los Gerbers
#
# Anda igual desde Git Bash que desde PowerShell o cmd. Sin sh en el PATH,
# make corre las recetas con cmd.exe, asi que aca no hay sintaxis de sh
# (test, mkdir -p, rm -rf, VAR=valor delante de un comando, $$(...), lineas
# partidas con \). Los chequeos van con funciones de make y lo que toca
# carpetas va con python, que ya es dependencia por mkrom.py.

DRIVER   := diskrom/DSKDRV.MAC
PAGE2    := diskrom/PAGE2.MAC
PROTO    := diskrom/PROTOCOL.INC
KIT      := build
OUT      := out

N80      := tools/bin/N80.exe
LK80     := tools/bin/LK80.exe
PYTHON   := python

# Orden de modulos, tomado de build/LINK.SUB. NO lo cambies sin leer
# build/README: el orden define el mapa de memoria de la DiskROM.
#
# Ojo con la linea original:
#
#   l80 /p:4000,/d:f237,doshead,bios,msxdos,smldisp,init,
#       dskbasic,msxdata,dskdrv,basdata,msxdos/n/x/y/e
#
# El ultimo "msxdos" NO es un modulo: el /n que lo sigue significa "llama
# MSXDOS al archivo de salida". Los modulos a linkear son nueve.
MODULES  := DOSHEAD BIOS MSXDOS SMLDISP INIT DSKBASIC MSXDATA
MODULES2 := BASDATA

# 0x4000 = pagina 1, donde el BIOS busca una DiskROM.
# 0xF237 = area de datos en la RAM del sistema.
# En LK80 los valores hexadecimales llevan una "h" al final.
CODE_ORG := 4000h
DATA_ORG := F237h

# 0xF327 = donde empiezan los datos de BASDATA (AUXBOD). Son los 240 bytes de
# MSXDATA a partir de F237, y nada mas: ver el comentario de LK80_ARGS.
DATA_ORG2 := F327h

REL_KIT  := $(addprefix $(KIT)/,$(addsuffix .REL,$(MODULES)))
REL_KIT2 := $(addprefix $(KIT)/,$(addsuffix .REL,$(MODULES2)))

.PHONY: all rom firmware fuses flash clean check check-rom check-firmware icsp mapas

all: rom firmware

rom: $(OUT)/sdf1.rom

check: check-rom check-firmware

check-rom:
	$(if $(wildcard $(N80)),,$(error falta $(N80) — ver el encabezado de este Makefile))
	$(if $(wildcard $(LK80)),,$(error falta $(LK80) — ver el encabezado de este Makefile))
	$(if $(wildcard $(KIT)/DOSHEAD.REL),,$(error falta el MSX-DOS kit en $(KIT)/))
	$(if $(wildcard $(KIT)/BOOT.Z80),,$(error falta $(KIT)/BOOT.Z80 del MSX-DOS kit: es el sector de arranque))
	$(info N80  $(shell $(N80) --version))
	$(info LK80 $(shell $(LK80) --version))
	$(info kit  $(words $(MODULES) $(MODULES2)) modulos en $(KIT)/)

# python y no mkdir -p: desde cmd, "mkdir -p out" crea tambien una carpeta "-p".
$(OUT):
	@$(PYTHON) -c "import os; os.makedirs(r'$(OUT)', exist_ok=True)"

# --- 1. Ensamblar el driver ---------------------------------------------
# -bt rel: relocalizable formato M80, que es lo que espera LK80.
#
# -l8c: OBLIGATORIO. M80 truncaba los simbolos publicos y externos a 6
# caracteres; N80 no lo hace por defecto. Sin este flag el linkeo falla con
# "can't resolve external symbol reference" en OEMSTA (el kit espera 6
# caracteres, el driver exporta OEMSTATEMENT) y en $SECBU (el driver pide
# $SECBUF, 7 caracteres).
#
# -id $(KIT): DSKDRV.MAC incluye el BOOT.Z80 del kit como plantilla del sector
# de arranque (ver el final de DSKDRV.MAC).
$(OUT)/DSKDRV.REL: $(DRIVER) $(PROTO) $(KIT)/BOOT.Z80 | $(OUT)
	$(N80) $(DRIVER) $@ -bt rel -l8c -id $(KIT)

# --- 2. Linkear contra el kit -------------------------------------------
# --code y --data son "link sequence items": aplican al archivo siguiente,
# por eso van antes del primer .REL. --data ademas pone el linker en modo
# "codigo y datos separados", que es el equivalente del /d: de L80.
#
# Los argumentos van en una variable para que la receta sea una sola linea:
# cmd no entiende las lineas de receta partidas con \.
#
# --data $(DATA_ORG2) antes de BASDATA: OBLIGATORIO. El .REL que arma N80 para
# el driver le hace reservar a LK80 un byte de datos aunque no tenga ninguno,
# y sin esto todo el segmento de datos de BASDATA queda un byte corrido:
# AUXBOD en F328 en vez de F327, y detras RAMAD0, $SECBUF, $DPBLIST, XFER...
# Son variables de sistema de MSX-DOS con direccion fija; corridas, la maquina
# muestra el logo, hace un beep y se reinicia. Con esto el link reproduce byte
# a byte, en todo el codigo, la ROM de ZiggyBox armada con M80/L80.
LK80_ARGS = --code $(CODE_ORG) --data $(DATA_ORG) \
            $(REL_KIT) $(OUT)/DSKDRV.REL --data $(DATA_ORG2) $(REL_KIT2) \
            --output-format hex --output-file
$(OUT)/msxdos.hex: $(OUT)/DSKDRV.REL
	$(LK80) $(LK80_ARGS) $@

# --- 3. Ensamblar la pagina 2 -------------------------------------------
# Las rutinas de manejo (diskrom/PAGE2.MAC), en 8000h. Binario absoluto y
# aparte: no se linkea con el kit. La tabla de saltos del principio es el
# contrato con los P2_... de DSKDRV.MAC.
$(OUT)/page2.bin: $(PAGE2) $(PROTO) | $(OUT)
	$(N80) $(PAGE2) $@ -bt abs

# --- 4. Armar la imagen de EEPROM ---------------------------------------
# Ubica el codigo de las dos paginas, rellena los 64 KB con FF y verifica la
# firma "AB".
$(OUT)/sdf1.rom: $(OUT)/msxdos.hex $(OUT)/page2.bin tools/mkrom.py
	$(PYTHON) tools/mkrom.py $< $@ $(OUT)/page2.bin

clean:
	@$(PYTHON) -c "import shutil; shutil.rmtree(r'$(OUT)', ignore_errors=True)"

# ======================= Firmware (ATmega328P) =========================
#
# No hace falta instalar arduino-cli: el Arduino IDE 2 trae el suyo adentro,
# y usa los mismos cores y librerias que ya tenes instalados. O sea que esto
# compila exactamente lo mismo que el IDE, sin el IDE.
#
# Si preferis el arduino-cli oficial, alcanza con:  make firmware ARDUINO_CLI=arduino-cli
#
# Nada de esto se versiona: son rutas de la maquina de cada uno.

SKETCH   := sdf-1-atmega328p
FW_OUT   := $(OUT)/firmware
FW_HEX   := $(FW_OUT)/$(SKETCH).ino.hex

# Las carpetas del usuario salen de las variables de entorno de Windows,
# leidas por make mismo y con las barras dadas vuelta. Nada de $(shell): desde
# PowerShell o cmd make no tiene sh ni cygpath, y un $(shell cygpath ...) deja
# las rutas vacias sin avisar. En Linux/macOS caen en $HOME; si hace falta,
# pasa ARDUINO_DATA y ARDUINO_USER a mano.
WINHOME  := $(if $(USERPROFILE),$(subst \,/,$(USERPROFILE)),$(HOME))
WINLOCAL := $(if $(LOCALAPPDATA),$(subst \,/,$(LOCALAPPDATA)),$(HOME)/.local/share)

ARDUINO_CLI  ?= C:/Program Files/Arduino IDE/resources/app/lib/backend/resources/arduino-cli.exe
ARDUINO_DATA ?= $(WINLOCAL)/Arduino15
ARDUINO_USER ?= $(firstword $(wildcard \
                  $(WINHOME)/OneDrive/Documentos/Arduino \
                  $(WINHOME)/Documents/Arduino \
                  $(WINHOME)/Documentos/Arduino))

# ---- FQBN -------------------------------------------------------------
#
# clock=20MHz_external NO se toca. El cristal de la placa es de 20 MHz.
#
# El BOM v1.1 dice otra cosa en el Manufacturer Part (...16000000...) y esta
# mal: hay que corregir el BOM, no el firmware.
#
# Y ojo, porque esto no da error si te equivocas: MiniCore usa los MISMOS
# fuses para 16 y 20 MHz (mismo cksel_bits), asi que un cristal equivocado
# no se nota en el arranque — solo corre todas las temporizaciones un 25 %.
#
# LTO=Os es el default del IDE (LTO desactivado). No lo prendas a la ligera:
# la ISR es sensible al inlining y hoy los tiempos del protocolo dependen de
# lo que tarda — ver la advertencia de §4.4 de GUIA.md. Si lo cambias, medi.
#
# bootloader=no_bootloader es lo correcto hoy: no hay UART disponible en el
# cartucho. Cuando exista el bootloader propio, esto cambia.
FQBN := MiniCore:avr:328:clock=20MHz_external,BOD=2v7,LTO=Os,variant=modelP,eeprom=keep,bootloader=no_bootloader

# Programador ISP. usbasp es el barato y el que asume CORRECCIONES.md paso 1.
PROGRAMMER ?= usbasp

# avrdude para "make flash": el mismo que instala el Arduino IDE, sin PATH.
# Si hay mas de uno se queda con el ultimo en orden alfabetico; si queres
# otro, pasalo a mano: make flash AVRDUDE=/ruta/a/avrdude
AVRDUDE      ?= $(lastword $(sort $(wildcard \
                  $(ARDUINO_DATA)/packages/*/tools/avrdude/*/bin/avrdude \
                  $(ARDUINO_DATA)/packages/*/tools/avrdude/*/bin/avrdude.exe)))
AVRDUDE_CONF ?= $(dir $(AVRDUDE))../etc/avrdude.conf

# El -p de avrdude sale del FQBN. modelP es el ATmega328P (firma 1E950F) y
# modelNonP el 328 sin P (1E9514): mismo micro, pero avrdude no graba si la
# firma no coincide con la del modelo que le pasas.
AVR_PART := $(if $(findstring variant=modelNonP,$(FQBN)),m328,m328p)

# Las dos carpetas que necesita el .exe para encontrar cores y librerias. Van
# con export y no como VAR=valor delante del comando: eso es sintaxis de sh, y
# desde PowerShell o cmd make no tiene sh.
export ARDUINO_DIRECTORIES_DATA = $(ARDUINO_DATA)
export ARDUINO_DIRECTORIES_USER = $(ARDUINO_USER)
ACLI = "$(ARDUINO_CLI)"

# Version del arduino-cli; queda vacia si no lo encuentra. Sirve tambien de
# chequeo de que existe: la ruta tiene espacios y $(wildcard) no la maneja.
ACLI_VERSION = $(shell $(ACLI) version)

firmware: $(FW_HEX)

$(FW_HEX): $(SKETCH)/$(SKETCH).ino $(SKETCH)/defs.h
	$(ACLI) compile -b "$(FQBN)" --output-dir $(FW_OUT) ./$(SKETCH)

check-firmware:
	$(if $(ACLI_VERSION),,$(error falta arduino-cli en '$(ARDUINO_CLI)'))
	$(if $(wildcard $(ARDUINO_DATA)/packages/MiniCore),,$(error falta MiniCore en $(ARDUINO_DATA)))
	$(if $(wildcard $(ARDUINO_USER)/libraries),,$(error no encuentro el sketchbook: pasa ARDUINO_USER a mano))
	$(if $(AVRDUDE),,$(error falta avrdude en $(ARDUINO_DATA)/packages — ver AVRDUDE))
	$(if $(wildcard $(AVRDUDE_CONF)),,$(error falta avrdude.conf en '$(AVRDUDE_CONF)'))
	$(info cli   $(ACLI_VERSION))
	$(info data  $(ARDUINO_DATA))
	$(info user  $(ARDUINO_USER))
	$(info fqbn  $(FQBN))
	$(info dude  $(AVRDUDE))
	$(info part  $(AVR_PART) con $(PROGRAMMER))

# ---- Grabado por ICSP -------------------------------------------------
#
# Requiere el cable de RESET del paso 1 de hardware/rev1/CORRECCIONES.md.
#
# Desenchufa el modulo de SD antes de grabar: muchos no sueltan MISO con su
# CS inactivo y el programador lee basura.

# Los fuses se graban UNA VEZ por chip, no en cada compilada. Con
# bootloader=no_bootloader esto no escribe ningun bootloader, solo fusea.
#
# Para el FQBN de arriba MiniCore calcula: lfuse=0xF7 hfuse=0xD7 efuse=0xFD
# (GUIA.md §7.3 dice hfuse=0xD6, que esta mal: deja BOOTRST activo apuntando
# a una seccion de bootloader vacia).
fuses:
	$(ACLI) burn-bootloader -b "$(FQBN)" -P $(PROGRAMMER)

# Aca NO se usa "arduino-cli upload". Con programador, MiniCore graba
# {proyecto}.with_bootloader.hex, pero con bootloader=no_bootloader su propio
# hook de compilacion (delete_merged_output) borra ese archivo, asi que el
# upload fallaba siempre con "with_bootloader.hex is not readable".
#
# Sin bootloader el .ino.hex ya es la imagen completa: se graba directo.
# avrdude borra antes de escribir y verifica despues; la EEPROM sobrevive al
# borrado porque el fuse EESAVE esta programado (eeprom=keep).
flash: $(FW_HEX)
	"$(AVRDUDE)" -C "$(AVRDUDE_CONF)" -c $(PROGRAMMER) -p $(AVR_PART) -U flash:w:$(FW_HEX):i

# Comprueba la conexion ICSP sin escribir nada: lee la firma, los fuses y el
# lock. Si contesta, el cableado esta bien; los fuses del SDF-1 son
# lfuse F7, hfuse D7, efuse FD. Si dice "target does not answer", revisar el
# cable, la alimentacion, el RESET y el modulo de SD (CORRECCIONES.md paso 1).
# Para comparar ademas la flash con el firmware compilado:
#   make icsp ICSP_EXTRA="-U flash:v:$(FW_HEX):i"
ICSP_EXTRA ?=

icsp:
	"$(AVRDUDE)" -C "$(AVRDUDE_CONF)" -c $(PROGRAMMER) -p $(AVR_PART) -U lfuse:r:-:h -U hfuse:r:-:h -U efuse:r:-:h -U lock:r:-:h $(ICSP_EXTRA)

# ===================== Mapas del PCB (hardware/rev1) =====================
#
# Los dos SVG de hardware/rev1 son derivados, igual que el BOM o el netlist:
# salen de los Gerbers, del archivo de test que viene adentro del ZIP y del
# netlist. Si se regeneran los Gerbers, se regeneran tambien estos, en el
# mismo commit (ver hardware/README.md).
#
# Los puntos de corte de cortes-rev1.svg estan fijos en el script. Con Gerbers
# nuevos, revisalos antes de regenerar:
#   python tools/mapa_cortes.py hardware/rev1 --analizar

REV1 := hardware/rev1

mapas: $(REV1)/cortes-rev1.svg $(REV1)/senales-rev1.svg

$(REV1)/cortes-rev1.svg: tools/mapa_cortes.py tools/placa.py $(REV1)/sdf1-gerbers.zip
	$(PYTHON) tools/mapa_cortes.py $(REV1) $@

$(REV1)/senales-rev1.svg: tools/mapa_senales.py tools/placa.py $(REV1)/sdf1-gerbers.zip $(REV1)/sdf1.net
	$(PYTHON) tools/mapa_senales.py $(REV1) $@
