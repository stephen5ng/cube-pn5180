# Wake-decision extraction and sleep fail-open fix — implementation plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** A keep-alive timer wake that cannot reach WiFi or MQTT goes back to sleep instead of waking the cube fully with the display lit, and the decision that governs it is covered by native tests.

**Architecture:** Two new pieces in `src/cube_utilities.{h,cpp}` — a pure `resolveWakeAction()` that decides, and a `runWakeCheckIn()` that sequences the decision against a `WakeCheckInPorts` interface. `handleWakeUp()` in `src/main.cpp` shrinks to mapping the ESP wake cause and supplying a concrete `WakeCheckInPorts` over the existing hardware calls. Native tests drive `runWakeCheckIn` through a recording fake, so the wiring from a failed check-in to `enterSleepMode()` is testable off-device.

**Tech Stack:** C++14, PlatformIO, Unity (native test env), ESP32 Arduino core, PubSubClient.

**Design doc:** `docs/superpowers/specs/2026-07-31-wake-decision-extraction-design.md` (merged in 24c3de3). Read it before Task 1 — this plan implements it and does not restate its rationale.

## Global Constraints

- Native tests must compile under `-DNATIVE_TESTING` with `-std=c++14`. `src/cube_utilities.{h,cpp}` may not include ESP-IDF or Arduino headers outside the existing `#ifdef NATIVE_TESTING` guards.
- `platformio.ini`'s `[env:native]` has `build_src_filter` limited to `cube_utilities.cpp` and `cube_tags.cpp`. All natively-tested code goes in `cube_utilities.cpp`. Do not add `main.cpp` to that filter.
- Verification for every task, in this order (per `CLAUDE.md`):
  1. `~/.platformio/penv/bin/platformio test -e native`
  2. `~/.platformio/penv/bin/platformio run -e v6`
- Comments describe the current state only. No "moved from…", no "previously…". Anything about future state is a `TODO`.
- Do not comment the obvious, and do not restate in a comment what the code already says.
- Commit granularity is feature-level. Each task below is exactly one commit.
- Work on a branch in a worktree under `.worktrees/`. The main checkout stays on `main`.

## File Structure

| File | Responsibility | Tasks |
|---|---|---|
| `src/cube_utilities.h` | Declares `WakeReason`, `WakeAction`, `SleepFlags`, `WakeCheckInPorts`, `resolveWakeAction`, `runWakeCheckIn` | 1, 2 |
| `src/cube_utilities.cpp` | Defines `resolveWakeAction` and `runWakeCheckIn` | 1, 2, 4 |
| `test/test_native/test_esp32_utilities.cpp` | Decision table, `FakeWakeCheckInPorts`, wiring tests | 1, 2, 4 |
| `src/main.cpp` | `KeepAliveCheckInPorts` (hardware implementation), slimmed `handleWakeUp()`, `KEEPALIVE_WIFI_TIMEOUT_MS` | 3, 5 |

---

### Task 1: Pure wake decision

**Files:**
- Modify: `src/cube_utilities.h` (append near `resolveAssignedSlot`)
- Modify: `src/cube_utilities.cpp` (append)
- Test: `test/test_native/test_esp32_utilities.cpp`

**Interfaces:**
- Consumes: nothing.
- Produces: `enum WakeAction { WAKE_ACTION_STAY_ASLEEP, WAKE_ACTION_WAKE_FULL };` and
  `WakeAction resolveWakeAction(bool wifi_connected, bool mqtt_connected, bool has_slot_topic, bool device_requests_sleep, bool slot_requests_sleep);`

This is the check-in decision only. It takes no wake reason — a check-in only happens on a timer wake, and wake-reason dispatch lives in Task 2.

- [ ] **Step 1: Write the failing tests**

Add above `int main(void)` in `test/test_native/test_esp32_utilities.cpp`:

