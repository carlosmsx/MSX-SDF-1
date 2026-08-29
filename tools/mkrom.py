#!/usr/bin/env python3
"""
Arma la imagen final de EEPROM del cartucho SDF-1 a partir del Intel HEX
que produce L80.

La DiskROM vive en la pagina 1 del slot (0x4000-0x7FFF). El chip es una
W27C512 de 64 KB con A0-A15 completos y CE=/SLTSL, asi que responde en
todo el espacio de direcciones del slot: lo que aparece en cada pagina lo
decide unicamente como se grabe el binario.

Este script:
  - vuelca el HEX en una imagen de 64 KB rellena con 0xFF
  - verifica que la firma "AB" quede en 0x4000 y en ningun otro lado
  - avisa cuanto espacio libre queda en la pagina 1

Uso:
    python tools/mkrom.py out/msxdos.hex out/sdf1.rom
"""

import sys

ROM_SIZE = 0x10000        # W27C512: 64 KB
PAGE1 = 0x4000            # donde el BIOS busca una DiskROM
PAGE_SIZE = 0x4000
SIGNATURE = b"AB"         # el BIOS busca 41h 42h exactos


def read_ihex(path):
    """Devuelve {direccion: byte} a partir de un archivo Intel HEX."""
    mem = {}
    base = 0
    with open(path, "r") as f:
        for lineno, line in enumerate(f, 1):
            line = line.strip()
            if not line or not line.startswith(":"):
                continue
            raw = bytes.fromhex(line[1:])
            count, addr_hi, addr_lo, rectype = raw[0], raw[1], raw[2], raw[3]
            addr = (addr_hi << 8) | addr_lo
            data = raw[4:4 + count]

            if (sum(raw) & 0xFF) != 0:
                sys.exit("mkrom: checksum invalido en la linea %d" % lineno)

            if rectype == 0x00:                       # datos
                for i, b in enumerate(data):
                    mem[base + addr + i] = b
            elif rectype == 0x01:                     # fin de archivo
                break
            elif rectype == 0x02:                     # segmento extendido
                base = ((data[0] << 8) | data[1]) << 4
            elif rectype == 0x04:                     # direccion lineal
                base = ((data[0] << 8) | data[1]) << 16
            # los demas tipos no aplican a un binario Z80
    return mem


def main():
    if len(sys.argv) != 3:
        sys.exit(__doc__.strip())

    hex_path, out_path = sys.argv[1], sys.argv[2]
    mem = read_ihex(hex_path)
    if not mem:
        sys.exit("mkrom: %s no contiene datos" % hex_path)

    # El HEX que produce LK80 abarca desde el codigo en 0x4000 hasta el
    # segmento de datos en la RAM del sistema (0xF237), rellenando el hueco
    # con ceros. A la EEPROM solo va la pagina 1: el resto son direcciones
    # de RAM donde viven las variables, no contenido de ROM.
    page1 = {a: b for a, b in mem.items() if PAGE1 <= a < PAGE1 + PAGE_SIZE}
    if not page1:
        sys.exit("mkrom: no hay nada en la pagina 1 (0x%04X-0x%04X)"
                 % (PAGE1, PAGE1 + PAGE_SIZE - 1))

    outside = sorted(a for a, b in mem.items()
                     if not (PAGE1 <= a < PAGE1 + PAGE_SIZE) and b != 0)

    rom = bytearray(b"\xFF" * ROM_SIZE)
    for addr, b in page1.items():
        rom[addr] = b

    # Fin del codigo: ultimo byte con contenido. El relleno de LK80 es 0x00.
    hi = max(a for a, b in page1.items() if b != 0)
    lo = PAGE1

    # --- Verificaciones que atrapan el error mas caro ------------------
    # Sin la firma correcta el BIOS saltea el slot entero: la placa puede
    # estar perfecta y el MSX no hace absolutamente nada.
    found = rom[PAGE1:PAGE1 + 2]
    if found != SIGNATURE:
        sys.exit(
            "mkrom: firma incorrecta en 0x%04X.\n"
            "        esperado %s, encontrado %s\n"
            "        El BIOS busca 41h 42h exactos; con otra cosa saltea\n"
            "        el slot y no llama a nada." % (
                PAGE1, SIGNATURE.hex(" "), bytes(found).hex(" ")))

    # La firma espejada en otra pagina hace que el BIOS llame a INIT dos veces.
    for page in range(0, ROM_SIZE, PAGE_SIZE):
        if page != PAGE1 and rom[page:page + 2] == SIGNATURE:
            sys.exit(
                "mkrom: la firma AB tambien aparece en 0x%04X.\n"
                "        El BIOS escanea las paginas 1 y 2: si la encuentra\n"
                "        dos veces, llama a la rutina de init dos veces." % page)

    with open(out_path, "wb") as f:
        f.write(rom)

    print("mkrom: %s" % out_path)
    print("       codigo   0x%04X - 0x%04X  (%d bytes)" % (lo, hi, hi - lo + 1))
    print("       libre    %d bytes en la pagina 1" % (PAGE1 + PAGE_SIZE - hi - 1))
    print("       firma    AB en 0x4000, unica")
    if outside:
        print("       datos    %d bytes en 0x%04X-0x%04X, fuera de la ROM "
              "(DSEG en RAM)" % (len(outside), outside[0], outside[-1]))


if __name__ == "__main__":
    main()
