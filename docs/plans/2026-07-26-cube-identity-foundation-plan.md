# Cube Physical-Identity Foundation Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Give every physical cube a unique static IP octet and a unique MQTT client ID, both derived from its MAC and independent of its logical slot — the foundation the dynamic cube pool needs so a returning cube and its replacement never collide — and guard the address table so a typo can't silently reintroduce a collision.

**Architecture:** Add an `ip_octet` field to the compiled `CubeMacEntry` table (today the IP is `cube_id + 20`, and backups deliberately share a `cube_id` with their primary, so two physical cubes compute the same address). Derive the static IP from that field, fail closed on an unknown MAC instead of sharing `.20`, and add a host-side validator (CI) that asserts the production table's MACs and octets are globally unique. Separately, derive both MQTT client IDs from the MAC so an old holder and its replacement do not evict each other at the broker. This is Rollout Step 1 of the design in [docs/plans/2026-07-25-dynamic-cube-pool.md](2026-07-25-dynamic-cube-pool.md); it touches no server code.

**Tech Stack:** C++ (Arduino/PlatformIO, ESP32), Unity native unit tests, Python 3 (host-side table validator), EspMQTTClient (full client), PubSubClient (maintenance/keepalive client).

## Global Constraints

- Static IPs are kept by design (fast power-bounce reconnect, no DHCP dependency). Do not introduce DHCP.
- Primaries keep their current address (`cube_id + 20`, i.e. `.21`–`.26` and `.31`–`.36`), so this change is address-preserving for currently-deployed hardware. Only the six backups get new octets (`.41`–`.46`), which are inert until a backup is physically deployed (see Deferred prerequisites).
- The native test MAC table (`#ifdef NATIVE_TESTING`) uses stable synthetic values that must never be changed to match hardware; only add the new field to its existing rows.
- Any change to the `CubeMacEntry` struct must update BOTH table initializers (test and production) or the build breaks.
- Commands: native tests `~/.platformio/penv/bin/platformio test -e native`; hardware compile `~/.platformio/penv/bin/platformio run -e v1 -e v6 -e v6_with_hall -e v6_with_hall_analog` (the real environments in `platformio.ini`; `esp32dev` is the `board` value, not an environment). Table validator `python3 tools/validate_mac_table.py`. Run native tests and the validator before the hardware compile.

## Deferred prerequisites (NOT part of this plan; gate backup deployment)

The six backup octets `.41`–`.46` are written into the table here but MUST NOT be relied on in the field until, as part of the later pool-commissioning rollout step:

- **DHCP reservation:** confirm `.41`–`.46` are outside the field router's DHCP pool (or reserved), so a backup booting on a new address cannot collide with a DHCP-leased host.
- **Inventory-aware tooling:** the OTA/monitoring tools derive IP as `20 + cube_id` and would not reach a backup on `.41`–`.46`: `tools/check_cubes.py:49`, `tools/update_cubes.py:57`, `tools/update.sh:9` and `:11`, plus the `cubes`-repo monitoring scripts. These must be made inventory/`ip_octet`-aware then.

Until both are done, do not power a backup on its new address. Primaries are unaffected (their addresses and the tooling that reaches them are unchanged).

---

### Task 1: Add a unique per-MAC `ip_octet` and a `findCubeIpOctet` accessor

**Files:**
- Modify: `src/cube_utilities.h` (struct `CubeMacEntry`, add declaration)
- Modify: `src/cube_utilities.cpp` (both table initializers, add `findCubeIpOctet`)
- Test: `test/test_native/test_esp32_utilities.cpp`

**Interfaces:**
- Consumes: nothing.
- Produces: `int findCubeIpOctet(const char *mac_address)` — returns the entry's `ip_octet`, or `-1` if the MAC is unknown. Field `CubeMacEntry.ip_octet` (int).

- [ ] **Step 1: Write the failing tests**