```cpp
void test_resolveWakeAction_network_failure_wakes_full() {
    // TODO(task 4): both become WAKE_ACTION_STAY_ASLEEP when the fail-open is fixed.
    TEST_ASSERT_EQUAL(WAKE_ACTION_WAKE_FULL,
                      resolveWakeAction(false, false, false, true, true));
    TEST_ASSERT_EQUAL(WAKE_ACTION_WAKE_FULL,
                      resolveWakeAction(true, false, false, true, true));
}

void test_resolveWakeAction_assigned_cube_obeys_slot_flag() {
    TEST_ASSERT_EQUAL(WAKE_ACTION_STAY_ASLEEP,
                      resolveWakeAction(true, true, true, false, true));
    TEST_ASSERT_EQUAL(WAKE_ACTION_WAKE_FULL,
                      resolveWakeAction(true, true, true, true, false));
}

void test_resolveWakeAction_unassigned_cube_obeys_device_flag() {
    TEST_ASSERT_EQUAL(WAKE_ACTION_STAY_ASLEEP,
                      resolveWakeAction(true, true, false, true, false));
    TEST_ASSERT_EQUAL(WAKE_ACTION_WAKE_FULL,
                      resolveWakeAction(true, true, false, false, true));
}
```

And register them inside `main()`, after the existing `RUN_TEST(test_makeMqttClientId_full_and_keepalive);`:

```cpp
    // Wake decision tests
    RUN_TEST(test_resolveWakeAction_network_failure_wakes_full);
    RUN_TEST(test_resolveWakeAction_assigned_cube_obeys_slot_flag);
    RUN_TEST(test_resolveWakeAction_unassigned_cube_obeys_device_flag);
```

- [ ] **Step 2: Run tests to verify they fail**

Run: `~/.platformio/penv/bin/platformio test -e native`
Expected: compile error — `'resolveWakeAction' was not declared in this scope`.

- [ ] **Step 3: Declare the enums and the function**

In `src/cube_utilities.h`, immediately after the `int resolveAssignedSlot(...)` declaration:

```cpp
enum WakeAction { WAKE_ACTION_STAY_ASLEEP, WAKE_ACTION_WAKE_FULL };

// The keep-alive check-in decision. Only a timer wake makes a check-in, so
// there is no wake-reason parameter; runWakeCheckIn() dispatches on that.
WakeAction resolveWakeAction(bool wifi_connected,
                             bool mqtt_connected,
                             bool has_slot_topic,
                             bool device_requests_sleep,
                             bool slot_requests_sleep);
```

- [ ] **Step 4: Implement**

In `src/cube_utilities.cpp`, after `resolveAssignedSlot`:

```cpp
WakeAction resolveWakeAction(bool wifi_connected, bool mqtt_connected,
                             bool has_slot_topic,
                             bool device_requests_sleep,
                             bool slot_requests_sleep) {
  if (!wifi_connected || !mqtt_connected) return WAKE_ACTION_WAKE_FULL;
  bool stay_asleep = has_slot_topic ? slot_requests_sleep
                                    : device_requests_sleep;
  return stay_asleep ? WAKE_ACTION_STAY_ASLEEP : WAKE_ACTION_WAKE_FULL;
}
```

- [ ] **Step 5: Run tests to verify they pass**

Run: `~/.platformio/penv/bin/platformio test -e native`
Expected: PASS, all tests.

- [ ] **Step 6: Verify the firmware still builds**

Run: `~/.platformio/penv/bin/platformio run -e v6`
Expected: SUCCESS.

- [ ] **Step 7: Commit**

```bash
git add src/cube_utilities.h src/cube_utilities.cpp test/test_native/test_esp32_utilities.cpp
git commit -m "Extract the keep-alive check-in decision into resolveWakeAction

Characterizes today's behavior, fail-open included: a check-in that
cannot reach WiFi or MQTT still resolves to a full wake. The two
network-failure assertions are marked with a TODO and flip when the
fail-open is fixed."
```

---

### Task 2: Orchestration seam

**Files:**
- Modify: `src/cube_utilities.h`
- Modify: `src/cube_utilities.cpp`
- Test: `test/test_native/test_esp32_utilities.cpp`

