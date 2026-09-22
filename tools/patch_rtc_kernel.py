#!/usr/bin/env python3
"""
Parchea la DiskROM para redirigir las 4 entradas de reloj del kernel
(CHKCLK/$GETTI/$SETDA/$SETTI) hacia las rutinas propias del driver
(MYCHKCLK/MYGETTI/MYSETDA/MYSETTI, en diskrom/DSKDRV.MAC), que hablan
con el DS1307 por el protocolo normal de comando+datos.

Camino A de reloj-cuatro-caminos.md: el camino B (emular el RP-5C01 en los
puertos B4h/B5h) fallaba por timing en el bucle de deteccion de CHKCLK, el
acceso mas apretado de toda la ROM (ver reloj-capa-rp5c01-en-el-kit.md y el
comentario de MSX_REENABLE_DELAY_US en defs.h). Este camino no toca ningun
puerto crudo: cada entrada se reemplaza por un JP de 3 bytes a una rutina
que usa WriteCommand/WriteByte/ReadByte, el mismo mecanismo ya probado en
hardware para el resto del protocolo (CMD_SDFMOUNT, CMD_DEVIN, etc).

Las 4 direcciones del kernel (CHKCLK, $SETDA, $SETTI, $GETTI) y las 4 de mis
rutinas (MYCHKCLK, MYGETTI, MYSETDA, MYSETTI, truncadas a 6 caracteres por
el linker) se leen de out/msxdos.sym: se mueven con cada link, nunca se
hardcodean (mismo criterio que la entrada DEVICE de tools/mkrom.py).

Uso:
    python tools/patch_rtc_kernel.py out/sdf1.rom out/msxdos.sym
"""

import re
import sys

# Nombre del kernel -> nombre de mi rutina (tal como quedan truncados a 6
# caracteres en el .sym: N80/LK80 truncan igual que M80 para el listado).
ENTRIES = [
    ("CHKCLK", "MYCHKC"),
    ("$GETTI", "MYGETT"),
    ("$SETDA", "MYSETD"),
    ("$SETTI", "MYSETT"),
]

JP_OPCODE = 0xC3


def read_symbols(path):
    """Devuelve {nombre: direccion} de una lista de simbolos formato L80."""
    symbols = {}
    with open(path, "r") as f:
        for value, name in re.findall(r"([0-9A-Fa-f]{4}) (\S+)", f.read()):
            symbols[name.upper()] = int(value, 16)
    return symbols


def patch_rtc_kernel(rom_path, sym_path):
    symbols = read_symbols(sym_path)

    with open(rom_path, "rb") as f:
        data = bytearray(f.read())

    print(f"Parcheando {rom_path} (camino A: JP a rutinas propias)...")
    print()

    for kernel_name, mine_name in ENTRIES:
        # $GETTI/$SETDA/$SETTI llevan el "$" tal cual en el .sym; CHKCLK no.
        addr = symbols.get(kernel_name.upper())
        if addr is None:
            sys.exit(f"patch_rtc_kernel: no encontre {kernel_name} en {sym_path}")

        target = symbols.get(mine_name)
        if target is None:
            sys.exit(f"patch_rtc_kernel: no encontre {mine_name} en {sym_path} "
                      f"(deberia ser {kernel_name} truncado a 6 caracteres)")

        if not (0x4000 <= addr < 0x8000):
            sys.exit(f"patch_rtc_kernel: {kernel_name} esta en 0x{addr:04X}, "
                      "fuera de la pagina 1")
        if not (0x4000 <= target < 0x8000):
            sys.exit(f"patch_rtc_kernel: {mine_name} esta en 0x{target:04X}, "
                      "fuera de la pagina 1")

        data[addr] = JP_OPCODE
        data[addr + 1] = target & 0xFF
        data[addr + 2] = (target >> 8) & 0xFF

        print(f"  {kernel_name:8s} 0x{addr:04X}  ->  JP 0x{target:04X} ({mine_name})")

    with open(rom_path, "wb") as f:
        f.write(data)

    print()
    print(f"Parche aplicado: 4 entradas redirigidas")
    print(f"Salida: {rom_path}")


if __name__ == "__main__":
    if len(sys.argv) != 3:
        sys.exit(__doc__.strip())

    patch_rtc_kernel(sys.argv[1], sys.argv[2])
