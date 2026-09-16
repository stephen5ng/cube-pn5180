#!/bin/bash
# Flash firmware to a cube plugged in over USB (wired update).
#
# Usage: ./flash_cube_wired.sh [serial_port]
#
# The wired counterpart to flash_cubes.sh: instead of resolving a cube over
# the network, it identifies the board on the serial port by MAC, looks that
# MAC up in config/cube_board_versions.txt, and flashes the matching
# environment. An unknown MAC aborts — flashing an unmatched board is how the
# wrong firmware gets deployed silently.
#
# Steps, in order (skipping any is a silent-failure risk):
#   1. Detect the serial port (or take it as $1) and read the board's MAC
#   2. Look up the MAC -> board version -> platformio environment
#   3. Look up the cube id the firmware will compile in for that MAC
#   4. Run the native test suite (mandatory before any flash)
#   5. Compile the target environment
#   6. Flash over USB
#   7. Verify the boot on serial (cube id must match, no FATAL)

set -euo pipefail

FW_DIR="$(cd "$(dirname "$0")/.." && pwd)"
CUBE_VERSIONS_FILE="$FW_DIR/config/cube_board_versions.txt"
MAC_FILE="$FW_DIR/src/cube_utilities.cpp"
PIO="${PIO:-$HOME/.platformio/penv/bin/pio}"
PIO_PYTHON="${PIO_PYTHON:-$HOME/.platformio/penv/bin/python}"
BAUD=115200
BOOT_WINDOW_S="${BOOT_WINDOW_S:-12}"

die() { echo "ERROR: $*" >&2; exit 1; }

# platformio (tests, build, upload) resolves the project from the cwd, so run
# everything from the firmware dir regardless of where the script is invoked.
cd "$FW_DIR"

read_mac_from() {
    "$PIO_PYTHON" -m esptool --port "$1" read-mac 2>/dev/null \
        | grep -i '^MAC:' | head -1 | awk '{print $2}' | tr 'a-f' 'A-F'
}