**Interfaces:**
- Consumes: `resolveWakeAction(...)` and `WakeAction` from Task 1.
- Produces:
  - `enum WakeReason { WAKE_REASON_TIMER, WAKE_REASON_BUTTON, WAKE_REASON_OTHER };`
  - `struct SleepFlags { bool device_requests_sleep; bool slot_requests_sleep; };`
  - `struct WakeCheckInPorts` with `awaitWifi()`, `connectMqtt()`, `hasSlotTopic()`, `readSleepFlags()`, `clearSleepFlags()`, `enterSleep()`, `stayAwake()`
  - `void runWakeCheckIn(WakeReason wake_reason, WakeCheckInPorts& ports);`

`enterSleep()` never returns on hardware. Every call site is followed immediately by `return`, and the fake fails the test if any port is touched afterwards.

- [ ] **Step 1: Write the failing tests**

Add above `int main(void)`:

```cpp
struct FakeWakeCheckInPorts : public WakeCheckInPorts {
  bool wifi_result = true;
  bool mqtt_result = true;
  bool slot_topic_result = false;
  SleepFlags flags = {false, false};

  char calls[128] = "";
  bool called_after_sleep = false;

  void record(const char* name) {
    if (strstr(calls, "enterSleep,") != NULL) called_after_sleep = true;
    strncat(calls, name, sizeof(calls) - strlen(calls) - 1);
    strncat(calls, ",", sizeof(calls) - strlen(calls) - 1);
  }
  bool sawCall(const char* name) {
    char needle[32];
    snprintf(needle, sizeof(needle), "%s,", name);
    return strstr(calls, needle) != NULL;
  }

  bool awaitWifi() { record("awaitWifi"); return wifi_result; }
  bool connectMqtt() { record("connectMqtt"); return mqtt_result; }
  bool hasSlotTopic() { record("hasSlotTopic"); return slot_topic_result; }
  SleepFlags readSleepFlags() { record("readSleepFlags"); return flags; }
  void clearSleepFlags() { record("clearSleepFlags"); }
  void enterSleep() { record("enterSleep"); }
  void stayAwake() { record("stayAwake"); }
};

void test_runWakeCheckIn_wifi_timeout() {
    FakeWakeCheckInPorts ports;
    ports.wifi_result = false;
    runWakeCheckIn(WAKE_REASON_TIMER, ports);
    // TODO(task 4): becomes enterSleep when the fail-open is fixed.
    TEST_ASSERT_TRUE(ports.sawCall("stayAwake"));
    TEST_ASSERT_FALSE(ports.sawCall("enterSleep"));
    TEST_ASSERT_FALSE(ports.sawCall("connectMqtt"));
    TEST_ASSERT_FALSE(ports.sawCall("hasSlotTopic"));
    TEST_ASSERT_FALSE(ports.sawCall("readSleepFlags"));
    TEST_ASSERT_FALSE(ports.called_after_sleep);
}

void test_runWakeCheckIn_mqtt_connect_fails() {
    FakeWakeCheckInPorts ports;
    ports.mqtt_result = false;
    runWakeCheckIn(WAKE_REASON_TIMER, ports);
    // TODO(task 4): becomes enterSleep when the fail-open is fixed.
    TEST_ASSERT_TRUE(ports.sawCall("stayAwake"));
    TEST_ASSERT_FALSE(ports.sawCall("enterSleep"));
    TEST_ASSERT_FALSE(ports.sawCall("hasSlotTopic"));
    TEST_ASSERT_FALSE(ports.sawCall("readSleepFlags"));
    TEST_ASSERT_FALSE(ports.sawCall("clearSleepFlags"));
    TEST_ASSERT_FALSE(ports.called_after_sleep);
}

void test_runWakeCheckIn_flag_set_sleeps_without_clearing() {
    FakeWakeCheckInPorts ports;
    ports.flags.device_requests_sleep = true;
    runWakeCheckIn(WAKE_REASON_TIMER, ports);
    TEST_ASSERT_TRUE(ports.sawCall("enterSleep"));
    TEST_ASSERT_FALSE(ports.sawCall("clearSleepFlags"));
    TEST_ASSERT_FALSE(ports.sawCall("stayAwake"));
    TEST_ASSERT_FALSE(ports.called_after_sleep);
}

void test_runWakeCheckIn_flag_clear_clears_then_stays_awake() {
    FakeWakeCheckInPorts ports;
    runWakeCheckIn(WAKE_REASON_TIMER, ports);
    TEST_ASSERT_EQUAL_STRING(
        "awaitWifi,connectMqtt,hasSlotTopic,readSleepFlags,clearSleepFlags,stayAwake,",
        ports.calls);
    TEST_ASSERT_FALSE(ports.sawCall("enterSleep"));
}

void test_runWakeCheckIn_button_wake_ignores_network() {
    FakeWakeCheckInPorts ports;
    ports.wifi_result = false;
    runWakeCheckIn(WAKE_REASON_BUTTON, ports);
    TEST_ASSERT_EQUAL_STRING("stayAwake,", ports.calls);
}

void test_runWakeCheckIn_normal_boot_touches_nothing() {
    FakeWakeCheckInPorts ports;
    runWakeCheckIn(WAKE_REASON_OTHER, ports);
    TEST_ASSERT_EQUAL_STRING("", ports.calls);
}
```