Add to `test/test_native/test_esp32_utilities.cpp` the three functions below (place them after `test_findCubeId_backup_cubes`), then register them in `main()` by adding these lines immediately after `RUN_TEST(test_findCubeId_backup_cubes);`:

```cpp
    RUN_TEST(test_findCubeIpOctet_primaries);
    RUN_TEST(test_findCubeIpOctet_backup_is_unique);
    RUN_TEST(test_findCubeIpOctet_unknown);
```

```cpp
void test_findCubeIpOctet_primaries() {
    // Primaries keep cube_id + 20.
    TEST_ASSERT_EQUAL(21, findCubeIpOctet("AA:AA:AA:AA:AA:AA")); // cube 1
    TEST_ASSERT_EQUAL(26, findCubeIpOctet("FF:FF:FF:FF:FF:FF")); // cube 6
    TEST_ASSERT_EQUAL(31, findCubeIpOctet("01:01:01:01:01:01")); // cube 11
    TEST_ASSERT_EQUAL(36, findCubeIpOctet("06:06:06:06:06:06")); // cube 16
}

void test_findCubeIpOctet_backup_is_unique() {
    // The backup shares cube_id 1 with a primary but MUST have a distinct octet.
    TEST_ASSERT_EQUAL(1, findCubeId("A1:A1:A1:A1:A1:A1"));       // same logical slot
    TEST_ASSERT_EQUAL(41, findCubeIpOctet("A1:A1:A1:A1:A1:A1")); // different address
    TEST_ASSERT_NOT_EQUAL(findCubeIpOctet("AA:AA:AA:AA:AA:AA"),
                          findCubeIpOctet("A1:A1:A1:A1:A1:A1"));
}

void test_findCubeIpOctet_unknown() {
    TEST_ASSERT_EQUAL(-1, findCubeIpOctet("AA:BB:CC:DD:EE:FF"));
    TEST_ASSERT_EQUAL(-1, findCubeIpOctet(""));
}
```

- [ ] **Step 2: Run tests to verify they fail**

Run: `~/.platformio/penv/bin/platformio test -e native`
Expected: FAIL — compile error, `findCubeIpOctet` not declared.

- [ ] **Step 3: Add the struct field and accessor declaration**

In `src/cube_utilities.h`, change the struct and add the declaration:

```cpp
// MAC-to-cube mapping. ip_octet is the physical static-IP host byte, unique
// per MAC and independent of cube_id (the logical/default slot).
struct CubeMacEntry {
  const char *mac;
  int cube_id;
  RgbOrder rgb_order;
  int ip_octet;
};
```

```cpp
// Returns the physical IP octet for the given MAC, or -1 if unknown.
int findCubeIpOctet(const char *mac_address);
```

- [ ] **Step 4: Add `ip_octet` to both tables and implement the accessor**

In `src/cube_utilities.cpp`, add the fourth field to every row of BOTH tables.

Test table (`#ifdef NATIVE_TESTING`):

```cpp
const CubeMacEntry CUBE_MAC_TABLE[] = {
  {"AA:AA:AA:AA:AA:AA",  1, RGB_ORDER_BGR, 21},
  {"BB:BB:BB:BB:BB:BB",  2, RGB_ORDER_BGR, 22},
  {"CC:CC:CC:CC:CC:CC",  3, RGB_ORDER_BGR, 23},
  {"DD:DD:DD:DD:DD:DD",  4, RGB_ORDER_BGR, 24},
  {"EE:EE:EE:EE:EE:EE",  5, RGB_ORDER_BGR, 25},
  {"FF:FF:FF:FF:FF:FF",  6, RGB_ORDER_BGR, 26},
  {"01:01:01:01:01:01", 11, RGB_ORDER_RGB, 31},
  {"02:02:02:02:02:02", 12, RGB_ORDER_RGB, 32},
  {"03:03:03:03:03:03", 13, RGB_ORDER_RGB, 33},
  {"04:04:04:04:04:04", 14, RGB_ORDER_RGB, 34},
  {"05:05:05:05:05:05", 15, RGB_ORDER_RGB, 35},
  {"06:06:06:06:06:06", 16, RGB_ORDER_RGB, 36},
  {"A1:A1:A1:A1:A1:A1",  1, RGB_ORDER_RGB, 41},  // backup: slot 1, unique octet
};
```

