#!/bin/bash
# Point a cube's table entry at a replacement ESP32, then flash it over USB.
#
# Usage: ./replace_chip.sh <cube_number> [port]
#
# The flash itself is flash_cube_wired.sh's job; this script only gets the
# tables right first.

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_DIR="$(dirname "$SCRIPT_DIR")"
MAC_FILE="$PROJECT_DIR/src/cube_utilities.cpp"
BOARD_FILE="$PROJECT_DIR/config/cube_board_versions.txt"
PIO_PYTHON="${PIO_PYTHON:-$HOME/.platformio/penv/bin/python}"
MQTT_HOST="${MQTT_SERVER:-192.168.8.247}"

die() { echo "ERROR: $*" >&2; exit 1; }

CUBE_NUM="${1:-}"
[ -n "$CUBE_NUM" ] || die "usage: replace_chip.sh <cube_number> [port]"
shift
PORT="${1:-}"

[[ "$CUBE_NUM" =~ ^[0-9]+$ ]] || die "cube number must be numeric, got '$CUBE_NUM'"

# --- Find the board holding the slot ---------------------------------------------
# The table compiles in no slot, so it cannot answer "which board is cube N".
# The retained assignments can, and they are the same records the firmware
# obeys, so the answer here matches what the cube itself believes.
# Every owner, not the first: two records naming one slot is the collision this
# is used to recover from, and picking either would rewrite a board's permanent
# row -- its MAC, its address, its panel wiring -- for the wrong hardware.
OWNERS=$(mosquitto_sub -h "$MQTT_HOST" -t 'cube/assign/+' -v -W 2 2>/dev/null \
    | sed -nE "s#^cube/assign/([0-9A-Fa-f]{12})[[:space:]].*\"slot\"[[:space:]]*:[[:space:]]*${CUBE_NUM}[[:space:]]*[,}].*#\1#p" \
    | tr 'a-f' 'A-F' | sort -u || true)
OWNER_COUNT=$(printf '%s\n' "$OWNERS" | grep -c . || true)
[ "$OWNER_COUNT" -ne 0 ] \
    || die "no board holds slot $CUBE_NUM. The roster assigns slots; check the admin page."
[ "$OWNER_COUNT" -eq 1 ] \
    || die "slot $CUBE_NUM is claimed by $OWNER_COUNT boards ($(echo $OWNERS)). Resolve the
duplicate assignment before replacing a chip: rewriting a row now could repoint
the wrong board."
OLD_MAC=$(printf '%s' "$OWNERS" | sed -E 's/(..)/\1:/g; s/:$//')
grep -q "\"$OLD_MAC\"" "$MAC_FILE" \
    || die "slot $CUBE_NUM is held by $OLD_MAC, which is not in $MAC_FILE."
echo "Current entry: cube $CUBE_NUM, MAC $OLD_MAC"

# --- Read the new chip's MAC ----------------------------------------------------
if [ -n "$PORT" ]; then
    [ -c "$PORT" ] || die "not a serial device: $PORT"
else
    PORT=$(ls /dev/cu.usbserial-* /dev/cu.wchusbserial* /dev/cu.SLAB_USBtoUART* 2>/dev/null | head -1 || true)
    [ -n "$PORT" ] || die "no serial port found. Plug the chip in or pass one: $0 $CUBE_NUM <port>"
fi
echo "Reading MAC from $PORT..."
NEW_MAC=$("$PIO_PYTHON" -m esptool --port "$PORT" read-mac 2>/dev/null \
    | grep -i '^MAC:' | head -1 | awk '{print $2}' | tr 'a-f' 'A-F')
[ -n "$NEW_MAC" ] || die "could not read MAC from $PORT. Check the USB connection."
echo "New MAC:       $NEW_MAC"

if [ "$NEW_MAC" = "$OLD_MAC" ]; then
    echo "Table already points cube $CUBE_NUM at $NEW_MAC; nothing to change."
else
    ! grep -q "\"$NEW_MAC\"" "$MAC_FILE" || die "$NEW_MAC is already in the table; refusing to duplicate it."

    # --- Edit both tables -------------------------------------------------------
    sed -i '' "s|{\"$OLD_MAC\"\(,[[:space:]]*$CUBE_NUM[[:space:]]*,\)|{\"$NEW_MAC\"\1|" "$MAC_FILE"
    # A sed that matches nothing still exits 0, so the edit is read back rather
    # than assumed.
    grep -q "\"$NEW_MAC\"" "$MAC_FILE" || die "MAC table edit did not apply — $MAC_FILE is unchanged."
    echo "Updated entry: $(sed -n "/\"$NEW_MAC\"/p" "$MAC_FILE")"

    # The board version travels with the PCB, not the chip, so the new MAC
    # inherits whatever the replaced one was registered as.
    # Matched case-sensitively, as the rewrite below is: a read that finds a
    # lowercase line the rewrite then misses would abort with the MAC table
    # already edited and this file not.
    OLD_VERSION=$(grep "^$OLD_MAC=" "$BOARD_FILE" | head -1 | cut -d= -f2 | tr -d '[:space:]')
    if [ -n "$OLD_VERSION" ]; then
        sed -i '' "s|^$OLD_MAC=.*|$NEW_MAC=$OLD_VERSION|" "$BOARD_FILE"
        grep -q "^$NEW_MAC=" "$BOARD_FILE" || die "board version edit did not apply to $BOARD_FILE."
        echo "Board version: $NEW_MAC=$OLD_VERSION (inherited from $OLD_MAC)"
    else
        echo "WARNING: $OLD_MAC had no entry in $BOARD_FILE, so none was inherited."
        echo "         Add '$NEW_MAC=<v1|v6|v6_with_hall>' there before flashing."
    fi
fi

# --- Validate -------------------------------------------------------------------
echo ""
python3 "$SCRIPT_DIR/validate_mac_table.py" || die "MAC table validation failed; fix before flashing."

echo ""
echo "Tables updated. Flash with:"
echo "  $SCRIPT_DIR/flash_cube_wired.sh $PORT"