Register in `main()` after the Task 1 registrations:

```cpp
    RUN_TEST(test_runWakeCheckIn_wifi_timeout);
    RUN_TEST(test_runWakeCheckIn_mqtt_connect_fails);
    RUN_TEST(test_runWakeCheckIn_flag_set_sleeps_without_clearing);
    RUN_TEST(test_runWakeCheckIn_flag_clear_clears_then_stays_awake);
    RUN_TEST(test_runWakeCheckIn_button_wake_ignores_network);
    RUN_TEST(test_runWakeCheckIn_normal_boot_touches_nothing);
```

The `TEST_ASSERT_FALSE(sawCall(...))` lines are the point of this task: they fail if a `WakeAction` is wired to the wrong effect, which an assertion on the returned value alone cannot catch.

- [ ] **Step 2: Run tests to verify they fail**

Run: `~/.platformio/penv/bin/platformio test -e native`
Expected: compile error — `'WakeCheckInPorts' has not been declared`.

- [ ] **Step 3: Declare the seam**

In `src/cube_utilities.h`, after the `resolveWakeAction` declaration:

```cpp
enum WakeReason { WAKE_REASON_TIMER, WAKE_REASON_BUTTON, WAKE_REASON_OTHER };

struct SleepFlags { bool device_requests_sleep; bool slot_requests_sleep; };

struct WakeCheckInPorts {
  virtual ~WakeCheckInPorts() {}
  virtual bool awaitWifi() = 0;
  virtual bool connectMqtt() = 0;
  virtual bool hasSlotTopic() = 0;
  virtual SleepFlags readSleepFlags() = 0;
  virtual void clearSleepFlags() = 0;
  // Disconnects an active keep-alive client, then sleeps. Does not return on
  // hardware, so every call site returns immediately after it.
  virtual void enterSleep() = 0;
  virtual void stayAwake() = 0;
};

void runWakeCheckIn(WakeReason wake_reason, WakeCheckInPorts& ports);
```

- [ ] **Step 4: Implement**

In `src/cube_utilities.cpp`, after `resolveWakeAction`:

```cpp
void runWakeCheckIn(WakeReason wake_reason, WakeCheckInPorts& ports) {
  if (wake_reason == WAKE_REASON_BUTTON) { ports.stayAwake(); return; }
  if (wake_reason != WAKE_REASON_TIMER) return;

  bool wifi = ports.awaitWifi();
  bool mqtt = wifi && ports.connectMqtt();
  bool has_slot_topic = false;
  SleepFlags flags = {false, false};
  if (mqtt) {
    has_slot_topic = ports.hasSlotTopic();
    flags = ports.readSleepFlags();
  }

  WakeAction action = resolveWakeAction(wifi, mqtt, has_slot_topic,
                                        flags.device_requests_sleep,
                                        flags.slot_requests_sleep);
  if (action == WAKE_ACTION_STAY_ASLEEP) { ports.enterSleep(); return; }
  if (mqtt) ports.clearSleepFlags();
  ports.stayAwake();
}
```

