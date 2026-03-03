#!/usr/bin/env python3
"""Check for firmware/board version mismatches"""

import socket
import subprocess
import sys

UDP_PORT = 54321
TIMEOUT = 1.0

# Expected board versions for each cube
CUBE_BOARDS = {
    1: "v1",
    2: "v6",
    3: "v1",
    4: "v6",
    5: "v6",
    6: "v1",
}

def get_firmware_version(ip):
    """Get firmware version via UDP diag"""
    try:
        sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
        sock.settimeout(TIMEOUT)
        sock.sendto(b'diag', (ip, UDP_PORT))
        data, _ = sock.recvfrom(1024)
        sock.close()
        diag = data.decode()
        # Parse fw=v1 or fw=v6
        for part in diag.split('|'):
            if part.startswith('fw='):
                return part.split('=')[1]
    except:
        pass
    return None

def check_cubes():
    """Check all cubes for firmware mismatches"""
    print("Checking firmware/board version mismatches...")
    print("=" * 50)

    mismatches = []
    errors = []

    for cube_id in range(1, 7):
        ip = f"192.168.8.{cube_id + 20}"
        expected_board = CUBE_BOARDS.get(cube_id, "?")

        fw = get_firmware_version(ip)

        if fw is None:
            errors.append((cube_id, ip, "No response"))
        elif fw != expected_board:
            mismatches.append((cube_id, ip, expected_board, fw))
        else:
            print(f"✅ Cube {cube_id}: {fw} (matches {expected_board} board)")

    print()
    if mismatches:
        print("⚠️  MISMATCHES FOUND:")
        for cube_id, ip, expected, actual in mismatches:
            print(f"   Cube {cube_id} ({ip}): Board={expected}, Firmware={actual}")
        print()
        print("Fix with:")
        for cube_id, ip, expected, actual in mismatches:
            print(f"   ./tools/flash_cubes.sh {cube_id} {expected}")
        return 1

    if errors:
        print("❌ ERRORS (no response):")
        for cube_id, ip, msg in errors:
            print(f"   Cube {cube_id} ({ip}): {msg}")
        return 1

    print("✅ All cubes have matching firmware!")
    return 0

if __name__ == "__main__":
    sys.exit(check_cubes())
