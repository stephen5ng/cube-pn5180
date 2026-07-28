#!/usr/bin/env python3
"""Validate uniqueness and canonical form of the production MAC table."""
import re
import sys
from pathlib import Path

SRC = Path(__file__).resolve().parent.parent / "src" / "cube_utilities.cpp"
ALLOWED_MIN, ALLOWED_MAX = 21, 199
EXPECTED_ROWS = 18

LOOSE = re.compile(r'\{\s*"[^"]*"[^}]*\}')
STRICT = re.compile(
    r'^\{\s*"((?:[0-9A-F]{2}:){5}[0-9A-F]{2})"\s*,\s*\d+\s*,\s*\w+\s*,\s*(\d+)\s*\}$'
)


def production_block(text):
    return text.split("#else", 1)[1].split("#endif", 1)[0]


def main():
    rows = LOOSE.findall(production_block(SRC.read_text()))
    errors = []
    if len(rows) != EXPECTED_ROWS:
        errors.append(f"expected {EXPECTED_ROWS} rows, found {len(rows)}")
    macs, octets = {}, {}
    for raw in rows:
        match = STRICT.match(raw.strip())
        if not match:
            errors.append(f"malformed/non-canonical row: {raw.strip()}")
            continue
        mac, octet = match.group(1), int(match.group(2))
        if mac in macs:
            errors.append(f"duplicate MAC {mac}")
        macs[mac] = True
        if octet in octets:
            errors.append(f"duplicate ip_octet {octet} ({octets[octet]} and {mac})")
        octets[octet] = mac
        if not ALLOWED_MIN <= octet <= ALLOWED_MAX:
            errors.append(
                f"ip_octet {octet} for {mac} outside {ALLOWED_MIN}-{ALLOWED_MAX}"
            )
    if errors:
        print("MAC table INVALID:")
        for error in errors:
            print(f"  - {error}")
        return 1
    print(
        f"MAC table OK: {len(rows)} rows, all MACs and octets unique and canonical."
    )
    return 0


if __name__ == "__main__":
    sys.exit(main())