Production table (`#else`) — primaries keep `cube_id + 20`; the six slot-1..6 backups get `.41`–`.46`:

```cpp
const CubeMacEntry CUBE_MAC_TABLE[] = {
  {"CC:DB:A7:9F:C2:84",  1, RGB_ORDER_BGR, 21},  // 30-pin
  {"3C:8A:1F:77:DF:8C",  2, RGB_ORDER_BGR, 22},  // 30-pin
  {"8C:4F:00:37:7C:DC",  3, RGB_ORDER_BGR, 23},  // 30-pin
  {"CC:DB:A7:9B:5D:9C",  4, RGB_ORDER_BGR, 24},  // 30-pin (moved from cube 11)
  {"04:83:08:59:76:98",  5, RGB_ORDER_BGR, 25},
  {"EC:E3:34:79:8A:BC",  6, RGB_ORDER_BGR, 26},  // 30-pin (Genuine Espressif replacement)
  {"94:54:C5:F1:AF:00", 11, RGB_ORDER_RGB, 31},  // 30-pin (EMPTY - chip moved to cube 4)
  {"EC:E3:34:79:9D:2C", 12, RGB_ORDER_RGB, 32},  // 30-pin
  {"04:83:08:59:6E:74", 13, RGB_ORDER_RGB, 33},  // 30-pin
  {"94:54:C5:EE:89:4C", 14, RGB_ORDER_RGB, 34},  // 30-pin
  {"8C:4F:00:36:7A:88", 15, RGB_ORDER_RGB, 35},  // 30-pin
  {"D8:BC:38:F9:39:30", 16, RGB_ORDER_RGB, 36},  // 30-pin
  {"80:F3:DA:54:53:B8",  1, RGB_ORDER_BGR, 41},  // backup slot 1
  {"5C:01:3B:65:46:2C",  2, RGB_ORDER_RGB, 42},  // backup slot 2
  {"5C:01:3B:64:E2:84",  3, RGB_ORDER_RGB, 43},  // backup slot 3
  {"D4:8A:FC:9F:B0:C0",  4, RGB_ORDER_BGR, 44},  // 38-pin backup slot 4
  {"D8:BC:38:E5:A8:38",  5, RGB_ORDER_RGB, 45},  // backup slot 5
  {"5C:01:3B:4A:87:4C",  6, RGB_ORDER_RGB, 46},  // backup slot 6
};
```

Add the accessor after `findCubeId` (around `src/cube_utilities.cpp:65`):

```cpp
int findCubeIpOctet(const char *mac_address) {
  const CubeMacEntry* entry = findCubeEntry(mac_address);
  return entry ? entry->ip_octet : -1;
}
```

- [ ] **Step 5: Run tests to verify they pass**

Run: `~/.platformio/penv/bin/platformio test -e native`
Expected: PASS — all tests, including the three new ones and every existing test (the added struct field must not break existing rows).

- [ ] **Step 6: Commit**

```bash
git add src/cube_utilities.h src/cube_utilities.cpp test/test_native/test_esp32_utilities.cpp
git commit -m "feat: add unique per-MAC ip_octet to CubeMacEntry"
```

---

### Task 2: Validate the production table globally (CI guard)

The native suite only compiles the synthetic `#ifdef NATIVE_TESTING` table, so a duplicate MAC or octet typo in the production `#else` rows would compile and pass every test — recreating the exact field IP conflict this plan removes. Add a host-side validator that parses the production block and asserts global uniqueness and a legal octet range, and run it in CI / before any flash.

