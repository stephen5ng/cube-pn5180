#!/bin/bash

FIRMWARE_PATH="/Users/stephenng/Documents/PlatformIO/Projects/cube-pn5180/.pio/build/esp32dev/firmware.bin"

# Function to convert cube number to IP
cube_to_ip() {
    local cube_num=$1
    if [[ $cube_num -ge 1 && $cube_num -le 6 ]]; then
        echo $((20 + cube_num))
    elif [[ $cube_num -ge 11 && $cube_num -le 16 ]]; then
        echo $((20 + cube_num))
    else
        echo "Invalid cube number: $cube_num. Valid ranges are 1-6 and 11-16." >&2
        return 1
    fi
}

# Function to update a specific IP
update_cube() {
    local ip=$1
    echo "Updating device at 192.168.8.$ip"
    python3 ~/.platformio/packages/framework-arduinoespressif32/tools/espota.py -i 192.168.8.$ip -d -r -f "$FIRMWARE_PATH"
}

if [[ $# -eq 0 ]]; then
    # No arguments - update all cubes
    for ip in $(seq 21 26) $(seq 31 36); do
        update_cube $ip
    done
else
    # One or more arguments - update specific cubes
    for cube_num in "$@"; do
        ip=$(cube_to_ip $cube_num)
        if [[ $? -eq 0 ]]; then
            update_cube $ip
        else
            echo "Skipping invalid cube number: $cube_num"
            exit 1
        fi
    done
fi
