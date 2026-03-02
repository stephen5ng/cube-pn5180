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

### Cube 2 - ESP32 Replacement
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
- **Result**: Firmware flashed successfully, awaiting performance testing

**Notes**: Long ext antenna boards appear to have NFC performance issues. Switching to standard 30-pin form factor.

---
