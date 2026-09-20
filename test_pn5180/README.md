# PN5180 Test Jig

Tests PN5180 chips/modules using a v1 board as the test platform.

## Hardware Needed

- v1 board (working) as test platform
- Jumper wires (dupont cables)
- PN5180 module to test

## Wiring

Use the v1 board's CN1 connector (9-pin header):

| CN1 Pin | Signal | ESP32 GPIO | Description |
|---------|--------|------------|-------------|
| 1 | GND | - | Ground |
| 2 | BUSY | GPIO 36 | Busy signal from PN5180 |
| 3 | SCK | GPIO 18 | SPI clock |
| 4 | MISO | GPIO 39 | SPI MISO |
| 5 | MOSI | GPIO 23 | SPI MOSI |
| 6 | NSS | GPIO 32 | SPI chip select |
| 7 | RST | GPIO 17 | Reset |
| 9 | +5V | - | Power |

**The sketch is pre-configured for these v1 board pins.**

## Procedure

1. **Connect PN5180 module to v1 board's CN1**
   - Use jumper wires or plug directly into CN1 header
   - Verify pin 1 orientation (GND)

2. **Upload test sketch:**
   ```bash
   cd test_pn5180
   ~/.platformio/penv/bin/platformio run --target upload
   ~/.platformio/penv/bin/platformio device monitor
   ```

3. **Check Serial Monitor:**
   - Should see `✓ PN5180 detected and responding!`
   - IRQ Status should be `0x00` or `0x14`

4. **Test tag reading:**
   - Hold an ISO15693 tag near the coil
   - Should see `TAG FOUND!` with UID

## Interpreting Results

| Result | Diagnosis |
|--------|-----------|
| "PN5180 detected" + reads tags | PN5180 is healthy |
| "PN5180 detected" + no tag reads | Check coil/connection |
| "Initialization failed" | PN5180 dead or wiring wrong |
| IRQ Status = 0x01 | General error - check connections |
| IRQ Status = 0x02 | BUSY stuck high |
| IRQ Status = 0x40 | TX error |
| IRQ Status = 0x400 | SRAM error - PN5180 defective |

## Testing Bare PN5180 Chips

If testing a bare PN5180 chip (removed from module):
- You'll need a test socket or breakout board
- Connect directly to the v1 board's CN1 pins
- Ensure proper 5V power and GND connections
