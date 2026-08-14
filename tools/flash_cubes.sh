#!/bin/bash
# Flash firmware to cubes based on their board version
# Usage: ./flash_cubes.sh <cube_id> [v1|v6]

CUBE_VERSIONS_FILE="$(dirname "$0")/../config/cube_board_versions.txt"
FW_DIR="$(dirname "$0")/.."
PIO="${PIO:-$HOME/.platformio/penv/bin/pio}"
PYTHON="${PYTHON:-python3}"

# One settling window for the whole fleet before the "all" sweep probes it. A
# sleeping cube answers on its next check-in -- sleep_interval_s, 20 by default
# -- and then needs a full boot, so this covers both, once, rather than per
# cube.
FLEET_SETTLE_S="${FLEET_SETTLE_S:-45}"

if ! python3 "$(dirname "$0")/validate_mac_table.py"; then
    echo "MAC table validation failed; aborting." >&2
    exit 1
fi

show_inventory() {
    echo "MAC-to-Board-Version Inventory:"
    echo "--------------------------------"
    grep -v "^#" "$CUBE_VERSIONS_FILE" | grep -v "^$" | sort
    echo ""
}

ping_once() {
    local ip=$1
    if command -v ip >/dev/null 2>&1; then
        ping -c 1 -W 1 "$ip" >/dev/null 2>&1
    else
        # macOS ping: -t is the timeout in seconds.
        ping -c 1 -t 1 "$ip" >/dev/null 2>&1
    fi
}

# MAC and static-IP octet for every board, from the compiled table that
# assigns them. The table is the only place an octet is decided, so reading it
# beats duplicating the mapping here.
mac_table_entries() {
    sed -n '/^#else/,/^#endif/p' "$FW_DIR/src/cube_utilities.cpp" \
        | sed -nE 's/^[[:space:]]*\{"(([0-9A-F]{2}:){5}[0-9A-F]{2})"[[:space:]]*,[^,]+,[^,]+,[[:space:]]*([0-9]+)[[:space:]]*\}.*/\1 \3/p'
}

# "MAC slot" for every board holding one, from the retained assignment
# records, in a single round trip. A null slot has no numeric match and so is
# absent, which is what "holds no slot" should look like here.
assignment_map() {
    mosquitto_sub -h 192.168.8.247 -t 'cube/assign/+' -v -W 2 2>/dev/null \
        | sed -nE 's#^cube/assign/([0-9A-Fa-f]{12})[[:space:]].*"slot"[[:space:]]*:[[:space:]]*([0-9]+).*#\1 \2#p'
}

