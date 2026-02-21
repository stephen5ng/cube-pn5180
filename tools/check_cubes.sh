#!/bin/bash
# Check health of all P0 cubes via UDP diag
# Usage: ./check_cubes.sh
# Sends random letters to all cubes for 5 seconds, then queries diagnostics

CUBES=(1 2 3 4 5 6)
UDP_PORT=54321
MQTT_HOST=192.168.8.247

# Thresholds
MAX_LOOP_US=5000
MAX_NFC_US=5000
MAX_LETTER_AVG_MS=600
MIN_RSSI=-75

query() {
  local ip="192.168.8.$((20 + $1))"
  python3 -c "
import socket, sys
s = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
s.settimeout(2)
try:
    s.sendto(b'diag', ('$ip', $UDP_PORT))
    print(s.recv(512).decode())
except:
    pass
s.close()
"
}

# Send letters to all cubes for N seconds, then stop
send_letters() {
  local duration=$1
  local end_time=$(( $(date +%s) + duration ))
  while [ $(date +%s) -lt $end_time ]; do
    for letter in {A..Z}; do
      [ $(date +%s) -ge $end_time ] && break
      for cube_id in "${CUBES[@]}"; do
        mosquitto_pub -c -i "${cube_id}_check" -h $MQTT_HOST -t "cube/$cube_id/letter" -m "$letter" &
      done
      sleep 0.25
    done
  done
  wait
}

parse() {
  local line="$1"
  echo $(echo "$line" | grep -o 'loop=[0-9]*'    | cut -d= -f2)
  echo $(echo "$line" | grep -o 'nfc=[0-9]*'     | head -1 | cut -d= -f2)
  echo $(echo "$line" | grep -o 'nfc_max=[0-9]*' | cut -d= -f2)
  echo $(echo "$line" | grep -o 'letter_avg=[0-9]*' | cut -d= -f2)
  echo $(echo "$line" | grep -o 'letter_max=[0-9]*' | cut -d= -f2)
  echo $(echo "$line" | grep -o 'rssi=-\?[0-9]*' | cut -d= -f2)
}

RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[0;33m'
NC='\033[0m'

pass() { printf "${GREEN}PASS${NC}"; }
fail() { printf "${RED}FAIL${NC}"; }
warn() { printf "${YELLOW}WARN${NC}"; }

echo "Sending letters for 5 seconds..."
send_letters 5

printf "\n%-6s %-5s %10s %10s %10s %12s %12s %7s\n" \
  "CUBE" "     " "LOOP(us)" "NFC(us)" "NFC_MAX(us)" "LETTER_AVG(ms)" "LETTER_MAX(ms)" "RSSI"
printf "%-6s %-5s %10s %10s %10s %12s %12s %7s\n" \
  "----" "-----" "--------" "-------" "-----------" "--------------" "--------------" "----"

all_pass=true

for cube_id in "${CUBES[@]}"; do
  raw=$(query $cube_id)
  if [ -z "$raw" ]; then
    printf "%-6s ${RED}%-5s${NC}\n" "$cube_id" "TIMEOUT"
    all_pass=false
    continue
  fi

  read loop nfc nfc_max letter_avg letter_max rssi <<< $(parse "$raw")

  # Evaluate each metric
  loop_ok=true;       [ "$loop"       -gt $MAX_LOOP_US ]       && loop_ok=false
  nfc_ok=true;        [ "$nfc"        -gt $MAX_NFC_US ]         && nfc_ok=false
  letter_ok=true;     [ "$letter_avg" -gt $MAX_LETTER_AVG_MS ]  && letter_ok=false
  rssi_ok=true;       [ "$rssi"       -lt $MIN_RSSI ]            && rssi_ok=false

  overall=true
  ($loop_ok && $nfc_ok && $letter_ok && $rssi_ok) || overall=false
  $overall || all_pass=false

  # Status
  if $overall; then status=$(pass); else status=$(fail); fi

  printf "%-6s %b %10s %10s %10s %12s %12s %7s\n" \
    "$cube_id" "$status" "$loop" "$nfc" "$nfc_max" "$letter_avg" "$letter_max" "${rssi}dB"

  # Print any individual failures
  $loop_ok   || printf "       └─ loop time ${loop}us exceeds ${MAX_LOOP_US}us\n"
  $nfc_ok    || printf "       └─ NFC avg ${nfc}us exceeds ${MAX_NFC_US}us (NFC hardware issue?)\n"
  $letter_ok || printf "       └─ letter_avg ${letter_avg}ms exceeds ${MAX_LETTER_AVG_MS}ms (is random_letters.sh running?)\n"
  $rssi_ok   || printf "       └─ RSSI ${rssi}dBm below ${MIN_RSSI}dBm\n"
done

echo ""
if $all_pass; then
  printf "${GREEN}All cubes healthy.${NC}\n\n"
else
  printf "${RED}Some cubes failed — see above.${NC}\n\n"
fi
