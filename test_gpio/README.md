# GPIO 35 Test Jig

Tests whether GPIO 35 (PN5180_BUSY) is damaged on ESP32s.

## Hardware Needed

- Jumper wire (dupont cable)
- Multimeter (optional, for verification)

## Connection Points on v6 PCB

| Signal | PN5180 Pin | Description |
|--------|-----------|-------------|
| GPIO 35 | Pin 13 | BUSY output |
| GND | Pin 7, 9, 11 | Ground |
| 3.3V | Pin 1 | Power |

## Procedure

1. **Prepare the board:**
   - Remove PN5180 chip OR lift pin 13 from the PCB
   - This isolates GPIO 35 so the ESP32 pin isn't driven by the PN5180

2. **Upload sketch:**
   ```bash
   cd test_gpio
   ~/.platformio/penv/bin/platformio run --target upload
   ~/.platformio/penv/bin/platformio device monitor
   ```

3. **Open Serial Monitor** (115200 baud)

4. **Test GND connection:**
   - Connect jumper from PN5180 pin 13 to any GND pad
   - Serial should show `GPIO 35: 0 (CONNECTED TO GND) ✓`
   - If reads 1 → pin is damaged (stuck HIGH)

5. **Test 3.3V connection:**
   - Connect jumper from PN5180 pin 13 to PN5180 pin 1 (3.3V)
   - Serial should show `GPIO 35: 1 (FLOATING or 3.3V)`
   - If reads 0 → pin is damaged (stuck LOW)

6. **Test pullup (floating):**
   - Leave pin unconnected
   - Serial should show `GPIO 35: 1 (FLOATING or 3.3V)`
   - If reading fluctuates randomly → pin is damaged

## Interpreting Results

| Result | Diagnosis |
|--------|-----------|
| All tests pass | GPIO 35 is healthy |
| Stuck HIGH (always 1) | GPIO 35 likely damaged (open circuit or input protection blown) |
| Stuck LOW (always 0) | GPIO 35 likely shorted to GND |
| Random readings | GPIO 35 input buffer damaged |

## For "Dead" ESP32s

If testing bare ESP32s without a PCB:
- Connect directly to ESP32 pin: GPIO 35 is on the edge connector
- Refer to ESP32-WROOM pinout for exact location
- GND and 3.3V available on USB connector pins