# --- 1. Serial port and MAC ---------------------------------------------------
# macOS names the same adapter differently per driver family (CP210x shows up as
# cu.SLAB_USBtoUART, CH34x as cu.usbserial-* or cu.wchusbserial*), and one board
# can present more than one node. Reading the MAC off each candidate is what
# tells them apart: same MAC means one board seen twice, so either node will do.
if [ $# -ge 1 ]; then
    CANDIDATES=("$1")
    [ -c "$1" ] || die "not a serial device: $1"
else
    CANDIDATES=()
    for node in /dev/cu.usbserial-* /dev/cu.wchusbserial* /dev/cu.SLAB_USBtoUART*; do
        [ -c "$node" ] && CANDIDATES+=("$node")
    done
    [ ${#CANDIDATES[@]} -gt 0 ] \
        || die "no serial port found. Plug the cube in or pass one: $0 /dev/cu.usbserial-XXX"
fi

PORT=""; MAC=""
for node in "${CANDIDATES[@]}"; do
    node_mac=$(read_mac_from "$node" || true)
    [ -n "$node_mac" ] || continue
    if [ -z "$MAC" ]; then
        PORT="$node"; MAC="$node_mac"
    elif [ "$node_mac" != "$MAC" ]; then
        die "more than one board attached ($MAC on $PORT, $node_mac on $node).
Pass the port explicitly: $0 <port>"
    fi
done
[ -n "$MAC" ] \
    || die "could not read a MAC from: ${CANDIDATES[*]}
Is the board powered and not held open by another program (serial monitor)?"
echo "Serial port: $PORT"
echo "Board MAC:   $MAC"

# --- 2. Version -> environment -------------------------------------------------
VERSION=$(grep -i "^$MAC=" "$CUBE_VERSIONS_FILE" | head -1 | cut -d= -f2 | tr -d '[:space:]')
[ -n "$VERSION" ] || die "MAC $MAC is not in $CUBE_VERSIONS_FILE — refusing to flash an unknown board.
Add it there first, then re-run."
ENV="$VERSION"
echo "Environment: $ENV"

# --- 3. Expected cube id --------------------------------------------------------
# The compiled table is what decides the board's identity, so the boot check in
# step 7 compares against this rather than against whatever the board prints.
EXPECT_CUBE_ID=$(sed -n '/^#else/,/^#endif/p' "$MAC_FILE" \
    | sed -nE "s/^[[:space:]]*\{\"$MAC\"[[:space:]]*,[[:space:]]*([A-Za-z0-9_]+).*/\1/p" | head -1)
[ -n "$EXPECT_CUBE_ID" ] \
    || die "MAC $MAC is in $CUBE_VERSIONS_FILE but not in the compiled table in $MAC_FILE.
The board would boot into 'FATAL: MAC not in cube table'. Add it there first."
echo "Cube id:     $EXPECT_CUBE_ID"

# --- 4. Native tests (mandatory) ----------------------------------------------
echo ""
echo "=== Running native tests ==="
"$PIO" test -e native || die "native tests failed — NOT flashing"

# --- 5. Compile -----------------------------------------------------------------
echo ""
echo "=== Compiling $ENV ==="
"$PIO" run -e "$ENV" || die "compile failed — NOT flashing"

# --- 6. Flash -------------------------------------------------------------------
echo ""
echo "=== Flashing $ENV over $PORT ==="
"$PIO" run -e "$ENV" -t upload --upload-port "$PORT" || die "flash failed"

# --- 7. Boot verification --------------------------------------------------------
echo ""
echo "=== Verifying boot (${BOOT_WINDOW_S}s of serial after reset) ==="
BOOT_LOG=$("$PIO_PYTHON" - "$PORT" "$BAUD" "$BOOT_WINDOW_S" <<'PYEOF'
import serial, sys, time
port, baud, window = sys.argv[1], int(sys.argv[2]), float(sys.argv[3])
s = serial.Serial(port, baud, timeout=2)
# Pulse RTS to reset the board so we capture boot from the top.
s.setDTR(False); s.setRTS(True); time.sleep(0.1); s.setRTS(False)
deadline = time.time() + window
out = b''
while time.time() < deadline:
    out += s.read(4096)
sys.stdout.write(out.decode('utf-8', 'replace'))
PYEOF
) || die "could not read serial after flash"

# A board whose MAC is missing from the compiled table boots into this and then
# does nothing useful. It still prints the MAC, so matching on the MAC alone
# would call that a success.
if echo "$BOOT_LOG" | grep -qi "FATAL: MAC not in cube table"; then
    echo "$BOOT_LOG" | tail -20
    die "board booted into 'MAC not in cube table' — the flashed firmware does not know $MAC"
fi

BOOTED_CUBE_ID=$(echo "$BOOT_LOG" | sed -nE 's/^cube_id:[[:space:]]*([0-9]+).*/\1/p' | head -1)
[ -n "$BOOTED_CUBE_ID" ] || { echo "$BOOT_LOG" | tail -20; die "board never printed a cube_id"; }

# A spare carries the CUBE_ID_NONE sentinel, whose numeric value is an
# implementation detail, so only a numeric table entry can be compared directly.
if [[ "$EXPECT_CUBE_ID" =~ ^[0-9]+$ ]] && [ "$BOOTED_CUBE_ID" != "$EXPECT_CUBE_ID" ]; then
    echo "$BOOT_LOG" | tail -20
    die "booted cube_id $BOOTED_CUBE_ID does not match the table's $EXPECT_CUBE_ID for $MAC"
fi

echo "Boot verified: MAC $MAC, cube_id $BOOTED_CUBE_ID, environment $ENV"
echo ""
echo "If this Mac is on the cube network, finish with an MQTT ping:
  mosquitto_rr -h 192.168.8.247 -t 'cube/$BOOTED_CUBE_ID/ping' -e 'cube/$BOOTED_CUBE_ID/echo' -m test -W 3"
