#!/bin/bash
# Flash firmware to cubes based on their board version
# Usage: ./flash_cubes.sh <cube_id> [v1|v6]

CUBE_VERSIONS_FILE="$(dirname "$0")/../cube_board_versions.txt"
FW_DIR="$(dirname "$0")/.."
PIO="$HOME/.platformio/penv/bin/pio"

show_inventory() {
    echo "MAC-to-Board-Version Inventory:"
    echo "--------------------------------"
    grep -v "^#" "$CUBE_VERSIONS_FILE" | grep -v "^$" | sort
    echo ""
}

get_mac_from_arp() {
    local cube_id=$1
    local ip="192.168.8.$((cube_id + 20))"
    ping -c 1 -t 1 "$ip" >/dev/null 2>&1
    # macOS arp elides leading zeros (e.g. "5c:1:3b:..."); zero-pad each octet.
    arp -a | grep "$ip" | grep -oE '([0-9a-f]{1,2}:){5}[0-9a-f]{1,2}' \
        | awk -F: 'BEGIN{OFS=":"} {for(i=1;i<=NF;i++) if(length($i)==1) $i="0"$i; print}' \
        | tr 'a-f' 'A-F'
}

get_version_by_mac() {
    local mac=$1
    grep -i "^${mac}=" "$CUBE_VERSIONS_FILE" | cut -d= -f2
}

is_cube_online() {
    local cube_id=$1
    mosquitto_pub -h 192.168.8.247 -t "cube/$cube_id/ping" -m "test" >/dev/null 2>&1
    mosquitto_sub -h 192.168.8.247 -t "cube/$cube_id/echo" -C 1 -W 2 >/dev/null 2>&1
    return $?
}

get_cube_version() {
    local cube_id=$1
    mosquitto_sub -h 192.168.8.247 -t "cube/$cube_id/version" -C 1 -W 2 2>/dev/null | tr -d '\n'
}

wake_cube() {
    local cube_id=$1
    echo "Waking cube $cube_id..."

    # Use wake script to clear retained sleep message
    MQTT_SERVER=192.168.8.247 "$(dirname "$0")/wake.sh"
    sleep 2  # Give cube time to wake
}

wait_for_cube_online() {
    local cube_id=$1
    local max_attempts=30
    local attempt=0

    echo "Waiting for cube $cube_id to come online..."
    while [ $attempt -lt $max_attempts ]; do
        if is_cube_online "$cube_id"; then
            echo "✅ Cube $cube_id is online"
            return 0
        fi
        attempt=$((attempt + 1))
        echo "  Attempt $attempt/$max_attempts..."
        sleep 1
    done

    echo "❌ Cube $cube_id did not come online after 30 seconds"
    return 1
}

flash_cube() {
    local cube_id=$1
    local version=$2
    local ip="192.168.8.$((cube_id + 20))"

    # If no version specified, resolve MAC from ARP and look up version
    if [ -z "$version" ]; then
        local mac=$(get_mac_from_arp "$cube_id")
        if [ -z "$mac" ]; then
            echo "❌ Could not resolve MAC for cube $cube_id at $ip"
            return 1
        fi
        echo "Detected MAC: $mac"
        version=$(get_version_by_mac "$mac")
        if [ -z "$version" ]; then
            echo "❌ MAC $mac not found in $CUBE_VERSIONS_FILE"
            return 1
        fi
    fi

    # Check if cube is online, wake if sleeping
    if ! is_cube_online "$cube_id"; then
        echo "⚠️  Cube $cube_id not responding to MQTT (possibly sleeping)"
        wake_cube "$cube_id"
        if ! wait_for_cube_online "$cube_id"; then
            echo "❌ Could not wake cube $cube_id. Aborting flash."
            return 1
        fi
    fi

    # Get current firmware version from cube, compare to target
    # Version format: "<sha>+<env>" (e.g. "1a73415+v6_with_hall") so env changes force a reflash
    local current_version=$(get_cube_version "$cube_id")
    if git -C "$(dirname "$0")/.." diff --quiet -- src 2>/dev/null; then
        # Clean tree - compare sha+env
        local sha=$(git -C "$(dirname "$0")/.." rev-parse --short HEAD)
        local target_version="${sha}+${version}"
        if [[ "$current_version" == "$target_version" ]]; then
            echo "✅ Cube $cube_id already running current firmware ($current_version, skipping)"
            return 0
        fi
    fi
    # Dirty tree - always flash (timestamp version won't match)

    echo "Flashing cube $cube_id (IP: $ip) with $version firmware..."
    if [ -n "$current_version" ]; then
        echo "   Current: $current_version"
    fi

    cd "$FW_DIR"

    # Rebuild if working tree state doesn't match build state
    # (clean tree but binary has "u" suffix = was built dirty)
    if git -C "$(dirname "$0")/.." diff --quiet -- src 2>/dev/null; then
        # Tree is clean - check if binary was built dirty
        if strings .pio/build/"$version"/firmware.elf 2>/dev/null | grep -qE '^[0-9]{4}\.[0-9]{4}u$'; then
            echo "   Rebuilding: clean tree but binary was built dirty"
            rm -rf ".pio/build/$version"
            $PIO run -e "$version"
        fi
    fi

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
    if [ -t 0 ]; then
        read -p "Flash all cubes? [y/N] " REPLY
        if [[ ! $REPLY =~ ^[Yy]$ ]]; then
            exit 1
        fi
    fi

    for cube_id in 1 2 3 4 5 6; do
        flash_cube "$cube_id"
        echo ""
    done
else
    cube_id=$1
    version=$2
    flash_cube "$cube_id" "$version"
fi
