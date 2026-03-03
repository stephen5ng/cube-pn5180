# Claude Code Development Notes

## Development Workflow Requirements

### After Major Feature Additions
**ALWAYS** perform both steps in this order:

1. **Run Tests First**
   ```bash
   ~/.platformio/penv/bin/platformio test -e native
   ```
   - Verify all existing functionality still works
   - Catch logic errors and regressions
   - Ensure utility functions are working correctly

2. **Test Compilation Second** 
   ```bash
   ~/.platformio/penv/bin/platformio run -e esp32dev
   ```
   - Verify code compiles for target hardware
   - Catch syntax errors, missing declarations, type mismatches
   - Ensure memory usage is within acceptable limits

### Why Both Are Important
- **Tests**: Verify functional correctness of the logic
- **Compilation**: Verify the code can actually be deployed to hardware
- **Order matters**: Tests are faster and catch logic issues first

### Common Issues to Watch For
- Function declaration order (forward declarations needed)
- Integer overflow in timer calculations (use proper data types)
- Missing include files or function definitions
- Array bounds checking for MAC address lookups
- Memory usage staying within ESP32 limits

## Project Structure
- `src/main.cpp` - Main ESP32 application code
- `src/cube_utilities.cpp/.h` - Utility functions for MAC/NFC handling
- `test/test_native/` - Native unit tests for utility functions
- `tools/` - Cube management: `flash_cubes.sh`, `reboot.sh`, `wake.sh`, `sleep.sh`, `check_cubes.sh`, `show_cube_numbers.py`, diagnostics
- `docs/` - Planning docs, hardware debugging notes, analysis write-ups
- `cube_board_versions.txt` - Maps cube IDs to board version (v1/v6) for flashing
- Sleep mode uses GPIO 5 for button input
- MAC address table determines cube configuration and RGB pin assignments

## Network Environment
- **MQTT Broker**: `192.168.8.247` (NOT localhost)
- **Cube IP Mapping**: Simple formula - cube N is at `192.168.8.{20+N}`
  - Cube 1: `192.168.8.21`
  - Cube 2: `192.168.8.22`
  - etc.
- **Python Game Directory**: `/Users/stephenng/programming/blockwords/cubes/`
  - This is where responsiveness tests and main game code are located
  - NOT the PlatformIO project directory

## MQTT Testing Commands
```bash
# Test cube connectivity (using correct broker)
mosquitto_pub -h 192.168.8.247 -t "cube/1/ping" -m "test"
mosquitto_sub -h 192.168.8.247 -t "cube/1/echo" -C 1

# Monitor cube topics
mosquitto_sub -h 192.168.8.247 -t "cube/1/#" -v

# Run responsiveness tests (from correct directory)
cd /Users/stephenng/programming/blockwords/cubes
MQTT_SERVER=192.168.8.247 python3 test_cube_responsiveness.py stress_0.1
```

## Performance Optimization History

### Current Status: NEAR-OPTIMAL ✅
The ESP32 firmware has been optimized to achieve **99.7% of theoretical maximum MQTT responsiveness**:

- **30 FPS Display Throttling**: Major optimization - throttles `animate()` and `updateDisplay()` to 30 FPS
- **QoS 0 MQTT**: Fire-and-forget messaging reduces broker overhead
- **Border Message Filtering**: Identified as primary bottleneck (99% improvement when filtered)

### Tested and REJECTED Approaches:
1. **ESP32-side Message Deduplication**: Instrumentation showed 66% duplicate messages, but deduplication overhead (2ms+ per message) would exceed benefits. See `docs/mqtt_deduplication_analysis.md` for detailed analysis.
2. **Complex Performance Instrumentation**: Simple solutions (30 FPS throttling) were more effective than complex measurement systems.

### Recommended Future Optimizations:
- **Python-side message reduction**: Filter duplicates before sending (more efficient on powerful host)
- **Smarter border logic**: Only send border updates when borders actually change
- **Message batching**: Combine multiple updates into single messages

### Key Insight:
When bottleneck is message volume, **reduce message count at source** rather than trying to process high volume more efficiently on constrained ESP32 hardware.

## Latest Optimization: Consolidated Border Messaging ✅

**Date**: August 2025  
**Status**: DEPLOYED and VERIFIED

### Implementation:
- **New Protocol**: `"NSW:0xF800"` format consolidates multiple border directions with single color
- **ESP32 Firmware**: Added `handleConsolidatedBorderCommand()` with N/S/E/W direction parsing  
- **Python Game**: Updated `cubes_to_game.py` to use consolidated messaging for word borders and clearing
- **Backward Compatibility**: Legacy individual border topics still supported

### Protocol Examples:
- `ENSW:0xF800` → Full red frame (single letter word)
- `NSW:0xF800` → Left edge of word (North, South, West borders)  
- `NS:0xF800` → Middle of word (North, South only)
- `NSE:0xF800` → Right edge of word (North, South, East borders)
- `:` → Clear all borders

### Performance Impact:
- **75% reduction in border message volume** (4 messages → 1 message per cube)
- **Major improvement** for high-frequency word marking operations  
- **Verified working** with RGB565 color format (`0xF800` = red)

### Code Locations:
- ESP32: `src/main.cpp:559` - `handleConsolidatedBorderCommand()`
- Python: `cubes_to_game.py:185` - `_mark_tiles_for_guess()` updated
- Topic: `cube/{id}/border` (consolidated) vs `cube/{id}/border_hline_top` (legacy)

## Sleep Mode: HUB75 Power-Off (v6 boards)

The v6 PCB has a TPS22975 load switch on GPIO5 that gates 5V to the HUB75 panel. Cutting power in deep sleep requires a specific sequence because:
1. I2S DMA continuously drives HUB75 data pins — must be stopped first
2. HUB75 data pins at 3.3V backfeed ~2.2V through the panel's clamping diodes — must be tri-stated
3. Non-RTC GPIO states don't persist through deep sleep — `gpio_deep_sleep_hold_en()` required

**Sleep sequence** (`enterSleepMode()`): `stopDMAoutput()` → tri-state all HUB75 pins → `gpio_hold_en(GPIO_NUM_5)` → `gpio_deep_sleep_hold_en()` → `esp_deep_sleep_start()`

**Wake sequence** (`setup()`): `gpio_deep_sleep_hold_dis()` → `gpio_hold_dis(GPIO_NUM_5)` → `pinMode(5, OUTPUT)` → `digitalWrite(5, HIGH)`

**PCB note**: The "EN" test pad is the ESP32 chip enable/reset — NOT the TPS22975 enable. GPIO5 (TPS22975 control) has no test pad.

## Testing Notes
- Native tests run utility functions without ESP32 dependencies
- All tests must pass before deployment
- Segfault protection implemented for unrecognized MAC addresses
- Sleep mode implementation includes battery maintenance wake-up every hour
- Comments should only refer to the current state of the code. They should not describe any changes from previous states ("More detailed error handling" or "code move to ...")
- Any references to future state in the code should be in a TODO.
- Do not comment code that is obvious. (And if code isn't obvious, try to rewrite it so that it is obvious)
- Be wary of putting information in comments that repeats the same information in the code. There generally should only be one source of truth.

