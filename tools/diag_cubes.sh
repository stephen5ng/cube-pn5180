#!/bin/bash
# Query diagnostic timing from all cubes via UDP
# Usage: ./diag_cubes.sh [count]
#   count: number of times to query (default: 1, use 0 for continuous)
#
# Results are appended to diag_cubes.log in CSV format for trend analysis

COUNT=${1:-1}
UDP_PORT=54321
TIMEOUT=2
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_DIR="$(dirname "$SCRIPT_DIR")"
UDP_QUERY="$SCRIPT_DIR/udp_query.py"
LOG_FILE="$PROJECT_DIR/logs/diag_cubes.log"
BOARD_VERSIONS_FILE="$PROJECT_DIR/cube_board_versions.txt"

# Read board version for a cube ID
get_board_version() {
  local cube_id=$1
  grep "^${cube_id}=" "$BOARD_VERSIONS_FILE" 2>/dev/null | cut -d= -f2
}

# Cube IPs: cube_id + 20 is the last octet
# P0: cubes 1-6 -> IPs .21-.26
# P1: cubes 11-16 -> IPs .31-.36
CUBES=(
  "192.168.8.21:1"
  "192.168.8.22:2"
  "192.168.8.23:3"
  "192.168.8.24:4"
  "192.168.8.25:5"
  "192.168.8.26:6"
)

# Initialize log file (no header needed for JSONL)
init_log() {
  : # JSONL doesn't need a header
}

# Append result as JSON line
log_result() {
  local timestamp="$1"
  local cube="$2"
  local board="$3"
  local mac="$4"
  local loop="$5"
  local mqtt="$6"
  local disp="$7"
  local udp_t="$8"
  local nfc="$9"
  local nfc_max="${10}"
  local ltr_avg="${11}"
  local ltr_max="${12}"
  local rssi="${13}"

  if [[ "$cube" == *"TIMEOUT"* ]]; then
    echo "{\"timestamp\":\"$timestamp\",\"cube\":\"$cube\",\"board\":\"$board\",\"timeout\":true}" >> "$LOG_FILE"
  else
    echo "{\"timestamp\":\"$timestamp\",\"cube\":\"$cube\",\"board\":\"$board\",\"mac\":\"$mac\",\"loop_us\":$loop,\"mqtt_us\":$mqtt,\"disp_us\":$disp,\"udp_us\":$udp_t,\"nfc_us\":$nfc,\"nfc_max_us\":$nfc_max,\"letter_avg_ms\":$ltr_avg,\"letter_max_ms\":$ltr_max,\"rssi_db\":$rssi}" >> "$LOG_FILE"
  fi
}

query_cube() {
  local ip="${1%%:*}"
  local label="${1##*:}"
  local response
  # Use Python script for proper UDP timeout handling
  response=$("$UDP_QUERY" "$ip" $UDP_PORT "diag" 0.5 2>/dev/null)

  if [ -n "$response" ]; then
    echo "$response"
  else
    echo "cube$label|TIMEOUT"
  fi
}

print_header() {
  printf "%-6s %6s %8s %8s %8s %8s %8s %10s %10s %10s %6s\n" \
    "CUBE" "BOARD" "LOOP" "MQTT" "DISP" "UDP" "NFC" "NFC_MAX" "LTR_AVG" "LTR_MAX" "RSSI"
  printf "%-6s %6s %8s %8s %8s %8s %8s %10s %10s %10s %6s\n" \
    "----" "-----" "----" "----" "----" "---" "---" "-------" "-------" "-------" "----"
}

parse_and_print() {
  local line="$1"
  local timestamp="$2"

  if [[ "$line" == *"|TIMEOUT" ]]; then
    local cube="${line%%|*}"
    local cube_id="${cube#cube}"
    local board=$(get_board_version "$cube_id")
    printf "%-6s %6s %8s\n" "$cube" "$board" "TIMEOUT"
    log_result "$timestamp" "$cube" "$board" ""
    return
  fi

  # Parse: cube_id|board=N|fw=N|mac=XX:XX...|loop=N|mqtt=N|disp=N|udp=N|nfc=N|nfc_max=N|letter_avg=N|letter_max=N|letter_n=N|rssi=N|samples=N
  # Old format: cube_id|fw=N|mac=XX:XX... (fallback to file for board type)
  local cube=$(echo "$line" | cut -d'|' -f1)
  local cube_id="${cube#cube}"
  local board_from_firmware=$(echo "$line" | grep -o 'board=[^|]*' | cut -d= -f2)
  local board="${board_from_firmware:-$(get_board_version "$cube_id")}"
  local mac=$(echo "$line" | grep -o 'mac=[0-9A-F:]*' | cut -d= -f2)
  local loop=$(echo "$line" | grep -o 'loop=[0-9]*' | cut -d= -f2)
  local mqtt=$(echo "$line" | grep -o 'mqtt=[0-9]*' | cut -d= -f2)
  local disp=$(echo "$line" | grep -o 'disp=[0-9]*' | cut -d= -f2)
  local udp_t=$(echo "$line" | grep -o 'udp=[0-9]*' | cut -d= -f2)
  local nfc=$(echo "$line" | grep -o 'nfc=[0-9]*' | head -1 | cut -d= -f2)
  local nfc_max=$(echo "$line" | grep -o 'nfc_max=[0-9]*' | cut -d= -f2)
  local ltr_avg=$(echo "$line" | grep -o 'letter_avg=[0-9]*' | cut -d= -f2)
  local ltr_max=$(echo "$line" | grep -o 'letter_max=[0-9]*' | cut -d= -f2)
  local rssi=$(echo "$line" | grep -o 'rssi=-\?[0-9]*' | cut -d= -f2)

  printf "%-6s %6s %7sus %7sus %7sus %7sus %7sus %9sus %9sms %9sms %5sdB\n" \
    "$cube" "$board" "$loop" "$mqtt" "$disp" "$udp_t" "$nfc" "$nfc_max" "$ltr_avg" "$ltr_max" "$rssi"

  log_result "$timestamp" "$cube" "$board" "$mac" "$loop" "$mqtt" "$disp" "$udp_t" "$nfc" "$nfc_max" "$ltr_avg" "$ltr_max" "$rssi"
}

# Initialize log file
init_log

iteration=0
while true; do
  iteration=$((iteration + 1))

  if [ "$COUNT" -gt 0 ] && [ "$iteration" -gt "$COUNT" ]; then
    break
  fi

  timestamp=$(date -u +"%Y-%m-%dT%H:%M:%SZ")
  echo ""
  echo "=== Diagnostic query #$iteration $(date '+%H:%M:%S') ==="
  print_header

  for cube in "${CUBES[@]}"; do
    result=$(query_cube "$cube")
    parse_and_print "$result" "$timestamp"
  done

  if [ "$COUNT" -eq 0 ] || [ "$iteration" -lt "$COUNT" ]; then
    sleep 3
  fi
done
