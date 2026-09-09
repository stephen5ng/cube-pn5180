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
#   1. Detect the serial port (or take it as $1)
#   2. Read the board's MAC via esptool
#   3. Look up the MAC -> board version -> platformio environment
#   4. Run the native test suite (mandatory before any flash)
#   5. Compile the target environment
#   6. Flash over USB
#   7. Verify the boot on serial (MAC + cube id must appear)

set -euo pipefail

CUBE_VERSIONS_FILE="$(dirname "$0")/../config/cube_board_versions.txt"
PIO="${PIO:-$HOME/.platformio/penv/bin/pio}"
PIO_PYTHON="${PIO_PYTHON:-$HOME/.platformio/penv/bin/python}"
BAUD=115200

die() { echo "ERROR: $*" >&2; exit 1; }

# platformio (tests, build, upload) resolves the project from the cwd, so run
# everything from the firmware dir regardless of where the script is invoked.
cd "$(dirname "$0")/.."

# --- 1. Serial port -----------------------------------------------------------
PORT="${1:-}"
if [ -z "$PORT" ]; then
    # Common USB-serial adapters on macOS; cube boards have used WCH/CH34x.
    PORT=$(ls /dev/cu.usbserial-* /dev/cu.wchusbserial* 2>/dev/null | head -1 || true)
fi
[ -n "$PORT" ] || die "no serial port found. Plug the cube in or pass one: $0 /dev/cu.usbserial-XXX"
[ -c "$PORT" ] || die "not a serial device: $PORT"
echo "Serial port: $PORT"

# --- 2. MAC --------------------------------------------------------------------
MAC_RAW=$("$PIO_PYTHON" -m esptool --port "$PORT" read_mac 2>/dev/null \
    | grep -i '^MAC:' | awk '{print $2}') \
    || die "could not read MAC from $PORT — is the cube in bootloader-reachable state?"
[ -n "$MAC_RAW" ] || die "could not read MAC from $PORT"
MAC=$(echo "$MAC_RAW" | tr 'a-f' 'A-F')
echo "Board MAC:   $MAC"

# --- 3. Version -> environment -------------------------------------------------
VERSION=$(grep -i "^$MAC=" "$CUBE_VERSIONS_FILE" | head -1 | cut -d= -f2 | tr -d '[:space:]') \
    || true
[ -n "$VERSION" ] || die "MAC $MAC is not in $CUBE_VERSIONS_FILE — refusing to flash an unknown board.
Add it there first (see validate_mac_table.py)."
ENV="$VERSION"
echo "Environment: $ENV (board version $VERSION)"

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
echo "=== Verifying boot (12s of serial after reset) ==="
BOOT_LOG=$("$PIO_PYTHON" - "$PORT" <<'PYEOF'
import serial, sys, time
port = sys.argv[1]
s = serial.Serial(port, 115200, timeout=2)
# Pulse RTS to reset the board so we capture boot from the top.
s.setDTR(False); s.setRTS(True); time.sleep(0.1); s.setRTS(False)
deadline = time.time() + 12
out = b''
while time.time() < deadline:
    out += s.read(4096)
sys.stdout.write(out.decode('utf-8', 'replace'))
PYEOF
) || die "could not read serial after flash"
echo "$BOOT_LOG" | grep -qi "$MAC" || { echo "$BOOT_LOG" | tail -20; die "booted board did not report MAC $MAC"; }
CUBE_ID=$(echo "$BOOT_LOG" | grep -oEi '^cube_id: [0-9]+' | awk '{print $2}')
echo "Boot verified: MAC $MAC, cube_id: ${CUBE_ID:-<not printed yet>}"
echo ""
echo "If this Mac is on the cube network, finish with an MQTT ping:
  mosquitto_sub -h 192.168.8.247 -t 'cube/${CUBE_ID:-N}/echo' -C 1 &
  mosquitto_pub -h 192.168.8.247 -t 'cube/${CUBE_ID:-N}/ping' -m test"
