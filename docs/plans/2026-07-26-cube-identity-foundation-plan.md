# Cube Physical-Identity Foundation Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Give every physical cube a unique static IP and a unique MQTT client ID, both derived from its MAC and independent of its logical slot — the foundation the dynamic cube pool needs so a returning cube and its replacement never collide.

**Architecture:** Add an `ip_octet` field to the compiled `CubeMacEntry` table (today the IP is `cube_id + 20`, and backups deliberately share a `cube_id` with their primary, so two physical cubes compute the same address). Derive the static IP from that field instead. Separately, derive both the full and the maintenance MQTT client IDs from the MAC rather than the logical slot, so an old holder and its replacement do not evict each other at the broker. This is Rollout Step 1 of the design in [docs/plans/2026-07-25-dynamic-cube-pool.md](2026-07-25-dynamic-cube-pool.md); it is behavior-preserving for the currently-deployed primaries and touches no server code.

**Tech Stack:** C++ (Arduino/PlatformIO, ESP32), Unity native unit tests, EspMQTTClient (full client), PubSubClient (maintenance/keepalive client).

## Global Constraints

- Static IPs are kept by design (fast power-bounce reconnect, no DHCP dependency). Do not introduce DHCP.
- Primaries keep their current address (`cube_id + 20`, i.e. `.21`–`.26` and `.31`–`.36`) so this change is behavior-preserving for deployed hardware.
- The native test MAC table (`#ifdef NATIVE_TESTING`) uses stable synthetic values that must never be changed to match hardware; only add the new field to its existing rows.
- Any change to the `CubeMacEntry` struct must update BOTH table initializers (test and production) or the build breaks.
- Native tests: `~/.platformio/penv/bin/platformio test -e native`. Hardware compile: `~/.platformio/penv/bin/platformio run -e esp32dev`. Run tests before compile (per repo CLAUDE.md).

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

### Task 2: Derive the firmware static IP from `ip_octet`

**Files:**
- Modify: `src/main.cpp` (`getCubeIpOctet`, ~`src/main.cpp:819-834`)

**Interfaces:**
- Consumes: `findCubeIpOctet(const char*)` from Task 1.
- Produces: no new symbols; `getCubeIpOctet()` now returns the per-MAC octet.

- [ ] **Step 1: Change the octet source**

In `src/main.cpp`, in `getCubeIpOctet()`, keep the existing side effects (setting `current_rgb_order`, `cube_identifier`, and calling `configurePins`) but replace the returned value. Change the final `return cube_id + 20;` to use the per-MAC octet, preserving the unknown-MAC fallback of `.20`:

```cpp
  int octet = entry ? entry->ip_octet : 20;
  return octet;
```

(Leave the lines that compute `cube_id`, set `current_rgb_order`, `cube_identifier`, and call `configurePins(cube_id)` unchanged — only the returned octet changes.)

- [ ] **Step 2: Verify the native suite still passes**

Run: `~/.platformio/penv/bin/platformio test -e native`
Expected: PASS (no native code depends on `getCubeIpOctet`, but confirm nothing regressed).

- [ ] **Step 3: Compile for hardware**

Run: `~/.platformio/penv/bin/platformio run -e esp32dev`
Expected: SUCCESS — compiles and links, memory within limits.

- [ ] **Step 4: Commit**

```bash
git add src/main.cpp
git commit -m "feat: derive cube static IP from per-MAC ip_octet"
```

---

### Task 3: MAC-derive both MQTT client IDs

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

Run: `~/.platformio/penv/bin/platformio run -e esp32dev`
Expected: SUCCESS.

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
4. `cubes` + console: assign/swap action, MAC-verified + liveness-gated game-start gate, publish `cube/roster/authoritative`.
5. `cubes`: server-side NFC tag resolution with sender-MAC/generation provenance; retire `cube/right/{id}`.
