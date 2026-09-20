#!/usr/bin/env python3
"""Validate the production MAC table, and prove the parser still reads it.

Two regexes here on purpose, doing opposite jobs. cube_table.ROW matches a
row by its exact shape and is what every tool depends on. LOOSE matches
anything row-shaped at all. When they disagree, the table's format has moved
out from under the parser -- which is precisely the failure that broke
flashing and the admin page, silently, because the only thing looking was a
parser that had been updated along with the table.
"""
import re
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
import cube_table

ALLOWED_MIN, ALLOWED_MAX = 21, 199
EXPECTED_ROWS = 21

LOOSE = re.compile(r'\{\s*"[^"]*"[^}]*\}')


def main():
    text = cube_table.SOURCE.read_text()
    loose_rows = LOOSE.findall("\n".join(cube_table.production_rows(text)))
    errors = []

    if len(loose_rows) != EXPECTED_ROWS:
        errors.append(f"expected {EXPECTED_ROWS} rows, found {len(loose_rows)}")

    try:
        boards = cube_table.boards()
    except SystemExit as exc:
        print("MAC table INVALID:")
        print(f"  - {exc}")
        return 1

    if len(boards) != len(loose_rows):
        errors.append(
            f"{len(loose_rows) - len(boards)} row(s) are row-shaped but do not "
            f"parse: cube_table.ROW and the table disagree about the format"
        )

    macs, octets = {}, {}
    for board in boards:
        mac, octet = board["mac"], board["ip_octet"]
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

    if cube_table.ARTIFACT.exists():
        if cube_table.ARTIFACT.read_text() != cube_table.rendered():
            errors.append(
                f"{cube_table.ARTIFACT.name} is stale -- run "
                f"`tools/cube_table.py write` and commit the result"
            )
    else:
        errors.append(f"{cube_table.ARTIFACT.name} is missing")

    if errors:
        print("MAC table INVALID:")
        for error in errors:
            print(f"  - {error}")
        return 1
    print(
        f"MAC table OK: {len(boards)} rows, all MACs and octets unique and "
        f"canonical, {cube_table.ARTIFACT.name} current."
    )
    return 0


if __name__ == "__main__":
    sys.exit(main())