`WAKE_REASON_OTHER` returns without calling `stayAwake()` because today's normal-boot branch (`src/main.cpp:1158-1160`) only logs — `last_activity_time` stays 0, which disables the auto-sleep guard at `src/main.cpp:1876` until MQTT connects. Only the EXT0 branch resets it, so only `WAKE_REASON_BUTTON` calls `stayAwake()`.

- [ ] **Step 5: Run tests to verify they pass**

Run: `~/.platformio/penv/bin/platformio test -e native`
Expected: PASS, all tests.

- [ ] **Step 6: Verify the firmware still builds**

Run: `~/.platformio/penv/bin/platformio run -e v6`
Expected: SUCCESS.

- [ ] **Step 7: Commit**

```bash
git add src/cube_utilities.h src/cube_utilities.cpp test/test_native/test_esp32_utilities.cpp
git commit -m "Add a port seam so the check-in wiring is natively testable

runWakeCheckIn sequences the decision against WakeCheckInPorts, so the
path from a failed check-in to enterSleep is under test rather than
only reachable on a bench. Nothing calls it yet."
```

---

### Task 3: Route `handleWakeUp()` through the seam

**Files:**
- Modify: `src/main.cpp:1051-1161` (`handleWakeUp`), plus a new class above it

**Interfaces:**
- Consumes: `WakeReason`, `WakeCheckInPorts`, `SleepFlags`, `runWakeCheckIn` from Task 2.
- Produces: nothing for later tasks beyond a `handleWakeUp()` whose behavior is unchanged.

No behavior change. There is no native test for this task — `main.cpp` is excluded from `[env:native]`'s `build_src_filter`, and adding it is out of scope. The seam is what makes the *decision* testable; this task's correctness rests on the port bodies being lifted verbatim from the existing branches plus the `-e v6` build. Review it as a transcription, line against line, against the pre-change `handleWakeUp()`.

- [ ] **Step 1: Add the hardware ports class**

Insert immediately above `void handleWakeUp()` in `src/main.cpp`:

```cpp
class KeepAliveCheckInPorts : public WakeCheckInPorts {
 public:
  KeepAliveCheckInPorts() : mqtt_(tcp_) {
    StoredSlot stored = loadStoredSlot();
    device_topic_ = "cube/device/" + mac_nocolons + "/auto_sleep";
    slot_topic_ = stored.slot > 0
        ? "cube/" + String(stored.slot) + "/auto_sleep"
        : String("");
    mqtt_.setServer(MQTT_SERVER_PI, MQTT_PORT);
    mqtt_.setSocketTimeout(MQTT_SOCKET_TIMEOUT_S);
  }

  // setupWiFiConnection() fired a non-blocking WiFi.begin(); wait for
  // association before touching MQTT, or an ordinary association delay looks
  // identical to a real MQTT failure and fail-opens to a full wake on every
  // check-in, defeating the keep-alive pulse.
  bool awaitWifi() {
    unsigned long wifi_wait_start = millis();
    while (WiFi.status() != WL_CONNECTED &&
           millis() - wifi_wait_start < WIFI_CONNECT_ATTEMPT_TIMEOUT_MS) {
      delay(10);
    }
    if (WiFi.status() != WL_CONNECTED) {
      debugSend("wifi timeout on check-in");
      return false;
    }
    return true;
  }

  bool connectMqtt() {
    String client_id = makeMqttClientId(WiFi.macAddress(), "-ka");
    if (!mqtt_.connect(client_id.c_str())) {
      debugSend("mqtt fail");
      return false;
    }
    debugSend("mqtt ok");
    return true;
  }

  bool hasSlotTopic() { return !slot_topic_.isEmpty(); }

  SleepFlags readSleepFlags() {
    // IMPORTANT: Read payload BEFORE any publish() calls — PubSubClient reuses
    // its internal buffer for both incoming and outgoing messages, so
    // publishing inside the callback overwrites the payload bytes.
    mqtt_.setCallback([this](char* topic, byte* payload, unsigned int length) {
      bool requested = length == 1 && payload[0] == '1';
      if (device_topic_ == topic) {
        flags_.device_requests_sleep = requested;
      } else if (slot_topic_ == topic) {
        flags_.slot_requests_sleep = requested;
      }
    });

    String status_topic = "cube/device/" + mac_nocolons + "/status";
    mqtt_.publish(status_topic.c_str(), "keep-alive");

    mqtt_.subscribe(device_topic_.c_str());
    if (hasSlotTopic()) {
      mqtt_.subscribe(slot_topic_.c_str());
    }

    // Wait for the retained message to arrive (also the WiFi-active dwell)
    unsigned long check_start = millis();
    while (millis() - check_start < KEEPALIVE_CHECKIN_WINDOW_MS) {
      mqtt_.loop();
      delay(10);
    }

    char dbg[64];
    snprintf(dbg, sizeof(dbg), "flags device=%d slot=%d",
             flags_.device_requests_sleep, flags_.slot_requests_sleep);
    debugSend(dbg);
    return flags_;
  }

  void clearSleepFlags() {
    mqtt_.publish(device_topic_.c_str(), "", true);
    if (hasSlotTopic()) {
      mqtt_.publish(slot_topic_.c_str(), "", true);
    }
    delay(100);
    mqtt_.disconnect();
  }

  void enterSleep() {
    mqtt_.disconnect();
    debugSend("sleep again");
    enterSleepMode();
  }

  void stayAwake() {
    last_activity_time = millis();
    debugSend("WAKE FULL - staying awake");
    Serial.println("Waking fully - continuing setup");
  }

 private:
  WiFiClient tcp_;
  PubSubClient mqtt_;
  String device_topic_;
  String slot_topic_;
  SleepFlags flags_ = {false, false};
};
```

