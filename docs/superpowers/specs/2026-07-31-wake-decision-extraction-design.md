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

Four commits: extract the decision behind a testable seam, characterize it,
change the decision, then shorten the keep-alive WiFi timeout. The extraction
commit does not alter behavior. The two behavior changes are independent of
each other and land as separate diffs.

## 1. Extract the decision behind a port seam (no behavior change)

Two pieces land together, because the seam is what makes the decision worth
extracting: a pure decision function, and an orchestrator that wires the
decision to its side effects through an interface the native tests can fake.

### 1a. The pure decision

New in `src/cube_utilities.h` / `.cpp`, alongside `resolveAssignedSlot` and
`parseAssignmentRecord`:

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

### 1b. The orchestration seam

A decision function alone would leave the regression untested: the bug is the
early `return` in `handleWakeUp()`, and a resolver-only test passes whether or
not that return is still there and whether or not the returned `WakeAction` is
wired to the right side effect. So the sequencing moves out of `main.cpp` too,
behind a narrow port interface:

```cpp
struct SleepFlags { bool device_requests_sleep; bool slot_requests_sleep; };

struct WakeCheckInPorts {
  virtual ~WakeCheckInPorts() {}
  virtual bool awaitWifi() = 0;        // blocks up to KEEPALIVE_WIFI_TIMEOUT_MS
  virtual bool connectMqtt() = 0;
  virtual bool hasSlotTopic() = 0;
  virtual SleepFlags readSleepFlags() = 0;  // status publish, subscribe, dwell
  virtual void clearSleepFlags() = 0;       // retained-flag clear + disconnect
  virtual void enterSleep() = 0;            // does not return on hardware
  virtual void stayAwake() = 0;             // resets last_activity_time
};

void runWakeCheckIn(WakeReason wake_reason, WakeCheckInPorts& ports);
```

Seven members, chosen so each is either a hardware call or an assertable
effect. `debugSend()` is deliberately not a port — nothing asserts on it, and
`main.cpp`'s implementations keep their existing debug lines.

`runWakeCheckIn` holds the sequence and nothing else:

```cpp
void runWakeCheckIn(WakeReason wake_reason, WakeCheckInPorts& ports) {
  if (wake_reason != WAKE_REASON_TIMER) { ports.stayAwake(); return; }

  bool wifi = ports.awaitWifi();
  bool mqtt = wifi && ports.connectMqtt();
  bool has_slot_topic = false;
  SleepFlags flags = {false, false};
  if (mqtt) {
    has_slot_topic = ports.hasSlotTopic();
    flags = ports.readSleepFlags();
  }

  WakeAction action = resolveWakeAction(wake_reason, wifi, mqtt,
                                        has_slot_topic,
                                        flags.device_requests_sleep,
                                        flags.slot_requests_sleep);
  if (action == WAKE_ACTION_STAY_ASLEEP) { ports.enterSleep(); return; }
  if (mqtt) ports.clearSleepFlags();
  ports.stayAwake();
}
```

`hasSlotTopic()` sits under the `mqtt` guard because `resolveWakeAction`
short-circuits on `!wifi_connected || !mqtt_connected` before it consults
`has_slot_topic` — reading it on a failure path would be an `loadStoredSlot()`
NVS read whose answer is discarded, on the exact path this design is trying to
make cheap. It is called before `readSleepFlags()`, and the concrete
implementation reads `loadStoredSlot()` once and caches the slot topic for
both, since `readSleepFlags()` needs the same topic to subscribe to.

**`enterSleep()` contract.** On hardware `enterSleepMode()` ends in
`esp_deep_sleep_start()` and never returns; in a fake it does return. Every
`enterSleep()` call site is therefore immediately followed by `return`, so the
device and the test cannot diverge on what runs afterwards. The fake enforces
this: it records the call, and any subsequent port call fails the test.

`handleWakeUp()` shrinks to mapping `esp_sleep_get_wakeup_cause()` to a
`WakeReason`, constructing a concrete `WakeCheckInPorts` over the existing
hardware code, and calling `runWakeCheckIn`. All of today's side effects live
in that implementation: waiting for WiFi, connecting the keep-alive MQTT
client, subscribing and reading the retained flags, clearing them, resetting
`last_activity_time`, `debugSend()`, and `enterSleepMode()`.

The ordering constraint at lines 1087–1090 — read the payload before any
`publish()`, because PubSubClient reuses one buffer — is preserved inside
`readSleepFlags()`: the subscribe, the dwell loop, and the flag read all
happen there, and `clearSleepFlags()` is the only port that publishes to those
topics, called strictly afterwards.

## 2. Characterization tests (no behavior change)

Added to `test/test_native/test_esp32_utilities.cpp`, asserting today's
behavior including the bug. Cases that commit 3 changes are marked in comments
so a reader does not mistake them for intent.

### 2a. Decision table (`resolveWakeAction`)

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

### 2b. Wiring tests (`runWakeCheckIn` + recording fake)

A `FakeWakeCheckInPorts` records call order and returns scripted values. These
are the tests that fail if the early return survives or a `WakeAction` is
wired to the wrong effect — the resolver table above catches neither:

| case | asserts |
|---|---|
| TIMER, WiFi times out | `enterSleep` called; `connectMqtt`, `hasSlotTopic`, `readSleepFlags` **never** called; nothing called after `enterSleep` *(bug: today asserts `stayAwake`, not `enterSleep`)* |
| TIMER, WiFi up, MQTT connect fails | `enterSleep` called; `hasSlotTopic`, `readSleepFlags` and `clearSleepFlags` **never** called *(bug: today asserts `stayAwake`)* |
| TIMER, all up, flag set | `enterSleep` called; `clearSleepFlags` **never** called |
| TIMER, all up, flag clear | `clearSleepFlags` called, then `stayAwake`; `enterSleep` never called |
| BUTTON, WiFi down | `stayAwake` called; `awaitWifi`, `connectMqtt`, `enterSleep` **never** called |

