#!/bin/bash
# Flash firmware to cubes based on their board version
# Usage: ./flash_cubes.sh <cube_id> [v1|v6]

CUBE_VERSIONS_FILE="$(dirname "$0")/../cube_board_versions.txt"
FW_DIR="$(dirname "$0")/.."
PIO="$HOME/.platformio/penv/bin/pio"

show_inventory() {
    echo "Cube Board Version Inventory:"
    echo "------------------------------"
    cat "$CUBE_VERSIONS_FILE" | grep -v "^#" | sort
    echo ""
}

flash_cube() {
    local cube_id=$1
    local version=$2
    local ip="192.168.8.$((cube_id + 20))"

    echo "Flashing cube $cube_id (IP: $ip) with $version firmware..."
    cd "$FW_DIR"
    $PIO run -e "$version" -t upload --upload-port "$ip"

    if [ $? -eq 0 ]; then
        echo "✅ Cube $cube_id flashed successfully"

        # Reboot and verify
        echo "Rebooting cube $cube_id..."
        mosquitto_pub -h 192.168.8.247 -t "cube/$cube_id/reboot" -m "0"
#        sleep 8

        # Check firmware version via UDP
#a        fw_check=$(echo "diag" | nc -u -w 1 "$ip" 54321 2>/dev/null | grep -o 'fw=v[0-9]' || echo "")
  #      if [[ "$fw_check" == "fw=$version" ]]; then
#            echo "✅ Verified: cube $cube_id running $version firmware"
#        else
#            echo "⚠️  Warning: could not verify firmware version on cube $cube_id"
#        fi
    else
        echo "❌ Failed to flash cube $cube_id"
        return 1
    fi
}

# Main
if [ $# -eq 0 ]; then
    show_inventory
    echo "Usage: $0 <cube_id> [v1|v6]"
    echo "       $0 all"
    exit 1
fi

if [ "$1" = "all" ]; then
    show_inventory
    echo ""
    read -p "Flash ALL cubes with their correct firmware? (y/N) " -n 1 -r
    echo
    if [[ ! $REPLY =~ ^[Yy]$ ]]; then
        exit 1
    fi

    while IFS='=' read -r cube_id version; do
        if [[ ! "$cube_id" =~ ^# ]] && [[ -n "$cube_id" ]]; then
            flash_cube "$cube_id" "$version"
            echo ""
        fi
    done < "$CUBE_VERSIONS_FILE"
else
    cube_id=$1
    version=$2

    if [ -z "$version" ]; then
        # Look up version from inventory
        version=$(grep "^$cube_id=" "$CUBE_VERSIONS_FILE" | cut -d= -f2)
        if [ -z "$version" ]; then
            echo "❌ Cube $cube_id not found in inventory. Please specify version."
            echo "   Usage: $0 $cube_id v1"
            exit 1
        fi
    fi

    flash_cube "$cube_id" "$version"
fi