- [ ] **Step 2: Replace the body of `handleWakeUp()`**

Replace everything between `void handleWakeUp() {` and its closing brace with:

```cpp
  esp_sleep_wakeup_cause_t wakeup_reason = esp_sleep_get_wakeup_cause();

  WakeReason reason = WAKE_REASON_OTHER;
  if (wakeup_reason == ESP_SLEEP_WAKEUP_TIMER) {
    reason = WAKE_REASON_TIMER;
    debugSend("timer wake");
  } else if (wakeup_reason == ESP_SLEEP_WAKEUP_EXT0) {
    reason = WAKE_REASON_BUTTON;
    debugPrintln("Woken by external signal (Pin 0 released)");
  } else {
    debugPrintln("Normal boot - staying awake");
  }

  KeepAliveCheckInPorts ports;
  runWakeCheckIn(reason, ports);
```

Two deliberate differences from the old text, both debug-only:

- The old EXT0 branch printed `"Pin 0 wake-up detected - staying awake"`; the button path now goes through `stayAwake()`, which prints `"Waking fully - continuing setup"` instead.
- The old code emitted `"stay_asleep=%d"`; `readSleepFlags()` emits `"flags device=%d slot=%d"`, which distinguishes the two flags.

Constructing `KeepAliveCheckInPorts` on a BUTTON or OTHER wake costs one `loadStoredSlot()` NVS read that the old code did not do. It is off the timer path, where the power budget matters, and it keeps the construction site single. If a bench check shows it delaying button wake noticeably, move construction inside a `reason == WAKE_REASON_TIMER` block.

- [ ] **Step 3: Verify the firmware builds**

Run: `~/.platformio/penv/bin/platformio run -e v6`
Expected: SUCCESS. Watch for `String` vs `const char*` mismatches on the callback comparison and for `enterSleepMode()` / `loadStoredSlot()` needing forward declarations above the class — if either is declared below, move the class down rather than adding a new forward declaration.

- [ ] **Step 4: Verify the native tests still pass**

Run: `~/.platformio/penv/bin/platformio test -e native`
Expected: PASS, unchanged from Task 2.

- [ ] **Step 5: Bench check — behavior is unchanged**

Flash one cube using the `flashing-esp32-cubes` skill, not a raw `platformio upload`. Verify the flash landed by reading the retained `cube/<n>/version` topic — espota reports failures it did not have.