# The address of the board that holds a slot.
#
# Asks the retained assignments first, because an address is not evidence of
# ownership. A retired board left powered still answers at the address its
# slot used to imply, and flashing it would report success -- the version
# check reads cube/{id}/version, which the board that actually holds the slot
# publishes -- while the cube in play kept its old firmware.
#
# Only when no assignment record exists at all does it fall back to guessing
# from the slot: primary-set MACs get 20+N and backup-set MACs 40+N
# (findCubeIpOctet), which holds only while every board's octet tracks its
# slot. A spare breaks that, keeping the octet its MAC was given.
resolve_cube_ip() {
    local cube_id=$1
    local assignments owner octet ip

    assignments=$(assignment_map)
    if [ -n "$assignments" ]; then
        owner=$(awk -v want="$cube_id" '$2 == want {print $1; exit}' \
            <<<"$assignments")
        if [ -z "$owner" ]; then
            echo "No board holds slot $cube_id" >&2
            return 1
        fi
        octet=$(mac_table_entries | awk -v want="$owner" '
            {mac = $1; gsub(/:/, "", mac)
             if (toupper(mac) == toupper(want)) {print $2; exit}}')
        if [ -z "$octet" ]; then
            echo "Slot $cube_id is held by $owner, which is not in the MAC table" >&2
            return 1
        fi
        ip="192.168.8.$octet"
        ping_once "$ip" && { echo "$ip"; return 0; }
        return 1
    fi

    for base in 20 40; do
        ip="192.168.8.$((cube_id + base))"
        if ping_once "$ip"; then
            echo "$ip"
            return 0
        fi
    done
    return 1
}

get_mac_from_arp() {
    local cube_id=$1
    local ip=$2
    if command -v ip >/dev/null 2>&1; then
        # Raspberry Pi / Linux: iproute2 is part of the base system.
        ping -c 1 -W 1 "$ip" >/dev/null 2>&1 || true
        ip neigh show to "$ip" \
            | awk -v target="$ip" '$1 == target && $5 ~ /^([0-9a-f]{2}:){5}[0-9a-f]{2}$/ {print toupper($5); exit}'
    else
        # macOS fallback: arp elides leading zeros (e.g. "5c:1:3b:...").
        ping -c 1 -t 1 "$ip" >/dev/null 2>&1 || true
        arp -a | grep "$ip" | grep -oE '([0-9a-f]{1,2}:){5}[0-9a-f]{1,2}' \
            | awk -F: 'BEGIN{OFS=":"} {for(i=1;i<=NF;i++) if(length($i)==1) $i="0"$i; print}' \
            | tr 'a-f' 'A-F'
    fi
}

get_version_by_mac() {
    local mac=$1
    grep -i "^${mac}=" "$CUBE_VERSIONS_FILE" | cut -d= -f2
}

is_cube_online() {
    local cube_id=$1

    # The cube echoes within milliseconds and does not retain the reply, so the
    # subscription must be established before the ping is published.
    # mosquitto_rr subscribes and waits for SUBACK before publishing; separate
    # pub/sub calls cannot express that ordering.
    mosquitto_rr -h 192.168.8.247 \
        -t "cube/$cube_id/ping" -e "cube/$cube_id/echo" \
        -m "test" -W 3 >/dev/null 2>&1
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

# A cube sleeps after AUTO_SLEEP_TIMEOUT_MS (10 minutes) of inactivity,
# whatever the retained sleep flag says. Flashing the fleet takes longer than
# that, so cubes late in the list doze off while the earlier ones are being
# flashed, and each then has to be woken and waited for.
#
# The /ping subscription resets the cube's inactivity timer, so nudging the
# ones still to come keeps them up rather than recovering them afterwards. A
# cube already asleep is not listening, which is fine -- flash_cube still
# wakes it.
keep_awake() {
    local id
    for id in "$@"; do
        mosquitto_pub -h 192.168.8.247 -t "cube/$id/ping" -m keep-awake \
            >/dev/null 2>&1 || true
    done
}

# A sleeping cube learns the flag was cleared only at its next check-in --
# sleep_interval_s, 20 by default -- and then needs a full boot, so the window
# has to cover both comfortably.
#
# Measured against the clock, not counted in attempts: each failed
# is_cube_online blocks for its own timeout before this loop's sleep, so an
# attempt count is several times longer than the same number of seconds and
# the reported figure would be wrong.
wait_for_cube_online() {
    local cube_id=$1
    local timeout_s=${2:-60}
    local deadline=$((SECONDS + timeout_s))

    echo "Waiting up to ${timeout_s}s for cube $cube_id to come online..."
    while [ $SECONDS -lt $deadline ]; do
        if is_cube_online "$cube_id"; then
            echo "✅ Cube $cube_id is online"
            return 0
        fi
        echo "  $((deadline - SECONDS))s remaining..."
        sleep 1
    done

    echo "❌ Cube $cube_id did not come online within ${timeout_s}s"
    return 1
}

flash_cube() {
    local cube_id=$1
    local version=$2

    # Wake first. Resolving the address needs the cube to answer a ping, so a
    # sleeping cube would fail resolution and return before ever reaching the
    # wake below -- the recovery path would be unreachable exactly when it is
    # needed.
    if ! is_cube_online "$cube_id"; then
        echo "⚠️  Cube $cube_id not responding to MQTT (possibly sleeping)"
        wake_cube "$cube_id"
        if ! wait_for_cube_online "$cube_id"; then
            echo "❌ Could not wake cube $cube_id. Aborting flash."
            return 1
        fi
    fi

    local ip=$(resolve_cube_ip "$cube_id")
    if [ -z "$ip" ]; then
        echo "❌ Cube $cube_id is on MQTT but its address did not answer"
        return 1
    fi

    # If no version specified, resolve MAC from ARP and look up version
    if [ -z "$version" ]; then
        local mac=$(get_mac_from_arp "$cube_id" "$ip")
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

    # Get current firmware version from cube, compare to target.
    # Version format: "<sha>+<env>" when clean, "<sha>-<src_hash>+<env>" when dirty.
    # See scripts/compute_version.py.
    local current_version=$(get_cube_version "$cube_id")
    local target_version=$("$PYTHON" "$FW_DIR/scripts/compute_version.py" "$version" "$FW_DIR")
    if [[ "$current_version" == "$target_version" ]]; then
        echo "✅ Cube $cube_id already running current firmware ($current_version, skipping)"
        return 0
    fi

    echo "Flashing cube $cube_id (IP: $ip) with $version firmware..."
    if [ -n "$current_version" ]; then
        echo "   Current: $current_version"
        echo "   Target:  $target_version"
    fi

    cd "$FW_DIR"

    # PIO won't rebuild when only git state changed (e.g. dirty→clean commit), so
    # the cached binary's embedded GIT_VERSION can lag behind. Force a rebuild
    # when it doesn't match the target.
    local binary_version=$(strings ".pio/build/$version/firmware.elf" 2>/dev/null \
        | grep -E "^[a-f0-9]+(-[a-f0-9]+)?\+${version}$" | head -1)
    if [ "$binary_version" != "$target_version" ]; then
        if [ -n "$binary_version" ]; then
            echo "   Rebuilding: binary embeds $binary_version, target is $target_version"
        fi
        rm -rf ".pio/build/$version"
    fi

    $PIO run -e "$version" -t upload --upload-port "$ip"
    local upload_status=$?

    if [ $upload_status -ne 0 ]; then
        # espota routinely reports failure after a successful upload: its
        # result-wait dies when the cube reboots (or a lossy link drops the
        # final OK) before answering. The retained version topic is the
        # ground truth -- the rebooted cube republishes it -- so ask the
        # cube before believing the exit code.
        echo "Upload reported failure; checking what cube $cube_id is running..."
        local attempt
        for attempt in 1 2 3; do
            sleep 8
            if [[ "$(get_cube_version "$cube_id")" == "$target_version" ]]; then
                echo "✅ Cube $cube_id is running $target_version despite the reported upload failure"
                return 0
            fi
        done
    fi

    if [ $upload_status -eq 0 ]; then
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
# Checked up front: without it every cube reads as offline and the failure
# surfaces as a misleading "could not wake cube N".
if ! command -v mosquitto_rr >/dev/null 2>&1; then
    echo "❌ mosquitto_rr not found (ships with mosquitto-clients)" >&2
    exit 1
fi

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

    failed=0
    all_cubes=(1 2 3 4 5 6 11 12 13 14 15 16)

    # Wake the fleet once and wait once, then ask each slot whether anything
    # answers. flash_cube waits a full wake window per cube it cannot reach,
    # so a bench holding half the fleet spends minutes discovering the same
    # thing a 3s probe already knows. An assignment record cannot stand in for
    # this: every slot has one whether or not its board is powered.
    #
    # Word splitting rather than mapfile: this runs under macOS's bash 3.2.
    keep_awake "${all_cubes[@]}"
    echo "Waiting ${FLEET_SETTLE_S}s for woken cubes to come up..."
    sleep "$FLEET_SETTLE_S"

    present=()
    absent=()
    for cube_id in "${all_cubes[@]}"; do
        if is_cube_online "$cube_id"; then
            present+=("$cube_id")
        else
            absent+=("$cube_id")
        fi
    done

    # Named rather than silently dropped: a shortened run must not read as a
    # whole-fleet one.
    if [ ${#absent[@]} -gt 0 ]; then
        echo "⏭️  No cube answering on ${#absent[@]} slot(s), skipping: ${absent[*]}"
    fi
    if [ ${#present[@]} -eq 0 ]; then
        echo "❌ No cubes answered; nothing to flash."
        exit 1
    fi
    echo "Flashing ${#present[@]} cube(s): ${present[*]}"
    echo ""
    all_cubes=("${present[@]}")
    for index in "${!all_cubes[@]}"; do
        keep_awake "${all_cubes[@]:$index}"
        flash_cube "${all_cubes[$index]}" || failed=1
        echo ""
    done
    exit "$failed"
else
    cube_id=$1
    version=$2
    flash_cube "$cube_id" "$version"
fi
