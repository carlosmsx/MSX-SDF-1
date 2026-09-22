#!/usr/bin/env python3
"""
Genera un 'fingerprint' de la ROM parchada: bytes clave que verifican el parche.
Útil para guardar junto a la ROM compilada y comparar luego si es la versión correcta.

Uso:
    python tools/fingerprint_rtc.py out/sdf1.rom
"""

import sys
import hashlib

def fingerprint_rtc(rom_path):
    """Genera un fingerprint de los bytes parchados."""

    with open(rom_path, "rb") as f:
        data = f.read()

    # Ubicaciones clave del parche (primero y último de cada tipo)
    key_addresses = [
        0x40BA,  # Primer OUT (0x01),A
        0x41B8,  # Último OUT (0x01),A
        0x40BE,  # Primer OUT (0x00),A
        0x4156,  # Último OUT (0x00),A
        0x40C5,  # Primer IN A,(0x00)
        0x41BA,  # Último IN A,(0x00)
    ]

    print("Fingerprint DS1307 - Bytes de verificación")
    print("=" * 50)
    print(f"ROM: {rom_path}")
    print()

    # Calcular hash de los bytes parchados
    patch_bytes = bytearray()
    for addr in key_addresses:
        patch_bytes.extend(data[addr:addr+2])

    sha256 = hashlib.sha256(patch_bytes).hexdigest()
    md5 = hashlib.md5(patch_bytes).hexdigest()

    print("Bytes de verificación (primero y último de cada patrón):")
    print()

    patterns = [
        (0x40BA, "Primer OUT (0x01),A"),
        (0x41B8, "Último OUT (0x01),A"),
        (0x40BE, "Primer OUT (0x00),A"),
        (0x4156, "Último OUT (0x00),A"),
        (0x40C5, "Primer IN A,(0x00)"),
        (0x41BA, "Último IN A,(0x00)"),
    ]

    for addr, desc in patterns:
        byte_pair = data[addr:addr+2].hex().upper()
        print(f"  0x{addr:04X}: {byte_pair}  ({desc})")

    print()
    print("Checksums:")
    print(f"  SHA256: {sha256}")
    print(f"  MD5:    {md5}")
    print()

    # Guardar en un archivo
    fp_file = rom_path.replace('.rom', '.fingerprint')
    with open(fp_file, 'w') as f:
        f.write("DS1307 RTC Patch Fingerprint\n")
        f.write("=" * 50 + "\n\n")
        f.write(f"ROM: {rom_path}\n\n")
        f.write("Verification bytes:\n")
        for addr, desc in patterns:
            byte_pair = data[addr:addr+2].hex().upper()
            f.write(f"  0x{addr:04X}: {byte_pair}  ({desc})\n")
        f.write(f"\nSHA256: {sha256}\n")
        f.write(f"MD5:    {md5}\n")

    print(f"Fingerprint guardado en: {fp_file}")
    return 0

if __name__ == "__main__":
    if len(sys.argv) != 2:
        sys.exit(__doc__.strip())

    rom_path = sys.argv[1]
    sys.exit(fingerprint_rtc(rom_path))