**Files:**
- Create: `tools/validate_mac_table.py`
- Modify: CI config if present (see Step 4).

**Interfaces:**
- Consumes: `src/cube_utilities.cpp` (the `#else` production `CUBE_MAC_TABLE`).
- Produces: exit code 0 (valid) / 1 (violation printed); no code symbols.

- [ ] **Step 1: Write the validator**

Create `tools/validate_mac_table.py`:

```python
#!/usr/bin/env python3
"""Validate the production CUBE_MAC_TABLE: unique MACs, unique ip_octets,
octets in the allowed host range. Guards against typos the native tests
(which compile the synthetic table) cannot catch."""
import re
import sys
from pathlib import Path

SRC = Path(__file__).resolve().parent.parent / "src" / "cube_utilities.cpp"
ALLOWED_MIN, ALLOWED_MAX = 21, 199  # .1 gateway, .20 unknown-sentinel, .247 broker excluded

def production_rows(text):
    # The production table is the `#else` branch of the NATIVE_TESTING guard.
    body = text.split("#else", 1)[1].split("#endif", 1)[0]
    row = re.compile(r'\{\s*"([0-9A-Fa-f:]+)"\s*,\s*\d+\s*,\s*\w+\s*,\s*(\d+)\s*\}')
    return [(m.group(1), int(m.group(2))) for m in row.finditer(body)]

def main():
    rows = production_rows(SRC.read_text())
    errors = []
    if not rows:
        errors.append("no production rows parsed")
    macs, octets = {}, {}
    for mac, octet in rows:
        if mac in macs:
            errors.append(f"duplicate MAC {mac}")
        macs[mac] = True
        if octet in octets:
            errors.append(f"duplicate ip_octet {octet} (MACs {octets[octet]} and {mac})")
        octets[octet] = mac
        if not ALLOWED_MIN <= octet <= ALLOWED_MAX:
            errors.append(f"ip_octet {octet} for {mac} outside {ALLOWED_MIN}-{ALLOWED_MAX}")
    if errors:
        print(f"MAC table INVALID ({len(rows)} rows):")
        for e in errors:
            print(f"  - {e}")
        return 1
    print(f"MAC table OK: {len(rows)} rows, all MACs and octets unique.")
    return 0

if __name__ == "__main__":
    sys.exit(main())
