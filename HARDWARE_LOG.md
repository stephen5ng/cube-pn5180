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

---