1. `tools/sleep.sh <n>` — cube sleeps, display dark.
2. `tools/wake.sh <n>` — cube wakes within ~1 check-in interval, display lit.
3. Press the button on a sleeping cube — it wakes.

Expected: identical to before this task. If any of the three regresses, this task is wrong; do not proceed to Task 4.

- [ ] **Step 6: Commit**

```bash
git add src/main.cpp
git commit -m "Route handleWakeUp through the check-in seam

KeepAliveCheckInPorts holds the hardware calls; handleWakeUp maps the
ESP wake cause and hands off to runWakeCheckIn. Behavior is unchanged
apart from two debug strings."
```

---

### Task 4: Fix the fail-open

**Files:**
- Modify: `src/cube_utilities.cpp` (`resolveWakeAction`)
- Test: `test/test_native/test_esp32_utilities.cpp`

**Interfaces:**
- Consumes: everything from Tasks 1–3.
- Produces: no signature change. `resolveWakeAction` returns `WAKE_ACTION_STAY_ASLEEP` when WiFi or MQTT is unreachable.

- [ ] **Step 1: Flip the test expectations**

In `test_resolveWakeAction_network_failure_wakes_full`, rename it to
`test_resolveWakeAction_network_failure_stays_asleep`, drop the `TODO`, and
change both assertions to `WAKE_ACTION_STAY_ASLEEP`:

```cpp
void test_resolveWakeAction_network_failure_stays_asleep() {
    TEST_ASSERT_EQUAL(WAKE_ACTION_STAY_ASLEEP,
                      resolveWakeAction(false, false, false, true, true));
    TEST_ASSERT_EQUAL(WAKE_ACTION_STAY_ASLEEP,
                      resolveWakeAction(true, false, false, true, true));
}
```

Update its `RUN_TEST` line to the new name.

In `test_runWakeCheckIn_wifi_timeout` and `test_runWakeCheckIn_mqtt_connect_fails`, drop the `TODO` comments and swap the first two assertions in each:

```cpp
    TEST_ASSERT_TRUE(ports.sawCall("enterSleep"));
    TEST_ASSERT_FALSE(ports.sawCall("stayAwake"));
```

Leave every `TEST_ASSERT_FALSE(sawCall(...))` and the `called_after_sleep` assertion as they are.

- [ ] **Step 2: Run tests to verify they fail**

Run: `~/.platformio/penv/bin/platformio test -e native`
Expected: FAIL — three tests, on the flipped assertions.

- [ ] **Step 3: Change the decision**

In `src/cube_utilities.cpp`, one line in `resolveWakeAction`:

```cpp
  if (!wifi_connected || !mqtt_connected) return WAKE_ACTION_STAY_ASLEEP;
```

- [ ] **Step 4: Run tests to verify they pass**

Run: `~/.platformio/penv/bin/platformio test -e native`
Expected: PASS, all tests.

- [ ] **Step 5: Verify the firmware builds**

Run: `~/.platformio/penv/bin/platformio run -e v6`
Expected: SUCCESS.

- [ ] **Step 6: Bench check — the display stays dark with no network**

1. `tools/sleep.sh <n>`, confirm the cube is asleep.
2. Stop the MQTT broker (or power down the AP).
3. Watch the cube across at least three `sleep_interval_s` periods (~60 s).

Expected: the display never lights. Before this change it would light within one interval and stay lit for `AUTO_SLEEP_TIMEOUT_MS` (10 min).

4. Restore the network, press the button.

Expected: the cube wakes. Button wake must survive a dead network — that is the escape hatch.

- [ ] **Step 7: Commit**

```bash
git add src/cube_utilities.cpp test/test_native/test_esp32_utilities.cpp
git commit -m "Re-sleep when a keep-alive check-in cannot reach the network

A check-in that cannot reach WiFi or MQTT went on to a full wake with
the display lit, then sat there for AUTO_SLEEP_TIMEOUT_MS. It now goes
straight back to sleep. Button wake is unaffected."
```

---

### Task 5: Keep-alive WiFi timeout and association logging

**Files:**
- Modify: `src/main.cpp:181` (constant block), `KeepAliveCheckInPorts::awaitWifi`

