#!/bin/bash -e
# Replace ESP32 chip for a given cube: read MAC, update source, test, compile, flash.
# Usage: ./replace_chip.sh <cube_number> [port]

CUBE_NUM=${1:?Usage: replace_chip.sh <cube_number> [port]}
PORT=${2:-/dev/cu.SLAB_USBtoUART}
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_DIR="$(dirname "$SCRIPT_DIR")"
MAC_FILE="$PROJECT_DIR/src/cube_utilities.cpp"
BOARD_FILE="$PROJECT_DIR/cube_board_versions.txt"
PIO=~/.platformio/penv/bin/platformio
ESPTOOL="$HOME/.platformio/penv/bin/python -m esptool"

# Read MAC from new chip
echo "Reading MAC from $PORT..."
MAC=$($ESPTOOL --port "$PORT" read-mac 2>&1 | grep "^MAC:" | tail -1 | awk '{print toupper($2)}')
if [ -z "$MAC" ]; then
    echo "ERROR: Could not read MAC. Check USB connection."
    exit 1
fi
echo "New MAC: $MAC"

# Calculate array index (cube 1 = line with index 0, cube 7 = cube 11, etc.)
if [ "$CUBE_NUM" -le 6 ]; then
    INDEX=$((CUBE_NUM - 1))
else
    INDEX=$((CUBE_NUM - 5))  # cube 11=6, 12=7, etc.
fi
LINE_NUM=$((23 + INDEX))  # line 23 is index 0 in production array

# Show current MAC
echo "Current entry (line $LINE_NUM):"
sed -n "${LINE_NUM}p" "$MAC_FILE"

# Update MAC in file, preserving comment after the MAC
sed -i '' "s|\"[0-9A-F:]\{17\}\",\(.*// $CUBE_NUM \)|\"$MAC\",\1|" "$MAC_FILE"

echo "Updated entry:"
sed -n "${LINE_NUM}p" "$MAC_FILE"

# Run tests
echo ""
echo "Running tests..."
cd "$PROJECT_DIR"
$PIO test -e native

# Look up board version
BOARD_ENV=$(grep "^${CUBE_NUM}=" "$BOARD_FILE" | cut -d= -f2)
if [ -z "$BOARD_ENV" ]; then
    echo "ERROR: No board version found for cube $CUBE_NUM in $BOARD_FILE"
    exit 1
fi
echo ""
echo "Board version: $BOARD_ENV"

# Compile and flash
echo "Compiling and flashing..."
$PIO run -e "$BOARD_ENV" -t upload --upload-port "$PORT"

echo ""
echo "Done! Cube $CUBE_NUM flashed with MAC $MAC"
echo "Install in cube and verify on network."
