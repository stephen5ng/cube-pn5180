#!/bin/bash
# flash_cubes.sh must find a cube whose radio drops the first probe.
#
# An idle ESP32 is in WiFi modem sleep and routinely misses the first ping
# after a quiet spell; one ping with a 1s timeout reported a healthy cube as
# "on MQTT but its address did not answer". The fake ping below drops the
# first probe and answers every later one, whether they come as more calls or
# as a higher -c on the same call.
set -u
DIR="$(cd "$(dirname "$0")" && pwd)"
TMP=$(mktemp -d)
trap 'rm -rf "$TMP"' EXIT

cat > "$TMP/ping" <<'EOF'
#!/bin/bash
count=1
while [ $# -gt 0 ]; do [ "$1" = "-c" ] && count=$2; shift; done
calls=$(( $(cat "$PING_CALLS" 2>/dev/null || echo 0) + 1 ))
echo $calls > "$PING_CALLS"
[ "$calls" -ge 2 ] || [ "$count" -ge 2 ]
EOF
chmod +x "$TMP/ping"
export PING_CALLS="$TMP/calls"

# Just the probe, not the whole script: flash_cubes.sh flashes when run.
eval "$(sed -n '/^ping_answers() {/,/^}/p' "$DIR/flash_cubes.sh")"
if ! declare -F ping_answers >/dev/null; then
    echo "FAIL: flash_cubes.sh has no ping_answers()"
    exit 1
fi

if PATH="$TMP:$PATH" ping_answers 192.0.2.1; then
    echo "PASS: a cube that drops the first probe is found"
else
    echo "FAIL: a cube that drops the first probe is reported unreachable"
    exit 1
fi
