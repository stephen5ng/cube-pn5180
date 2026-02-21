#!/usr/bin/env python3
import socket
import sys

if len(sys.argv) < 3:
    print("Usage: test_cube_diag.py <cube_ip> <cube_id>")
    sys.exit(1)

cube_ip = sys.argv[1]
cube_id = sys.argv[2]

try:
    s = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    s.settimeout(2)
    s.sendto(b'diag', (cube_ip, 54321))
    resp = s.recv(512).decode()
    s.close()

    # Parse response
    parts = {}
    for item in resp.split('|'):
        if '=' in item:
            k, v = item.split('=', 1)
            parts[k] = v
        elif item:
            parts['id'] = item

    fw = parts.get('fw', '?')
    print("="*60)
    print(f"Cube {cube_id} (fw={fw}):")
    print("="*60)

    # Display in human-readable format
    loop_us = int(parts.get('loop', 0))
    nfc_us = int(parts.get('nfc', 0))
    nfc_max = int(parts.get('nfc_max', 0))
    letter_avg = int(parts.get('letter_avg', 0))
    letter_max = int(parts.get('letter_max', 0))
    rssi = int(parts.get('rssi', 0))

    print(f"  Loop time (avg):     {loop_us:>10} us")
    print(f"  MQTT (avg):          {int(parts.get('mqtt', 0)):>10} us")
    print(f"  Display (avg):       {int(parts.get('disp', 0)):>10} us")
    print(f"  UDP (avg):           {int(parts.get('udp', 0)):>10} us")
    print(f"  NFC (avg):           {nfc_us:>10} us")
    print(f"  NFC (max):           {nfc_max:>10} us")
    print(f"  Letter interval avg: {letter_avg:>10} ms")
    print(f"  Letter interval max: {letter_max:>10} ms")
    print(f"  RSSI:                {rssi:>10} dBm")
    print("="*60)

    # Simple pass/fail
    loop_ok = loop_us < 5000
    nfc_ok = nfc_us < 5000
    letter_ok = letter_avg < 600 or letter_avg == 0

    if loop_ok and nfc_ok and letter_ok:
        print("\n✓ Cube appears HEALTHY")
    else:
        print("\n✗ Cube has issues:")
        if not loop_ok:
            print(f"  - Loop time is SLOW ({loop_us}us, threshold 5000us)")
        if not nfc_ok:
            print(f"  - NFC is SLOW ({nfc_us}us, threshold 5000us)")
            print(f"    → Cube may need NFC hardware swap (BUSY pin stuck?)")
        if not letter_ok and letter_avg > 0:
            print(f"  - Letter interval is LONG ({letter_avg}ms, threshold 600ms)")
            print(f"    → Check if random_letters.sh is running on host")
    print()

except socket.timeout:
    print(f"\n✗ Cube {cube_ip} did not respond (timeout)")
    print(f"   Check: is cube powered on? Connected to network?")
except Exception as e:
    print(f"\n✗ Error: {e}")
