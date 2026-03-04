# Auto-Sleep Implementation Plan

> **For Claude:** REQUIRED SUB-SKILL: Use superpowers:executing-plans to implement this plan task-by-task.

**Goal:** Auto-sleep after 10 minutes of MQTT inactivity, with per-cube retained flag for maintenance-wake decisions, and a `sleep_now` command for immediate sleep.

**Architecture:** Add MQTT activity timestamp tracking in `loop()`. Before deep sleep, publish `cube/{id}/auto_sleep = "1"` (retained). On timer wake, subscribe to `cube/{id}/auto_sleep` instead of global `cube/sleep` — if "1" go back to sleep, if empty stay awake. Replace global `cube/sleep` topic with `cube/sleep_now` for manual sleep trigger.

**Tech Stack:** C++ (Arduino/ESP32), PlatformIO, MQTT (EspMQTTClient), mosquitto CLI tools

---

## Current State (understand before changing)

- `src/main.cpp:962` subscribes to global `cube/sleep` retained message in `handleWakeUp()`
- `src/main.cpp:1080` subscribes to `cube/sleep` for sleep commands in `onConnectionEstablished()`
- `src/main.cpp:1003` — `handleSleepCommand()` sleeps on message `"1"`
- `tools/sleep.sh` publishes `"1"` to `cube/sleep --retain`
- `tools/wake.sh` publishes `""` to `cube/sleep --retain` (clears it)
- No `last_mqtt_message_time` tracking exists yet

---

### Task 1: Add MQTT inactivity tracking

**Files:**
- Modify: `src/main.cpp`

**Step 1: Add RTC variable and constant**

Find the sleep state management block (~line 127) and add after it:

```cpp
// Auto-sleep inactivity tracking
#define AUTO_SLEEP_TIMEOUT_MS  600000UL  // 10 minutes
RTC_DATA_ATTR unsigned long last_mqtt_message_time = 0;
```

**Step 2: Reset timer on every MQTT message**

In `loop()`, find where `mqtt_client.loop()` is called and add after it:

```cpp
// Auto-sleep: enter deep sleep after 10 minutes of MQTT inactivity
if (last_mqtt_message_time > 0 &&
    (millis() - last_mqtt_message_time > AUTO_SLEEP_TIMEOUT_MS)) {
  debugSend("auto-sleep: inactivity timeout");
  publishAutoSleepFlag();
  enterSleepMode();
}
```

**Step 3: Add `publishAutoSleepFlag()` helper**

Add before `enterSleepMode()`:

```cpp
void publishAutoSleepFlag() {
  String flag_topic = mqtt_topic_cube + "/auto_sleep";
  mqtt_client.publish(flag_topic.c_str(), "1", true);
  delay(100);  // Give MQTT time to flush before sleep
}
```

**Step 4: Update timestamp on any MQTT message**

In `onConnectionEstablished()`, add a wildcard subscription that updates the timestamp. Add near the end of the subscription list:

```cpp
mqtt_client.subscribe(mqtt_topic_cube + "/#", [](const String& /*msg*/) {
  last_mqtt_message_time = millis();
});
```

> **Note:** EspMQTTClient fires all matching callbacks. The wildcard here only updates the timestamp; specific topic handlers still fire too.

**Step 5: Initialize timer on first MQTT connection**

In `onConnectionEstablished()`, after the subscription block, add:

```cpp
// Start inactivity timer from first MQTT connection
if (last_mqtt_message_time == 0) {
  last_mqtt_message_time = millis();
}
```

**Step 6: Compile to check for errors**

```bash
~/.platformio/penv/bin/platformio run -e v6
```

Expected: SUCCESS (no errors)

**Step 7: Commit**

```bash
git add src/main.cpp
git commit -m "feat: add MQTT inactivity tracking for auto-sleep"
```

---

### Task 2: Use per-cube retained flag in maintenance wake

**Files:**
- Modify: `src/main.cpp:925-978` (`handleWakeUp()` timer branch)

**Step 1: Change the subscribe topic**

In `handleWakeUp()`, find:

```cpp
keepalive_mqtt.subscribe("cube/sleep");
```

Replace with:

```cpp
String auto_sleep_topic = "cube/" + String(cube_identifier) + "/auto_sleep";
keepalive_mqtt.subscribe(auto_sleep_topic.c_str());
```

**Step 2: Clear the flag when waking fully**

In `handleWakeUp()`, in the `!stay_asleep` branch (around line 987), add before `is_sleep_mode = false`:

```cpp
// Clear the auto_sleep retained flag so cube stays awake after next reboot
keepalive_mqtt.publish(auto_sleep_topic.c_str(), "", true);
delay(100);
```

**Step 3: Compile to check**

```bash
~/.platformio/penv/bin/platformio run -e v6
```

Expected: SUCCESS

**Step 4: Commit**

```bash
git add src/main.cpp
git commit -m "feat: use per-cube retained flag for maintenance wake decision"
```

---

### Task 3: Replace `cube/sleep` command with `cube/sleep_now`

**Files:**
- Modify: `src/main.cpp:1080` (subscription in `onConnectionEstablished()`)
- Modify: `tools/sleep.sh`

**Step 1: Update firmware subscription**

In `onConnectionEstablished()`, find:

```cpp
mqtt_client.subscribe(String(MQTT_TOPIC_PREFIX_CUBE) + "sleep", handleSleepCommand);
```

Replace with:

```cpp
mqtt_client.subscribe(String(MQTT_TOPIC_PREFIX_CUBE) + "sleep_now", handleSleepNowCommand);
```

**Step 2: Rename the handler**

Find `handleSleepCommand` (~line 1003) and rename it + simplify — the new command always sleeps (no "1" check needed):

```cpp
void handleSleepNowCommand(const String& /*message*/) {
  debugSend("sleep_now cmd received");
  publishAutoSleepFlag();
  enterSleepMode();
}
```

> Remove the old `handleSleepCommand` entirely.

**Step 3: Update sleep.sh**

Replace contents of `tools/sleep.sh`:

```bash
#!/bin/bash -e

MQTT_HOST=${MQTT_SERVER:-192.168.8.247}
mosquitto_pub -h $MQTT_HOST -t "cube/sleep_now" -m "1"
```

> Note: no `--retain` — this is a fire-and-forget command, not a retained state flag.

**Step 4: Compile**

```bash
~/.platformio/penv/bin/platformio run -e v6
~/.platformio/penv/bin/platformio run -e v6_with_hall
```

Expected: both SUCCESS

**Step 5: Commit**

```bash
git add src/main.cpp tools/sleep.sh
git commit -m "feat: replace cube/sleep with cube/sleep_now command"
```

---

### Task 4: Update wake.sh to clear per-cube flag

**Files:**
- Modify: `tools/wake.sh`

**Step 1: Update wake.sh**

Replace contents of `tools/wake.sh`:

```bash
#!/bin/bash -e

MQTT_HOST=${MQTT_SERVER:-192.168.8.247}

if [ -z "$1" ]; then
  # Wake all cubes: clear flags for all known cube IDs
  for id in 1 2 3 4 5 6 11 12 13 14 15 16; do
    mosquitto_pub -h $MQTT_HOST -t "cube/$id/auto_sleep" -m "" --retain
  done
  echo "Cleared auto_sleep flag for all cubes"
else
  # Wake specific cube
  mosquitto_pub -h $MQTT_HOST -t "cube/$1/auto_sleep" -m "" --retain
  echo "Cleared auto_sleep flag for cube $1"
fi
```

**Step 2: Test wake.sh syntax**

```bash
bash -n tools/wake.sh
```

Expected: no output (no errors)

**Step 3: Commit**

```bash
git add tools/wake.sh
git commit -m "feat: update wake.sh to clear per-cube auto_sleep flag"
```

---

### Task 5: Run tests and validate compilation

**Step 1: Run native tests**

```bash
~/.platformio/penv/bin/platformio test -e native
```

Expected: all tests PASS

**Step 2: Compile all firmware variants**

```bash
~/.platformio/penv/bin/platformio run -e v6
~/.platformio/penv/bin/platformio run -e v6_with_hall
~/.platformio/penv/bin/platformio run -e v1
```

Expected: all SUCCESS

**Step 3: Commit if any test fixes needed**

```bash
git add -p
git commit -m "fix: address any test failures from auto-sleep changes"
```

---

### Task 6: Update design doc

**Files:**
- Modify: `docs/auto_sleep_design.md` — replace with summary of what was implemented

**Step 1: Replace the design doc**

Overwrite `docs/auto_sleep_design.md` with:

```markdown
# Auto-Sleep Feature

## Summary
Cubes auto-sleep after 10 minutes of MQTT inactivity.

## Mechanism
- `last_mqtt_message_time` tracks last MQTT activity (reset on any message to `cube/{id}/#`)
- After 10 minutes with no messages, cube publishes `cube/{id}/auto_sleep = "1"` (retained) then sleeps
- On 20-second maintenance wake, cube checks `cube/{id}/auto_sleep`:
  - `"1"` → go back to sleep
  - empty → stay awake (clear the flag)

## Commands
- `cube/sleep_now` — immediate sleep (any message, no retained)
- `tools/sleep.sh` — sends `cube/sleep_now`
- `tools/wake.sh [cube_id]` — clears `cube/{id}/auto_sleep` retained flag

## Wake Protocol
Run `wake.sh <id>` to wake a specific cube, or `wake.sh` to wake all cubes.
The cube will stay awake on next maintenance tick once the flag is cleared.
```

**Step 2: Commit**

```bash
git add docs/auto_sleep_design.md
git commit -m "docs: update auto-sleep design doc to reflect implementation"
```

---

## Manual Testing Checklist

After flashing, verify:

1. **Auto-sleep**: Leave cube idle 10 minutes → should sleep (check `cube/{id}/debug` messages)
2. **Maintenance wake**: After auto-sleep, cube should wake every 20s, check `cube/{id}/auto_sleep`, go back to sleep
3. **Wake**: Run `./tools/wake.sh <id>` → cube stays awake on next maintenance tick
4. **sleep_now**: Run `./tools/sleep.sh` → cube sleeps immediately
5. **Timer reset**: Send any MQTT message → 10-minute timer resets

## MQTT monitoring during test

```bash
mosquitto_sub -h 192.168.8.247 -t "cube/2/#" -v
```
