# Auto-Sleep Feature Design

## Overview
Cubes automatically enter deep sleep after 10 minutes of MQTT inactivity to conserve power.

## Implementation Status
✅ **Partially implemented** - Core tracking added, race condition solution pending

### What's working:
- `last_mqtt_message_time` tracks when last MQTT message received
- `MqttHandlerWrapper` template automatically updates timestamp on all MQTT callbacks
- Auto-sleep check in `loop()` enters sleep mode after 10 minutes
- Timer resets on any MQTT message

### What's missing:
- **Retained message protocol** to handle race conditions (see below)
- **`wake.sh` update** to clear per-cube retained messages

## The Race Condition Problem

### Scenario: You want to play a game, but cube is about to auto-sleep

**Timeline:**
1. T=0: Cube has 10 seconds until auto-sleep (timer at 9m50s)
2. T=0: You run `wake.sh 2` → clears retained message, sends ping
3. T=0: **Ping is in-flight**
4. T=10: Cube's timer expires → auto-sleeps (sets retained `"auto"`, goes to sleep)
5. T=10: Ping arrives... but cube is already asleep, doesn't receive it
6. T=30: Cube wakes up on 20-second maintenance timer
7. Cube sees retained `"auto"` → clears it, but timer still expired
8. Cube goes back to sleep
9. **Your wake command is lost!** 😞

## Proposed Solution: Timestamp-Based Retained Messages

### Protocol:
- `cube/{id}/sleep` retained message stores state with timestamps
- Values:
  - `wake_<timestamp>` - Explicit wake command from `wake.sh`
  - `auto_<timestamp>` - Auto-sleep from inactivity
  - `1` - Manual sleep command
  - empty/absent - Normal awake state

### wake.sh behavior:
```bash
TIMESTAMP=$(date +%s%N | cut -b1-13)  # milliseconds
mosquitto_pub -h $MQTT_HOST -t "cube/$CUBE_ID/sleep" -m "wake_$TIMESTAMP" --retain
mosquitto_pub -h $MQTT_HOST -t "cube/$CUBE_ID/ping" -m "wake"
```

### ESP32 auto-sleep logic:
```cpp
// Before entering auto-sleep, check retained message
String current = getRetained("cube/{id}/sleep");

// Parse timestamp if present
if (current.startsWith("wake_")) {
  uint64_t wake_time = current.substring(5).toInt();
  uint64_t now = millis();

  // If wake command is recent (within 30 seconds), DON'T auto-sleep
  if (now - wake_time < 30000) {
    Serial.println("Recent wake command received, canceling auto-sleep");
    return;
  }
}

// No recent wake command, proceed with auto-sleep
mqtt_client.publish("cube/{id}/sleep", "auto", true);
enterSleepMode();
```

### ESP32 wake-up logic (in `onConnectionEstablished()`):
```cpp
String sleep_reason = getRetained("cube/{id}/sleep");

if (sleep_reason.startsWith("auto")) {
  // We auto-slept, clear the marker but DON'T reset timer
  mqtt_client.publish("cube/{id}/sleep", "", true);
  // Timer is still at 10+ minutes, will sleep after this wake cycle
} else {
  // Normal wake, reset timer
  last_mqtt_message_time = millis();
}
```

### On any MQTT message:
```cpp
// Clear any sleep state when cube receives messages
mqtt_client.publish("cube/{id}/sleep", "", true);
```

## Alternative: Simpler Timestamp Check

Instead of parsing timestamps in retained messages, use **RTC memory** on the ESP32:

```cpp
// In enterSleepMode(), before sleeping:
RTC_DATA_ATTR static uint64_t last_auto_sleep_attempt = 0;
last_auto_sleep_attempt = millis();

// In auto-sleep check:
if (millis() - last_auto_sleep_attempt < 30000) {
  // We just tried to auto-sleep recently, wait longer
  return;
}
```

**But this doesn't solve the race** because wake.sh can't update RTC memory.

## Open Questions

1. **Should auto-sleep use a global `cube/sleep` or per-cube `cube/{id}/sleep`?**
   - Global affects all cubes (presumptuous for one cube's inactivity)
   - Per-cube is more appropriate for auto-sleep

2. **What timestamp threshold?** 30 seconds? 1 minute?
   - Needs to be longer than the MQTT message delivery time
   - Short enough that auto-sleep still works effectively

3. **Should we implement the full timestamp solution, or is there a simpler approach?**

## Files to Modify

1. **src/main.cpp**
   - Add retained message check before auto-sleep
   - Update `onConnectionEstablished()` to check retained message on wake
   - Clear retained message on MQTT message receipt

2. **tools/wake.sh**
   - Add timestamp-based wake message
   - Support per-cube waking
   - Send ping to reset timer

3. **tools/sleep.sh**
   - Consider whether this should also use timestamps

## Testing Scenarios

1. **Normal auto-sleep**: No messages for 10 minutes → sleep
2. **Wake during auto-sleep**: Run `wake.sh` while cube is asleep → cube wakes and stays awake
3. **Race condition**: Run `wake.sh` 1 second before auto-sleep → cube sees wake timestamp, cancels sleep
4. **Per-cube isolation**: Cube 2 auto-sleeps doesn't affect cube 3
5. **Global sleep**: `sleep.sh` still works to put all cubes to sleep

## References

- Discussion on 2025-03-03 about auto-sleep race conditions
- ESP32 retained message behavior: Messages persist across reboots/sleep
- MQTT QoS: Retained messages are delivered to clients when they subscribe