```

- [ ] **Step 2: Run it against the current table — expect PASS**

Run: `python3 tools/validate_mac_table.py`
Expected: `MAC table OK: 18 rows, all MACs and octets unique.` (exit 0).

- [ ] **Step 3: Prove it catches a collision**

Temporarily change one backup octet to duplicate a primary (e.g. set the `80:F3:DA:54:53:B8` row's octet from `41` to `21`), then:

Run: `python3 tools/validate_mac_table.py`
Expected: exit 1 with `duplicate ip_octet 21 ...`. Revert the edit and re-run to confirm PASS.

- [ ] **Step 4: Wire into CI / pre-flash**

If a CI workflow exists (`.github/workflows/*.yml` or similar), add a step running `python3 tools/validate_mac_table.py` before the build. If none exists, add a note at the top of `tools/flash_cubes.sh` to run it first, and document it in the repo `CLAUDE.md` "After Major Feature Additions" checklist.

- [ ] **Step 5: Commit**

```bash
git add tools/validate_mac_table.py
git commit -m "feat: CI validator for unique MACs/ip_octets in production table"
```

---

### Task 3: Derive the firmware static IP from `ip_octet`; fail closed on unknown MAC

Currently an unknown MAC returns `.20` and the cube proceeds as logical cube 0 — two uncommissioned/mistyped devices would collide, defeating the uniqueness guarantee. The design requires every usable cube to be pre-commissioned in this table, so a missing entry becomes a boot-blocking diagnostic before Wi-Fi/MQTT rather than a shared address. (Bench/dev boards must therefore be commissioned into the table, or built with a bypass flag.)

**Files:**
- Modify: `src/main.cpp` (`getCubeIpOctet`, ~`src/main.cpp:819-834`, and its boot caller)

**Interfaces:**
- Consumes: `findCubeIpOctet(const char*)` from Task 1.
- Produces: no new public symbols; `getCubeIpOctet()` now returns the per-MAC octet, and boot halts with a diagnostic for an unknown MAC.

- [ ] **Step 1: Change the octet source and fail closed**

In `src/main.cpp`, in `getCubeIpOctet()`, keep the existing side effects (setting `current_rgb_order`, `cube_identifier`, and calling `configurePins`) but replace the returned value and handle the unknown MAC. Where it currently does `int cube_id = entry ? entry->cube_id : 0; ... return cube_id + 20;`, make the unknown case halt with a visible diagnostic instead of returning `.20`:

```cpp
  if (!entry) {
    Serial.print("FATAL: MAC not in cube table: ");
    Serial.println(mac_address);
    if (display_manager) display_manager->displayDebugMessage("BAD MAC");
    while (true) { delay(1000); }  // fail closed before Wi-Fi/MQTT
  }
  return entry->ip_octet;
```

(Use the `entry` already fetched at the top of the function; keep the `current_rgb_order`, `cube_identifier`, and `configurePins(cube_id)` lines for the known-MAC path.)

- [ ] **Step 2: Verify the native suite still passes**

Run: `~/.platformio/penv/bin/platformio test -e native`
Expected: PASS (native code does not call `getCubeIpOctet`; confirm no regression).

- [ ] **Step 3: Compile for hardware**

Run: `~/.platformio/penv/bin/platformio run -e v1 -e v6 -e v6_with_hall -e v6_with_hall_analog`
Expected: SUCCESS on all four environments; memory within limits.

- [ ] **Step 4: Commit**

```bash
git add src/main.cpp
git commit -m "feat: derive cube IP from ip_octet; fail closed on unknown MAC"
```

---

### Task 4: MAC-derive both MQTT client IDs

**Files:**
- Modify: `src/cube_utilities.h` (declare helper)
- Modify: `src/cube_utilities.cpp` (implement native + Arduino helper)
- Modify: `src/main.cpp` (full client `src/main.cpp:1487-1489`; keepalive client `src/main.cpp:995`)
- Test: `test/test_native/test_esp32_utilities.cpp`

**Interfaces:**
- Consumes: `removeColonsFromMacC` / `removeColonsFromMac` (existing).
- Produces: `void makeMqttClientIdC(const char* mac, const char* suffix, char* out, size_t n)` (native) and `String makeMqttClientId(const String& mac, const char* suffix)` (Arduino) — producing `"cube-" + <mac without colons> + <suffix>`.

- [ ] **Step 1: Write the failing test**

Add to `test/test_native/test_esp32_utilities.cpp` the function below (place it after the string utility tests), then register it in `main()` by adding this line immediately after `RUN_TEST(test_createMqttTopic_constants);`:

```cpp
    RUN_TEST(test_makeMqttClientId_full_and_keepalive);
```

```cpp
void test_makeMqttClientId_full_and_keepalive() {
    char buf[40];
    makeMqttClientIdC("CC:DB:A7:9F:C2:84", "", buf, sizeof(buf));
    TEST_ASSERT_EQUAL_STRING("cube-CCDBA79FC284", buf);

    makeMqttClientIdC("CC:DB:A7:9F:C2:84", "-ka", buf, sizeof(buf));
    TEST_ASSERT_EQUAL_STRING("cube-CCDBA79FC284-ka", buf);

    // Two physical cubes on the same logical slot get different client IDs.
    char other[40];
    makeMqttClientIdC("80:F3:DA:54:53:B8", "", other, sizeof(other));
    TEST_ASSERT_EQUAL_STRING("cube-80F3DA5453B8", other);
}
```

- [ ] **Step 2: Run test to verify it fails**

Run: `~/.platformio/penv/bin/platformio test -e native`
Expected: FAIL — compile error, `makeMqttClientIdC` not declared.

- [ ] **Step 3: Declare and implement the helper**

In `src/cube_utilities.h`, add declarations in the matching `#ifdef NATIVE_TESTING` / `#else` blocks alongside the existing MAC helpers:

```cpp
#ifdef NATIVE_TESTING
void makeMqttClientIdC(const char* mac_address, const char* suffix, char* output, size_t output_size);
#else
String makeMqttClientId(const String& mac_address, const char* suffix);
#endif
```

In `src/cube_utilities.cpp`, implement both (native version near `removeColonsFromMacC`, Arduino version near `removeColonsFromMac`):

```cpp
#ifdef NATIVE_TESTING
void makeMqttClientIdC(const char* mac_address, const char* suffix, char* output, size_t output_size) {
  char nocolons[32];
  removeColonsFromMacC(mac_address, nocolons, sizeof(nocolons));
  snprintf(output, output_size, "cube-%s%s", nocolons, suffix);
}
#else
String makeMqttClientId(const String& mac_address, const char* suffix) {
  return "cube-" + removeColonsFromMac(mac_address) + suffix;
}
#endif
```

- [ ] **Step 4: Run test to verify it passes**

Run: `~/.platformio/penv/bin/platformio test -e native`
Expected: PASS — including the new test and all existing tests.

- [ ] **Step 5: Wire the full client to the MAC-derived ID**

In `src/main.cpp` at ~`1487`, replace the slot-derived client name:

```cpp
  static String client_name = makeMqttClientId(WiFi.macAddress(), "");
  mqtt_client.setMqttClientName(client_name.c_str());
```

(Remove the old `static String client_name = cube_id;` line; keep the surrounding `debugSend`/display lines.)

- [ ] **Step 6: Wire the keepalive client to the MAC-derived ID**

In `src/main.cpp` at ~`995`, replace:

```cpp
    String client_id = makeMqttClientId(WiFi.macAddress(), "-ka");
```

(Replaces `String client_id = "cube-" + String(cube_identifier) + "-ka";`.)

- [ ] **Step 7: Compile for hardware**

Run: `~/.platformio/penv/bin/platformio run -e v1 -e v6 -e v6_with_hall -e v6_with_hall_analog`
Expected: SUCCESS on all four environments.

- [ ] **Step 8: Commit**

```bash
git add src/cube_utilities.h src/cube_utilities.cpp src/main.cpp test/test_native/test_esp32_utilities.cpp
git commit -m "feat: MAC-derive full and keepalive MQTT client IDs"
```

---

## Notes for the next plan

This plan delivers only Rollout Step 1 (physical identity foundation). It does **not** yet make the slot dynamic — cubes still derive their logical slot from the compiled `cube_id`, and MQTT topics remain slot-scoped. Subsequent plans, in rollout order:

2. `cubes` server: persisted `MAC → {slot, generation}` map, `MAC → tag` inventory, retained `cube/assign/{MAC}` publishes (authority marker off; behavior-preserving).
3. `cube-pn5180`: read slot from the assignment (compiled fallback until authority latches), NVS `{slot, generation}`, MAC-scoped presence, liveness-challenge response, MAC-scoped keepalive topics, `state: sleeping` before deep sleep.
4. `cubes` + console + tooling: assign/swap action, MAC-verified + liveness-gated game-start gate, publish `cube/roster/authoritative`, DHCP reservation of `.41`–`.46`, and inventory-aware OTA/monitoring tooling (the Deferred prerequisites above).
5. `cubes`: server-side NFC tag resolution with sender-MAC/generation provenance; retire `cube/right/{id}`.
