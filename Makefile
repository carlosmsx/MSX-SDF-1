# Compilacion del SDF-1: la DiskROM (Z80) y el firmware (ATmega328P).
#
# Las dos mitades del cartucho se compilan por separado y no dependen una de
# la otra, pero el protocolo entre ambas si: si tocas los codigos de comando,
# tocas los dos lados. Por eso viven en el mismo Makefile.
#
# Salidas, todas en out/ (que esta en .gitignore):
#   out/sdf1.rom                        imagen de 64 KB para la W27C512
#   out/firmware/*.hex                  firmware del ATmega328P
#
# OJO: build/ NO es una carpeta de salida, es el MSX-DOS kit de ASCII. No
# apuntes ningun --output-dir ahi.
#
# Y OJO DE NUEVO: build/ NO VIENE EN ESTE REPO. Es codigo de terceros y no
# esta redistribuido aca. Tenes que poner tu propia copia del MSX-DOS kit en
# build/ antes de "make rom" — los ocho .REL que lista MODULES/MODULES2, mas
# el README del kit. "make check" te avisa si falta.
#
# Por lo mismo out/sdf1.rom NO se publica: contiene el kernel de ASCII
# linkeado con nuestro driver. Se compila localmente, no se distribuye.
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

DRIVER   := diskrom/DSKDRV.MAC
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

REL_KIT  := $(addprefix $(KIT)/,$(addsuffix .REL,$(MODULES)))
REL_KIT2 := $(addprefix $(KIT)/,$(addsuffix .REL,$(MODULES2)))

.PHONY: all rom firmware fuses flash clean check check-rom check-firmware

all: rom firmware

rom: $(OUT)/sdf1.rom

check: check-rom check-firmware

check-rom:
	@test -x $(N80)  || { echo "falta $(N80) — ver el encabezado de este Makefile"; exit 1; }
	@test -x $(LK80) || { echo "falta $(LK80) — ver el encabezado de este Makefile"; exit 1; }
	@test -f $(KIT)/DOSHEAD.REL || { echo "falta el MSX-DOS kit en $(KIT)/"; exit 1; }
	@echo "N80  $$($(N80) --version)"
	@echo "LK80 $$($(LK80) --version)"
	@echo "kit  $(words $(MODULES) $(MODULES2)) modulos en $(KIT)/"

$(OUT):
	@mkdir -p $(OUT)

# --- 1. Ensamblar el driver ---------------------------------------------
# -bt rel: relocalizable formato M80, que es lo que espera LK80.
#
# -l8c: OBLIGATORIO. M80 truncaba los simbolos publicos y externos a 6
# caracteres; N80 no lo hace por defecto. Sin este flag el linkeo falla con
# "can't resolve external symbol reference" en OEMSTA (el kit espera 6
# caracteres, el driver exporta OEMSTATEMENT) y en $SECBU (el driver pide
# $SECBUF, 7 caracteres).
$(OUT)/DSKDRV.REL: $(DRIVER) | $(OUT)
	$(N80) $(DRIVER) $@ -bt rel -l8c

# --- 2. Linkear contra el kit -------------------------------------------
# --code y --data son "link sequence items": aplican al archivo siguiente,
# por eso van antes del primer .REL. --data ademas pone el linker en modo
# "codigo y datos separados", que es el equivalente del /d: de L80.
$(OUT)/msxdos.hex: $(OUT)/DSKDRV.REL
	$(LK80) --code $(CODE_ORG) --data $(DATA_ORG) \
	        $(REL_KIT) $(OUT)/DSKDRV.REL $(REL_KIT2) \
	        --output-format hex --output-file $@

# --- 3. Armar la imagen de EEPROM ---------------------------------------
# Ubica el codigo, rellena los 64 KB con FF y verifica la firma "AB".
$(OUT)/sdf1.rom: $(OUT)/msxdos.hex tools/mkrom.py
	$(PYTHON) tools/mkrom.py $< $@

clean:
	rm -rf $(OUT)

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

# cygpath traduce las variables de entorno de Windows a un formato que
# entienden tanto make como los .exe. Si compilas en Linux/macOS, pasa
# ARDUINO_DATA y ARDUINO_USER a mano.
WINHOME  := $(shell cygpath -m "$$USERPROFILE" 2>/dev/null || echo "$$HOME")
WINLOCAL := $(shell cygpath -m "$$LOCALAPPDATA" 2>/dev/null || echo "$$HOME/.local/share")

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

# Programador ISP. usbasp es el barato y el que asume BODGES.md paso 5.
PROGRAMMER ?= usbasp

# Las tres cosas que necesita el .exe para encontrar cores y librerias.
ACLI = ARDUINO_DIRECTORIES_DATA="$(ARDUINO_DATA)" \
       ARDUINO_DIRECTORIES_USER="$(ARDUINO_USER)" \
       "$(ARDUINO_CLI)"

firmware: $(FW_HEX)

$(FW_HEX): $(SKETCH)/$(SKETCH).ino $(SKETCH)/defs.h
	@mkdir -p $(FW_OUT)
	$(ACLI) compile -b "$(FQBN)" --output-dir $(FW_OUT) ./$(SKETCH)

check-firmware:
	@test -x "$(ARDUINO_CLI)" || { echo "falta arduino-cli en '$(ARDUINO_CLI)'"; exit 1; }
	@test -d "$(ARDUINO_DATA)/packages/MiniCore" || { echo "falta MiniCore en $(ARDUINO_DATA)"; exit 1; }
	@test -d "$(ARDUINO_USER)/libraries" || { echo "no encuentro el sketchbook (ARDUINO_USER)"; exit 1; }
	@echo "cli   $$($(ACLI) version)"
	@echo "data  $(ARDUINO_DATA)"
	@echo "user  $(ARDUINO_USER)"
	@echo "fqbn  $(FQBN)"

# ---- Grabado por ICSP -------------------------------------------------
#
# Requiere el cable de RESET del paso 5 de hardware/rev1/BODGES.md.
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

flash: $(FW_HEX)
	$(ACLI) upload -b "$(FQBN)" -P $(PROGRAMMER) --input-dir $(FW_OUT) ./$(SKETCH)