**Interfaces:**
- Consumes: `KeepAliveCheckInPorts` from Task 3.
- Produces: `KEEPALIVE_WIFI_TIMEOUT_MS`, used only by `awaitWifi()`.

Task 4 leaves an unreachable AP costing 10 s awake per 20 s of sleep — a 33% awake duty cycle. This trims it to 13%.

- [ ] **Step 1: Add the constant**

In `src/main.cpp`, directly below `#define WIFI_CONNECT_ATTEMPT_TIMEOUT_MS 10000`:

```cpp
// Timer-wake check-in only. Must stay above real association time: below it,
// a cube that can reach the AP but associates slowly re-sleeps every cycle and
// wake.sh can never reach it. 3x the ~1s a static-IP association is expected
// to take; the "wifi assoc" debug line below is how that gets confirmed.
#define KEEPALIVE_WIFI_TIMEOUT_MS 3000UL
```

`WIFI_CONNECT_ATTEMPT_TIMEOUT_MS` keeps its 10,000 ms for the foreground connect in `loop()`. Do not change it.

- [ ] **Step 2: Use it, and log the measurement**

In `KeepAliveCheckInPorts::awaitWifi()`, change the timeout and add the log on both exits:

```cpp
  bool awaitWifi() {
    unsigned long wifi_wait_start = millis();
    while (WiFi.status() != WL_CONNECTED &&
           millis() - wifi_wait_start < KEEPALIVE_WIFI_TIMEOUT_MS) {
      delay(10);
    }
    char dbg[64];
    snprintf(dbg, sizeof(dbg), "wifi assoc %lums", millis() - wifi_wait_start);
    debugSend(dbg);
    if (WiFi.status() != WL_CONNECTED) {
      debugSend("wifi timeout on check-in");
      return false;
    }
    return true;
  }
```

- [ ] **Step 3: Verify the firmware builds**

Run: `~/.platformio/penv/bin/platformio run -e v6`
Expected: SUCCESS.

- [ ] **Step 4: Verify the native tests still pass**

Run: `~/.platformio/penv/bin/platformio test -e native`
Expected: PASS, unchanged from Task 4. This task touches no natively-tested code.

- [ ] **Step 5: Bench check — the fail-closed risk**

This is the check that matters. A too-short timeout makes a healthy cube unwakeable.

1. With the network healthy, `tools/sleep.sh <n>`, then watch UDP debug across several check-ins.

Expected: `wifi assoc NNNms` on every check-in, with `NNN` well under 3000. Record the worst value seen.

2. `tools/wake.sh <n>`.

Expected: the cube wakes within one or two check-in intervals.

3. Move the cube to the weakest-signal spot it will realistically occupy and repeat step 1.

Expected: still under 3000 ms. If any reading exceeds ~1500 ms, raise `KEEPALIVE_WIFI_TIMEOUT_MS` rather than shipping a cube that re-sleeps forever in that corner.

- [ ] **Step 6: Commit**

```bash
git add src/main.cpp
git commit -m "Cut the keep-alive WiFi wait to 3s and log association time

A check-in against an unreachable AP was waiting on the foreground
connect timeout, 10s against a 20s sleep interval. The association
time is now logged on every check-in so the constant can be tightened
against real data instead of an estimate."
```

---

## Verification of the whole change

After Task 5, on a cube left running overnight:

- Battery-powered, no broker reachable: the display never lights, and `wifi assoc` lines appear roughly every `sleep_interval_s`.
- Broker reachable, `auto_sleep` cleared: the cube wakes on its next check-in.
- Button wake works in both conditions.

## Out of scope

Carried from the design doc — do not let these creep in:

- Any wider testability refactor of `main.cpp`. Only the wake decision and its sequencing move.
- Retry counters, sleep-interval backoff, or a distinct "shipping mode".
- Lowering `WIFI_CONNECT_ATTEMPT_TIMEOUT_MS` for the foreground connect.
- The stale-`last_activity_time` bug on the normal-boot path (`main.cpp:176`, `:1360`, `:1876`). Task 2 preserves it deliberately; fixing it is its own change.