The "never called" assertions are the point: they fail on a mis-wired action,
where an expectation on the returned value alone would not.

## 3. Fail-open fix (behavior change)

A keep-alive wake that cannot reach WiFi or MQTT goes back to sleep instead
of waking fully. In `resolveWakeAction`:

```cpp
if (!wifi_connected || !mqtt_connected) return WAKE_ACTION_STAY_ASLEEP;
```

The two `WAKE_FULL *(bug)*` rows in 2a become `STAY_ASLEEP`, and the two
bug-marked wiring tests in 2b flip from `stayAwake` to `enterSleep`, with the
comments dropped.

`main.cpp` needs no change: commit 1 routed both failure paths through the
returned `WakeAction`, so `STAY_ASLEEP` now calls `enterSleepMode()` the same
way the flag-set path does. That call happens before `display_manager` is
allocated, so `enterSleepMode()` takes its existing cheap path — no "sleep..."
paint, no HUB75 teardown — identical in cost to an ordinary keep-alive
re-sleep.

Button wake is untouched: `WAKE_REASON_BUTTON` returns `WAKE_ACTION_WAKE_FULL`
regardless of network state, so a cube in a dead zone can always be woken by
hand for debugging.

## 4. Keep-alive WiFi timeout (behavior change)

Commit 3 alone leaves the no-network duty cycle high. The timer-wake path at
lines 1062–1066 waits on `WIFI_CONNECT_ATTEMPT_TIMEOUT_MS`, which is 10,000 ms
(`src/main.cpp:181`) — sized for the foreground connect, not for a check-in
meant to be a brief pulse. Against a 20 s `sleep_interval_s`, an unreachable AP
costs ~10 s awake per cycle: a 33% awake duty cycle, dark display or not.

New constant, used **only** in the timer-wake check-in path:

```cpp
#define KEEPALIVE_WIFI_TIMEOUT_MS 3000UL
```

`WIFI_CONNECT_ATTEMPT_TIMEOUT_MS` stays at 10,000 ms for the foreground
connect in `loop()` — this commit does not lower that.

### Duty cycle, awake time per 20 s cycle

Deep-sleep interval is `sleep_interval_s` = 20 s in every row. Boot and
`setupWiFiConnection()` overhead is common to all rows and omitted; `t_assoc`
is actual WiFi association time.

| case | awake time | before | after |
|---|---|---|---|
| AP unreachable | WiFi timeout | 10,000 ms → **33%** | 3,000 ms → **13%** |
| WiFi up, broker down | `t_assoc` + `MQTT_SOCKET_TIMEOUT_S` (2 s, `main.cpp:184`) | `t_assoc` + 2,000 ms → ~**12%** | unchanged |
| normal check-in, flag set | `t_assoc` + `KEEPALIVE_CHECKIN_WINDOW_MS` (1 s, `main.cpp:127`) | `t_assoc` + 1,000 ms → ~**9%** | unchanged |

Only the first row changes; the other two are here because they bound how low
the constant may usefully go.

### Why 3,000 ms, and how it gets validated

The binding risk runs the other way: **`KEEPALIVE_WIFI_TIMEOUT_MS` must exceed
real association time**, or commit 3 turns a fail-open into a fail-closed — a
cube that can reach the AP but associates slowly re-sleeps every cycle, and
`wake.sh` can never wake it. That is worse than the bug being fixed, and it
only shows up in a weak-signal corner.

3,000 ms is a starting value, not a measured one. The cubes use static IPs (no
DHCP round-trip), so association is the entire cost, and 3 s is several times a
healthy association while still cutting the no-network duty cycle by two
thirds.

Validation goes in the firmware, not in this doc: commit 4 also makes the
check-in `debugSend` the measured association time,

```cpp
snprintf(dbg, sizeof(dbg), "wifi assoc %lums", millis() - wifi_wait_start);
```

on both the success and the timeout path. Field data from real cubes then
either justifies lowering the constant or shows it is already too tight. It
should not be lowered further without that data.

A failed check-in does **not** back off — `sleep_interval_s` stays 20 s — per
the out-of-scope list below. The table above is the cost of that choice,
stated so it reads as a choice rather than an oversight.

## Testing

Per `CLAUDE.md`, in order, for each commit:

1. `~/.platformio/penv/bin/platformio test -e native`
2. `~/.platformio/penv/bin/platformio run -e v6`

Commit 3 additionally warrants a bench check: with WiFi off or the broker
down, confirm a sleeping cube's display stays dark across several
`sleep_interval_s` periods.

Commit 4 warrants the opposite bench check, the fail-closed one: with the
network healthy, confirm `wake.sh` still wakes a sleeping cube, and read the
`wifi assoc NNNms` debug line to confirm real association time sits well under
3,000 ms.

## Out of scope

- Any wider testability refactor of `main.cpp`. Only the wake decision and its
  sequencing move.
- Retry counters, sleep-interval backoff, or a distinct "shipping mode". A
  failed check-in re-sleeps on the first failure at the unchanged interval;
  the button is the override.
- Lowering `WIFI_CONNECT_ATTEMPT_TIMEOUT_MS` for the foreground connect.
- A battery-life budget in mAh. No measured current figures for the v6 board
  exist in the repo, so the duty-cycle table above is stated as an awake-time
  fraction rather than as a runtime estimate.
- The `ESP_SLEEP_WAKEUP_EXT0` and normal-boot paths, beyond mapping them to
  `WAKE_REASON_BUTTON` / `WAKE_REASON_OTHER`.
