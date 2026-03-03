# Hardware Change Log

Tracking hardware modifications, replacements, and issues to identify patterns.

## Format
- **Date**: YYYY-MM-DD
- **Cube**: N
- **Action**: (Replacement/Repair/Modification)
- **Reason**: Why the change was made
- **Old Hardware**: Details about what was replaced
- **New Hardware**: Details about the replacement (including ESP32 chip info)
- **Result**: Outcome of the change

---

## 2026-03-02

### Cube 2 - ESP32 Replacement (Attempt 1)
- **Action**: Replacement
- **Reason**: Performance issues - loop time over 1 second (should be ~475µs), stuck in NFC operations (NFC time: 1,017,706µs)
- **Old Hardware**: Long ext antenna ESP32
  - MAC: D4:8A:FC:9F:B0:C0
  - Form factor: Long board with external antenna connector
- **New Hardware**: 30-pin ESP32
  - MAC: 94:54:C5:EE:87:F0
  - Chip type: ESP32-D0WD-V3 (revision v3.1)
  - Features: Wi-Fi, BT, Dual Core + LP Core, 240MHz, Vref calibration in eFuse, Coding Scheme None
  - Crystal: 40MHz
  - Flash: 4MB
  - Form factor: 30-pin standard ESP32 dev board
- **Result**: **FAILED** - HUB75 display driving problems

**Notes**: Long ext antenna boards appear to have NFC performance issues. First 30-pin replacement had HUB75 driving issues.

### Cube 2 - ESP32 Replacement (Attempt 2)
- **Action**: Replacement
- **Reason**: Previous 30-pin ESP32 (94:54:C5:EE:87:F0) had HUB75 display driving problems
- **Old Hardware**: 30-pin ESP32 (Attempt 1)
  - MAC: 94:54:C5:EE:87:F0
  - Problem: Could not drive HUB75 display properly
- **New Hardware**: 30-pin ESP32 for v6 board
  - MAC: 3C:8A:1F:77:DF:8C
  - Chip type: ESP32-D0WD-V3 (revision v3.1)
  - Features: Wi-Fi, BT, Dual Core + LP Core, 240MHz, Vref calibration in eFuse, Coding Scheme None
  - Crystal: 40MHz
  - Flash: 4MB
  - Form factor: 30-pin standard ESP32 dev board
  - Board version: v6 (requires BOARD_V6 build flag)
- **Result**: Firmware (v6) flashed successfully, awaiting testing

**Notes**: Important to specify `--environment v6` for v6 boards to get correct pin assignments.

### Cube 4 - ESP32 Replacement
- **Action**: Replacement
- **Reason**: No UDP response - MQTT working but UDP server not responding. Previous history of PN5180 BUSY pin issues causing ~1 second NFC delays.
- **Old Hardware**: 30-pin ESP32
  - MAC: 5C:01:3B:64:E2:84
  - Problem: No UDP response, history of PN5180 BUSY pin hardware issues (documented in hardware-debugging.md)
- **New Hardware**: 30-pin ESP32 for v6 board (Attempt 2)
  - MAC: 3C:8A:1F:77:B9:24
  - Chip type: ESP32-D0WD-V3 (revision v3.1)
  - Features: Wi-Fi, BT, Dual Core + LP Core, 240MHz, Vref calibration in eFuse, Coding Scheme None
  - Crystal: 40MHz
  - Flash: 4MB
  - Form factor: 30-pin standard ESP32 dev board
  - Board version: v6 (requires BOARD_V6 build flag)
- **Result**: Firmware (v6) flashed successfully, awaiting testing

**Notes**: Previous attempt used MAC 14:33:5C:30:25:98 but was replaced. Board version reference from `/Users/stephenng/programming/blockwords/cubes/cube_board_versions.txt`

---

## v6 PCB Design Issues (for next revision)

### HUB75 power not cut during deep sleep (RESOLVED)

- **Symptom**: 5V present across HUB75 connector even when ESP32 is in deep sleep
- **Root causes** (two independent issues):
  1. **GPIO5 not held low in deep sleep**: GPIO5 controls TPS22975 enable. Without explicit hold, GPIO5 floats during deep sleep and the switch stays on.
  2. **HUB75 data pin backfeed**: Even with TPS22975 off, the ESP32's HUB75 data pins (driven at 3.3V by I2S DMA) backfeed ~2.2V onto the 5V rail through the panel's input clamping/ESD diodes.
- **Misdiagnosis**: Initially thought the PCB "EN" test pad was the TPS22975 enable — it's actually the ESP32 chip enable/reset pin. A 100kΩ resistor was soldered to the wrong signal (should be removed).
- **Fix** (firmware only, no PCB change needed):
  1. `stopDMAoutput()` — stop I2S DMA from driving HUB75 pins
  2. `pinMode(pin, INPUT)` on all 14 HUB75 data pins — tri-state to prevent backfeed
  3. `digitalWrite(5, LOW)` + `gpio_hold_en(GPIO_NUM_5)` — disable TPS22975
  4. `gpio_deep_sleep_hold_en()` — hold ALL GPIO states through deep sleep (critical for non-RTC pins like A=19, B=21, D=22, CLK=16 which would otherwise float)
- **Verified working** on cube 4, 2026-03-03

### GPIO34/35 have no internal pull resistors

- **Symptom**: PN5180 BUSY pin floats HIGH if PN5180 dies or has a bad connection, causing 250ms timeout per SPI transaction (~1s per NFC read)
- **Root cause**: GPIO34–39 on ESP32 are input-only with no internal pull-up or pull-down capability
- **Fix for next revision**: Add 10kΩ pull-down on BUSY line (defaults BUSY to LOW = not busy when PN5180 is absent or dead)

### PN5180 on a cable

- **Symptom**: Repeated PN5180 failures across multiple cubes — erratic reads before full failure
- **Root cause**: Cable between PCB and PN5180 module introduces noise pickup from HUB75 switching currents, ESD exposure, and connector intermittency. Decoupling cap on main PCB doesn't help with 10cm of wire to the chip.
- **Fix for next revision**: Move PN5180 onto the main PCB to eliminate cable entirely. If cable is retained, add 100nF decoupling cap at the PN5180 end of the cable.
