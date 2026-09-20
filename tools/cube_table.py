#!/usr/bin/env python3
"""The one place that reads the compiled MAC table.

Five tools across two repositories used to parse src/cube_utilities.cpp with
their own regex. When the cube_id column was removed, three of them silently
matched nothing: flashing could not resolve an address, the wired flasher
died after writing the board, and the admin page lost a cube. Only the
parser with a test was updated, because it was the only one anyone knew to
look at.

So: one parser here, and config/cube_table.json beside it as the artifact
everything else reads. CI regenerates the file and fails on a difference, so
a change to the table's shape breaks the build rather than the fleet.

  cube_table.py json            the document, on stdout
  cube_table.py write           regenerate config/cube_table.json
  cube_table.py octets          "<MAC> <octet>" per board
  cube_table.py macs            colon-free MACs, one per line
  cube_table.py octet <MAC>     that board's octet, or exit 1
"""
import json
import re
import sys
from pathlib import Path

REPO = Path(__file__).resolve().parent.parent
SOURCE = REPO / "src" / "cube_utilities.cpp"
ARTIFACT = REPO / "config" / "cube_table.json"

# {"CC:DB:A7:9F:C2:84", RGB_ORDER_BGR, 21},
#
# Three fields: MAC, panel wiring, static-IP octet. Counting commas is what
# broke last time, so the row is matched by shape -- a quoted MAC, a bare
# identifier, a number -- and a fourth field would fail to match rather than
# shifting the octet silently.
ROW = re.compile(
    r'^\s*\{\s*"((?:[0-9A-F]{2}:){5}[0-9A-F]{2})"\s*,'
    r'\s*RGB_ORDER_(BGR|RGB)\s*,'
    r'\s*(\d+)\s*\}\s*,'
)


def production_rows(text: str) -> list[str]:
    """Only the #else block: the file also carries a test table."""
    return text.split("#else", 1)[1].split("#endif", 1)[0].splitlines()


def boards(source: Path = SOURCE) -> list[dict[str, object]]:
    found = []
    for line in production_rows(source.read_text()):
        match = ROW.match(line)
        if match:
            found.append(
                {
                    "mac": match.group(1),
                    "rgb_order": match.group(2),
                    "ip_octet": int(match.group(3)),
                }
            )
    if not found:
        raise SystemExit(
            f"{source} yielded no boards. The row format has changed and this "
            f"parser no longer matches it -- fix ROW here rather than adding a "
            f"second parser somewhere else."
        )
    return found


def document(source: Path = SOURCE) -> dict[str, object]:
    return {
        "_generated": "by tools/cube_table.py from src/cube_utilities.cpp -- do not edit",
        "protocol": 1,
        "boards": boards(source),
    }


def rendered(source: Path = SOURCE) -> str:
    return json.dumps(document(source), indent=2) + "\n"


def main(argv: list[str]) -> int:
    command = argv[1] if len(argv) > 1 else "json"
    if command == "json":
        sys.stdout.write(rendered())
    elif command == "write":
        ARTIFACT.write_text(rendered())
        print(f"wrote {ARTIFACT.relative_to(REPO)}")
    elif command == "octets":
        for board in boards():
            print(f"{board['mac']} {board['ip_octet']}")
    elif command == "macs":
        for board in boards():
            print(board["mac"].replace(":", ""))
    elif command == "octet":
        wanted = argv[2].upper()
        for board in boards():
            if board["mac"].upper() == wanted:
                print(board["ip_octet"])
                return 0
        print(f"{wanted} is not in the compiled table", file=sys.stderr)
        return 1
    else:
        print(__doc__, file=sys.stderr)
        return 2
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
