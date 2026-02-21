# Hardware Debugging Notes

## NFC/PN5180 Slow Read Diagnosis (2026-02-19)

### Symptom
Cube 4 visibly lagging during `random_letters.sh` — letters updating ~1s behind other cubes.

### Root Cause
Bad ESP32/PN5180 hardware on cube 4. The PN5180 BUSY pin was stuck/floating HIGH, causing the library's busy-wait loops to hit the 250ms `commandTimeout` on every SPI transaction.

### Diagnosis Method
Added `diag` UDP command to firmware that reports per-section loop timing:
- Cube 4 (bad): NFC avg = 1,017,742 us (~1 sec), loop = 1,022,384 us
- Cube 2 (good): NFC avg = 183 us, loop = 668 us
- After ESP32 swap: Cube 4 NFC avg = 204 us, loop = 783 us

### PN5180 Library Busy-Wait Mechanism
- `transceiveCommand()` in PN5180.cpp has 5 busy-wait loops polling the BUSY pin
- Each loop has `commandTimeout = 250ms` (defined in PN5180.h)
- A single `getInventory()` involves multiple SPI transactions
- If BUSY pin misbehaves: multiple 250ms timeouts per call → ~1s total block

### PCB Design Considerations
1. **Add 10k pull-down on BUSY line** — GPIO34/35 on ESP32 are input-only with no internal pull-up/pull-down. If PN5180 dies or has bad solder joint, BUSY floats HIGH → 250ms timeout per transaction. External pull-down defaults BUSY to LOW (not busy).
2. **SPI trace routing** — keep MISO (GPIO34) and BUSY (GPIO35) away from HUB75 clock/data lines to avoid cross-talk.
3. **GPIO35 has no internal pull** — unlike most ESP32 GPIOs, pins 34-39 are input-only with no pull resistor capability. Must use external resistors.

### Why PN5180s Fail Repeatedly
Symptom: erratic behavior (slow reads) before full failure. Likely causes in order of suspicion:

1. **Cable connector intermittency** — PN5180 is connected to custom ESP32 PCB via cable. A flexing/loose connector causes BUSY to float or get noisy → erratic reads → chip damage from repeated undefined signal states. Most likely root cause given the pattern.
2. **Cable as noise antenna** — cable near HUB75 wiring picks up inductive coupling from matrix switching current spikes. Higher inductance than PCB trace = worse noise pickup.
3. **ESD via cable** — cable is more physically flexible than a PCB trace, more opportunity for ESD coupling when cube is handled.
4. **Missing local decoupling** — 100nF cap should be at the PN5180 end of the cable, not on the main PCB. A cap on the main PCB doesn't help if there's 10cm of wire between it and the chip.

**Immediate fix:** Solder 100nF cap between VDD and GND at the PN5180 end of the cable.
**Better fix:** Investigate connector quality/type — crimped connectors on short cables that flex repeatedly are a common failure point.
**PCB fix:** Move PN5180 onto the main PCB to eliminate the cable entirely.

### Key Diagnostic Data Points
- Normal NFC read: ~200 us
- Normal loop time: ~500-800 us
- Normal letter interval during random_letters.sh (250ms send rate): ~270ms received
- Bad NFC read (timeout): hits commandTimeout at 250ms per transaction
- NFC is throttled to 50ms intervals, but blocking reads still stall the entire loop
