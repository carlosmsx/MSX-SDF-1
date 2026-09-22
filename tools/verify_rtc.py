#!/usr/bin/env python3
"""
Verifica que la ROM grabada tenga el parche DS1307 aplicado.
Busca los patrones reemplazados en el rango 0x40BA-0x41BA.

Uso:
    python tools/verify_rtc.py out/sdf1.rom
"""

import sys

def verify_rtc_patch(rom_path):
    """Verifica que el ROM tenga los patrones parchados."""

    with open(rom_path, "rb") as f:
        data = f.read()

    # Patrones parcheados que debemos encontrar
    expected_patches = [
        (b'\xD3\x01', 'OUT (0x01),A', 12),  # Puerto comando (antes 0xB4)
        (b'\xD3\x00', 'OUT (0x00),A', 10),  # Puerto datos (antes 0xB5)
        (b'\xDB\x00', 'IN A,(0x00)', 5),    # Lectura datos (antes 0xB5)
    ]

    # Patrones antiguos que NO debemos encontrar
    forbidden_patches = [
        (b'\xD3\xB4', 'OUT (0B4h),A', 'Comando sin parche'),
        (b'\xD3\xB5', 'OUT (0B5h),A (escritura)', 'Datos sin parche'),
        (b'\xDB\xB5', 'IN A,(0B5h)', 'Lectura sin parche'),
    ]

    rango_start = 0x40BA
    rango_end = 0x41C0

    print(f"Verificando {rom_path}...")
    print(f"Rango de verificacion: 0x{rango_start:04X}-0x{rango_end:04X}\n")

    all_ok = True

    # Verificar patrones esperados
    print("Patrones parchados (DEBE encontrar):")
    total_expected = 0
    for pattern, desc, expected_count in expected_patches:
        count = 0
        for i in range(rango_start, rango_end - len(pattern) + 1):
            if data[i:i+len(pattern)] == pattern:
                count += 1
        total_expected += expected_count
        status = "✓" if count == expected_count else "✗"
        print(f"  {status} {desc}: encontrados {count}, esperados {expected_count}")
        if count != expected_count:
            all_ok = False

    print(f"\nTotal esperado: {total_expected} accesos")
    if sum(data[i:i+2] == b'\xD3\x01' or data[i:i+2] == b'\xD3\x00' or
           data[i:i+2] == b'\xDB\x00' for i in range(rango_start, rango_end-1)) == total_expected:
        print("✓ Total coincide")
    else:
        print("✗ Total no coincide")
        all_ok = False

    # Verificar que NO haya patrones antiguos
    print("\nPatrones antiguos (NO debe encontrar):")
    for pattern, desc, note in forbidden_patches:
        count = 0
        first_addr = None
        for i in range(rango_start, rango_end - len(pattern) + 1):
            if data[i:i+len(pattern)] == pattern:
                if first_addr is None:
                    first_addr = i
                count += 1
        if count == 0:
            print(f"  ✓ {desc}: no encontrado (correcto)")
        else:
            print(f"  ✗ {desc}: encontrado {count} veces en 0x{first_addr:04X} ({note})")
            all_ok = False

    print()
    if all_ok:
        print("✓ ROM VERIFICADA: el parche DS1307 está correctamente aplicado")
        return 0
    else:
        print("✗ ROM NO VERIFICADA: el parche no está correctamente aplicado")
        print("  Probablemente sea la ROM vieja sin parche")
        return 1

if __name__ == "__main__":
    if len(sys.argv) != 2:
        sys.exit(__doc__.strip())

    rom_path = sys.argv[1]
    sys.exit(verify_rtc_patch(rom_path))
