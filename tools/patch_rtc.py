#!/usr/bin/env python3
"""
Parchea la DiskROM para redirigir accesos a B4h/B5h (RP-5C01)
hacia los puertos 0/1 del firmware, donde se emula con DS1307.

Reemplaza:
  OUT (0B4h),A  →  OUT (0x01),A  (puerto de comando)
  OUT (0B5h),A  →  OUT (0x00),A  (puerto de datos)
  IN A,(0B5h)   →  IN A,(0x00)   (puerto de datos)

Uso:
    python tools/patch_rtc.py out/sdf1.rom
"""

import sys

def patch_rtc(rom_path):
    """Parchea el ROM con el mapeo de puertos para DS1307."""

    with open(rom_path, "rb") as f:
        data = bytearray(f.read())

    # Rango donde estan los accesos (segun las notas)
    start = 0x40BA
    end = 0x41C0

    # Patrones a reemplazar
    patches = [
        (b'\xD3\xB4', b'\xD3\x01', 'OUT (0B4h),A → OUT (0x01),A'),  # Puerto de comando
        (b'\xD3\xB5', b'\xD3\x00', 'OUT (0B5h),A → OUT (0x00),A'),  # Puerto de datos (escritura)
        (b'\xDB\xB5', b'\xDB\x00', 'IN A,(0B5h) → IN A,(0x00)'),    # Puerto de datos (lectura)
    ]

    total_patched = 0

    for pattern, replacement, desc in patches:
        count = 0
        i = start
        while i < end - len(pattern) + 1:
            if data[i:i+len(pattern)] == pattern:
                data[i:i+len(pattern)] = replacement
                print(f"  {desc} en 0x{i:04X}")
                count += 1
                i += len(replacement)
            else:
                i += 1
        print(f"  {count} ocurrencias de {desc}")
        total_patched += count

    # Escribir el ROM parcheado
    with open(rom_path, "wb") as f:
        f.write(data)

    print(f"\nParche aplicado: {total_patched} accesos a puertos redirigidos")
    print(f"Salida: {rom_path}")

    if total_patched < 25:
        print(f"WARNING: Se esperaban ~27 accesos, se encontraron {total_patched}")
        print("   Verificar que sea el rango correcto en la ROM")
    else:
        print("OK: Parche verificado - todos los accesos a B4h/B5h redirigidos a puertos 0/1")

if __name__ == "__main__":
    if len(sys.argv) != 2:
        sys.exit(__doc__.strip())

    rom_path = sys.argv[1]
    print(f"Parcheando {rom_path} para DS1307...")
    patch_rtc(rom_path)
