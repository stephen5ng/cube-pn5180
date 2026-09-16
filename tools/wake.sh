#!/bin/bash -e
# Clear a cube's retained auto_sleep flag so it stays awake after its next
# check-in.
#
# Usage: wake.sh              # every board in the table
#        wake.sh <cube_id>    # the board holding that slot
#        wake.sh <MAC>        # one board, with or without colons
#
# The flag is keyed by MAC rather than by slot: a board has a MAC before anyone
# assigns it a slot, and keeps it when its slot changes, so a spare can be woken
# the same way a fielded cube can.

MQTT_HOST=${MQTT_SERVER:-192.168.8.247}
FW_DIR="$(cd "$(dirname "$0")/.." && pwd)"
MAC_FILE="$FW_DIR/src/cube_utilities.cpp"

# Every board the firmware knows, colon-free, from the compiled table.
table_macs() {
    sed -n '/^#else/,/^#endif/p' "$MAC_FILE" \
        | sed -nE 's/^[[:space:]]*\{"(([0-9A-F]{2}:){5}[0-9A-F]{2})".*/\1/p' \
        | tr -d ':'
}

# The board holding a slot. Retained assignments are asked first because they
# outrank the compiled table whenever an operator has moved a cube; the table is
# the fallback for a board running on its compiled identity.
mac_for_slot() {
    local slot=$1 mac
    mac=$(mosquitto_sub -h "$MQTT_HOST" -t 'cube/assign/+' -v -W 2 2>/dev/null \
        | sed -nE "s#^cube/assign/([0-9A-Fa-f]{12})[[:space:]].*\"slot\"[[:space:]]*:[[:space:]]*${slot}[[:space:]]*[,}].*#\1#p" \
        | head -1 | tr 'a-f' 'A-F')
    if [ -z "$mac" ]; then
        mac=$(sed -n '/^#else/,/^#endif/p' "$MAC_FILE" \
            | sed -nE "s/^[[:space:]]*\{\"(([0-9A-F]{2}:){5}[0-9A-F]{2})\"[[:space:]]*,[[:space:]]*${slot}[[:space:]]*,.*/\1/p" \
            | head -1 | tr -d ':')
    fi
    printf '%s' "$mac"
}

clear_flag() {
    mosquitto_pub -h "$MQTT_HOST" -t "cube/device/$1/auto_sleep" -m "" --retain
}

if [ -z "${1:-}" ]; then
    count=0
    for mac in $(table_macs); do
        clear_flag "$mac"
        count=$((count + 1))
    done
    echo "Cleared auto_sleep flag for $count board(s)"
elif [[ "$1" =~ ^[0-9]+$ ]]; then
    mac=$(mac_for_slot "$1")
    if [ -z "$mac" ]; then
        echo "No board holds slot $1" >&2
        exit 1
    fi
    clear_flag "$mac"
    echo "Cleared auto_sleep flag for cube $1 ($mac)"
else
    mac=$(printf '%s' "$1" | tr -d ':' | tr 'a-f' 'A-F')
    clear_flag "$mac"
    echo "Cleared auto_sleep flag for $mac"
fi
