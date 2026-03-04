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
UDP_QUERY="$SCRIPT_DIR/udp_query.py"
LOG_FILE="$SCRIPT_DIR/diag_cubes.log"

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
  local loop="$3"
  local mqtt="$4"
  local disp="$5"
  local udp_t="$6"
  local nfc="$7"
  local nfc_max="$8"
  local ltr_avg="$9"
  local ltr_max="${10}"
  local rssi="${11}"

  if [[ "$cube" == *"TIMEOUT"* ]]; then
    echo "{\"timestamp\":\"$timestamp\",\"cube\":\"$cube\",\"timeout\":true}" >> "$LOG_FILE"
  else
    echo "{\"timestamp\":\"$timestamp\",\"cube\":\"$cube\",\"loop_us\":$loop,\"mqtt_us\":$mqtt,\"disp_us\":$disp,\"udp_us\":$udp_t,\"nfc_us\":$nfc,\"nfc_max_us\":$nfc_max,\"letter_avg_ms\":$ltr_avg,\"letter_max_ms\":$ltr_max,\"rssi_db\":$rssi}" >> "$LOG_FILE"
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
  printf "%-6s %8s %8s %8s %8s %8s %10s %10s %10s %6s\n" \
    "CUBE" "LOOP" "MQTT" "DISP" "UDP" "NFC" "NFC_MAX" "LTR_AVG" "LTR_MAX" "RSSI"
  printf "%-6s %8s %8s %8s %8s %8s %10s %10s %10s %6s\n" \
    "----" "----" "----" "----" "---" "---" "-------" "-------" "-------" "----"
}

parse_and_print() {
  local line="$1"
  local timestamp="$2"

  if [[ "$line" == *"|TIMEOUT" ]]; then
    local cube="${line%%|*}"
    printf "%-6s %8s\n" "$cube" "TIMEOUT"
    log_result "$timestamp" "$cube"
    return
  fi

  # Parse: cube_id|loop=N|mqtt=N|disp=N|udp=N|nfc=N|nfc_max=N|letter_avg=N|letter_max=N|letter_n=N|rssi=N|samples=N
  local cube=$(echo "$line" | cut -d'|' -f1)
  local loop=$(echo "$line" | grep -o 'loop=[0-9]*' | cut -d= -f2)
  local mqtt=$(echo "$line" | grep -o 'mqtt=[0-9]*' | cut -d= -f2)
  local disp=$(echo "$line" | grep -o 'disp=[0-9]*' | cut -d= -f2)
  local udp_t=$(echo "$line" | grep -o 'udp=[0-9]*' | cut -d= -f2)
  local nfc=$(echo "$line" | grep -o 'nfc=[0-9]*' | head -1 | cut -d= -f2)
  local nfc_max=$(echo "$line" | grep -o 'nfc_max=[0-9]*' | cut -d= -f2)
  local ltr_avg=$(echo "$line" | grep -o 'letter_avg=[0-9]*' | cut -d= -f2)
  local ltr_max=$(echo "$line" | grep -o 'letter_max=[0-9]*' | cut -d= -f2)
  local rssi=$(echo "$line" | grep -o 'rssi=-\?[0-9]*' | cut -d= -f2)

  printf "%-6s %7sus %7sus %7sus %7sus %7sus %9sus %9sms %9sms %5sdB\n" \
    "$cube" "$loop" "$mqtt" "$disp" "$udp_t" "$nfc" "$nfc_max" "$ltr_avg" "$ltr_max" "$rssi"

  log_result "$timestamp" "$cube" "$loop" "$mqtt" "$disp" "$udp_t" "$nfc" "$nfc_max" "$ltr_avg" "$ltr_max" "$rssi"
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
