# NFC Self-Test on Boot Implementation Plan

> **For Claude:** REQUIRED SUB-SKILL: Use superpowers:executing-plans to implement this plan task-by-task.

**Goal:** On every boot, run a quick NFC self-test and display the result on the HUB75 so a broken ESP32 GPIO or PN5180 is immediately visible.

**Architecture:** Before calling `setupNfcReader()` in `setup()`, read the BUSY pin state and run one timed `getInventory()` call. Display a one-line result via the existing `displayDebugMessage()`. No new files needed.

**Tech Stack:** Arduino/ESP32, existing PN5180ISO15693 library, existing DisplayManager.

---

### Task 1: Read BUSY pin before PN5180 init and display result

**Files:**
- Modify: `src/main.cpp` around line 1373 (between `displayDebugMessage("nfc...")` and `setupNfcReader()`)

**Step 1: Write the test first**

There are no native tests for hardware pin reads (requires ESP32). Skip unit test — verify manually by observing the HUB75 display on boot.

**Step 2: Add BUSY pin pre-check and timed NFC read**

Replace this block in `setup()`:

```cpp
display_manager->displayDebugMessage("nfc...");
debugPrintln(WiFi.macAddress().c_str());

debugPrintln("setting up nfc reader...");
setupNfcReader();
debugPrintln("nfc reader done");
```

With:

```cpp
debugPrintln(WiFi.macAddress().c_str());

// Self-test: check BUSY pin state before init (should be LOW)
pinMode(pn5180_busy_pin, INPUT);
bool busy_before_init = digitalRead(pn5180_busy_pin);

debugPrintln("setting up nfc reader...");
setupNfcReader();
debugPrintln("nfc reader done");

// Self-test: timed NFC read
uint8_t test_card_id[NFCID_LENGTH];
unsigned long nfc_test_start = micros();
readNfcCard(test_card_id);
unsigned long nfc_test_us = micros() - nfc_test_start;

char nfc_test_result[32];
if (busy_before_init) {
  snprintf(nfc_test_result, sizeof(nfc_test_result), "nfc:BUSY!");
} else if (nfc_test_us > 100000UL) {
  snprintf(nfc_test_result, sizeof(nfc_test_result), "nfc:SLOW %luus", nfc_test_us);
} else {
  snprintf(nfc_test_result, sizeof(nfc_test_result), "nfc:ok %luus", nfc_test_us);
}
display_manager->displayDebugMessage(nfc_test_result);
```

Note: `configurePins()` is called earlier in `setup()` (inside `setupWiFiConnection()` → MAC lookup), so `pn5180_busy_pin` is already set by this point.

**Step 3: Verify configurePins() is called before this point**

Check that `configurePins()` runs before line 1373. Grep for where it's called:

```bash
grep -n "configurePins" src/main.cpp
```

Expected: called during WiFi setup (MAC-based cube ID lookup), before the NFC block.

**Step 4: Build and verify compilation**

```bash
~/.platformio/penv/bin/platformio run -e v1
~/.platformio/penv/bin/platformio run -e v6
~/.platformio/penv/bin/platformio run -e v6_with_hall
```

Expected: all three environments compile with no errors.

**Step 5: Run native tests to check for regressions**

```bash
~/.platformio/penv/bin/platformio test -e native
```

Expected: all 36 tests pass.

**Step 6: Flash to one cube and observe HUB75 on boot**

Flash to cube 3 (v1, known healthy):

```bash
./tools/flash_cubes.sh 3
```

Reboot and watch the HUB75 display. Expected sequence:
```
2025.0305
wake:0
192.168.8.23
nfc:ok 210us   ← new line
```

The display stays up until the game sends a `/letter` command.

**Step 7: Test with a known-bad ESP32 (if available)**

Plug the bad cube 4 ESP32 into a working board and flash. Expected:
```
nfc:BUSY!      ← if GPIO 35 is stuck HIGH
```
or
```
nfc:SLOW 1012345us   ← if BUSY signal is unreliable
```

**Step 8: Commit**

```bash
git add src/main.cpp
git commit -m "feat: add NFC self-test result to HUB75 boot display"
```
