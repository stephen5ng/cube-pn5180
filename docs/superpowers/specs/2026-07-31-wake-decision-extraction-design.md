# Wake-decision extraction and sleep fail-open fix — design

## Problem

`handleWakeUp()` in `src/main.cpp` (lines 1051–1161) decides, on every wake,
whether the cube goes back to sleep or wakes fully. That decision is tangled
with the hardware calls it depends on — `WiFi.status()`, `keepalive_mqtt.
connect()`, `enterSleepMode()` — so none of it is reachable from
`test/test_native/`, and a wrong branch only shows up on a physical bench test.

A wrong branch is already there. On a keep-alive timer wake, both failure
paths return without sleeping:

```cpp
if (WiFi.status() != WL_CONNECTED) {
  debugSend("wifi timeout on check-in");
  last_activity_time = millis();  // Reset auto-sleep timer
  return;
}
...
} else {
  debugSend("mqtt fail");
  // If MQTT failed, assume we should wake (safer default)
  last_activity_time = millis();  // Reset auto-sleep timer
}
```

Returning falls through to `setup()`, which raises `POWER_SWITCH_PIN` and
builds the `DisplayManager` — a full wake with the display lit. The only path
back to sleep is then the idle timeout in `loop()`,
`AUTO_SLEEP_TIMEOUT_MS` = 10 minutes. A cube with no reachable network
therefore spends ~10 minutes awake for every ~20 seconds
(`sleep_interval_s`) asleep, which is backwards for transport and storage:
the case where the display should stay dark is the one where it stays lit.

## Approach

Three commits: extract the decision, characterize it, then change it. The
extraction commit does not alter behavior, so the behavior change lands as a
diff that flips two expectations and two returns — reviewable on its own.

## 1. Extract the decision (no behavior change)

New pure function in `src/cube_utilities.h` / `.cpp`, alongside
`resolveAssignedSlot` and `parseAssignmentRecord`:

```cpp
enum WakeReason { WAKE_REASON_TIMER, WAKE_REASON_BUTTON, WAKE_REASON_OTHER };
enum WakeAction { WAKE_ACTION_STAY_ASLEEP, WAKE_ACTION_WAKE_FULL };

WakeAction resolveWakeAction(WakeReason wake_reason,
                             bool wifi_connected,
                             bool mqtt_connected,
                             bool has_slot_topic,
                             bool device_requests_sleep,
                             bool slot_requests_sleep);
```

`WakeReason` is our own enum rather than `esp_sleep_wakeup_cause_t` so the
function needs no ESP-IDF headers and compiles under `NATIVE_TESTING` like
the rest of `cube_utilities`.

The function only decides. Every side effect stays in `main.cpp`: mapping
`esp_sleep_get_wakeup_cause()` to a `WakeReason`, waiting for WiFi,
connecting the keep-alive MQTT client, subscribing and reading the retained
flags, clearing the retained flags on a full wake, resetting
`last_activity_time`, emitting `debugSend()` messages, and calling
`enterSleepMode()`.

`has_slot_topic` carries the existing distinction at lines 1121–1123: an
assigned cube (non-empty `slot_auto_sleep_topic`) obeys the slot flag so
`wake.sh` can wake it by clearing that topic; an unassigned cube has no slot
topic and obeys the device flag.

Body, matching today's behavior exactly:

```cpp
WakeAction resolveWakeAction(WakeReason wake_reason, bool wifi_connected,
                             bool mqtt_connected, bool has_slot_topic,
                             bool device_requests_sleep,
                             bool slot_requests_sleep) {
  if (wake_reason != WAKE_REASON_TIMER) return WAKE_ACTION_WAKE_FULL;
  if (!wifi_connected || !mqtt_connected) return WAKE_ACTION_WAKE_FULL;
  bool stay_asleep = has_slot_topic ? slot_requests_sleep
                                    : device_requests_sleep;
  return stay_asleep ? WAKE_ACTION_STAY_ASLEEP : WAKE_ACTION_WAKE_FULL;
}
```

`handleWakeUp()` becomes: gather the four booleans, call `resolveWakeAction`,
then act on the result. The MQTT connect and flag-read still only happen when
WiFi is up, and the flag-clearing publishes still only happen on a full wake
from a timer check-in — the ordering constraint at lines 1087–1090 (read the
payload before any `publish()`, because PubSubClient reuses one buffer) is
unaffected, since the callback still runs before the decision is made.

## 2. Characterization test (no behavior change)

Added to `test/test_native/test_esp32_utilities.cpp`, asserting today's
behavior including the bug. The two failure cases are marked in comments as
the behavior commit 3 changes, so a reader does not mistake them for intent.

| wake_reason | wifi | mqtt | has_slot_topic | device flag | slot flag | expected |
|---|---|---|---|---|---|---|
| BUTTON | – | – | – | – | – | WAKE_FULL |
| OTHER | – | – | – | – | – | WAKE_FULL |
| TIMER | false | – | – | – | – | WAKE_FULL *(bug)* |
| TIMER | true | false | – | – | – | WAKE_FULL *(bug)* |
| TIMER | true | true | true | false | true | STAY_ASLEEP |
| TIMER | true | true | false | true | false | STAY_ASLEEP |
| TIMER | true | true | true | true | false | WAKE_FULL |
| TIMER | true | true | false | false | true | WAKE_FULL |

The last two cover the slot-vs-device precedence: an assigned cube ignores
the device flag, an unassigned cube ignores the slot flag.

## 3. Fail-open fix (behavior change)

A keep-alive wake that cannot reach WiFi or MQTT goes back to sleep instead
of waking fully. In `resolveWakeAction`:

```cpp
if (!wifi_connected || !mqtt_connected) return WAKE_ACTION_STAY_ASLEEP;
```

and the two `WAKE_FULL *(bug)*` rows above become `STAY_ASLEEP`, with the
comments dropped.

`main.cpp` needs no further change: commit 1 already routed both failure
paths through the returned `WakeAction`, so `STAY_ASLEEP` now calls
`enterSleepMode()` the same way the flag-set path does. That call happens
before `display_manager` is allocated, so `enterSleepMode()` takes its
existing cheap path — no "sleep..." paint, no HUB75 teardown — identical in
cost to an ordinary keep-alive re-sleep.

Button wake is untouched: `WAKE_REASON_BUTTON` returns `WAKE_ACTION_WAKE_FULL`
regardless of network state, so a cube in a dead zone can always be woken by
hand for debugging.

Resulting behavior with no network: the cube sleeps continuously, waking for
a ~1s keep-alive attempt every `sleep_interval_s` (default 20s) with the
display never powered, instead of ~10 minutes awake per cycle.

## Testing

Per `CLAUDE.md`, in order, for each commit:

1. `~/.platformio/penv/bin/platformio test -e native`
2. `~/.platformio/penv/bin/platformio run -e v6`

Commit 3 additionally warrants a bench check: with WiFi off or the broker
down, confirm a sleeping cube's display stays dark across several
`sleep_interval_s` periods.

## Out of scope

- Any wider testability refactor of `main.cpp`. Only the wake decision moves.
- Retry counters, thresholds, or a distinct "shipping mode". A failed
  check-in re-sleeps on the first failure; the button is the override.
- The `ESP_SLEEP_WAKEUP_EXT0` and normal-boot paths, beyond mapping them to
  `WAKE_REASON_BUTTON` / `WAKE_REASON_OTHER`.
