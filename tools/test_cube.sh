#!/bin/bash
# Test a single cube's performance
# Usage: ./test_cube.sh [cube_id]
# Default: cube 1

CUBE_ID=${1:-1}
MQTT_HOST=192.168.8.247

# Calculate IP: P0 cubes (1-6) use .21-.26, P1 cubes (11-16) use .31-.36
if [ "$CUBE_ID" -ge 1 ] && [ "$CUBE_ID" -le 6 ]; then
  CUBE_IP="192.168.8.$((20 + CUBE_ID))"
elif [ "$CUBE_ID" -ge 11 ] && [ "$CUBE_ID" -le 16 ]; then
  CUBE_IP="192.168.8.$((20 + CUBE_ID))"
else
  echo "Invalid cube ID: $CUBE_ID (valid: 1-6, 11-16)"
  exit 1
fi

echo "Testing cube $CUBE_ID at $CUBE_IP..."
echo "Sending letters for 8 seconds..."

# Send letters in background
for i in {1..8}; do
  for letter in {A..Z}; do
    mosquitto_pub -c -i "test_${CUBE_ID}" -h "$MQTT_HOST" -t "cube/$CUBE_ID/letter" -m "$letter" 2>/dev/null &
    sleep 0.25
  done
done &

sleep 8

# Query diagnostics via Python
python3 /Users/stephenng/Documents/PlatformIO/Projects/cube-pn5180/tools/test_cube_diag.py "$CUBE_IP" "$CUBE_ID"

# Cleanup
pkill -f "mosquitto_pub.*test_${CUBE_ID}" 2>/dev/null || true
