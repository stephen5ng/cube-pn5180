# Display Debug Info Disappears - Bug Analysis

## Symptom
- Cube 1 displays startup debug info (version, IP address, etc.)
- Then that info disappears
- Only affects cube 1
- Consistent across recent firmware pushes

## Investigation Date
2025-03-04

## Code Analysis

### How Debug Messages Work
`displayDebugMessage()` in `DisplayManager` class:
```cpp
void displayDebugMessage(const char* message) {
  int y_pos = debug_line * 8 + 8;
  led_display->setCursor(1, y_pos);
  led_display->setTextSize(1);
  led_display->setFont(NULL);
  led_display->setTextColor(RED, BLACK);
  led_display->print(message);
  led_display->flipDMABuffer();
  // ... write again for double-buffering
  debug_line++;
}
```

These write DIRECTLY to the HUB75 DMA buffer and flip it - bypassing `updateDisplay()`.

### Why They Disappear
The `DisplayManager` constructor initializes `is_dirty = true`:
```cpp
DisplayManager(String cube_id) : is_dirty(true), ...
```

When `loop()` starts, `updateDisplay()` is called every 33ms:
```cpp
void updateDisplay(unsigned long current_time) {
  if (!is_dirty) return;  // Early exit if not dirty

  // ... draw content ...
  led_display->flipDMABuffer();
  led_display->clearScreen();  // ← Wipes everything including debug messages
  is_dirty = false;
}
```

**Root cause:** First loop iteration runs `updateDisplay()` which clears the screen, then draws nothing (no letter/image set yet).

### Why Only Cube 1?
**Hypothesis:** Cube 1 may receive an MQTT message immediately after boot that triggers `is_dirty = true`, causing `updateDisplay()` to run sooner than other cubes.

Alternative: Other cubes have timing differences (WiFi connect delay) that keep debug info visible longer, but it eventually disappears on all cubes once `updateDisplay()` runs.

## Debug Logging Added (Incomplete)

Added UDP debug logging via existing `debugSend()` function:
- `SETUP DONE` - sent after `setupUDP()` completes
- `LOOP START` - sent on first loop iteration
- `DISPLAY DEBUG: First updateDisplay...` - shows display state when first update runs
- Animation/color change tracking during first 10 seconds

### Issue: Messages Not Received
During testing, UDP debug messages were NOT received. Possible reasons:
1. Timing: Listener starts after cube sends messages
2. UDP not ready: `setupUDP()` called at end of setup, messages sent immediately
3. WiFi/MQTT timing: Cube doing battery maintenance wake, not full boot

## Next Steps

1. **Verify UDP debug is actually sent** - Add Serial fallback, check if messages are being sent at all
2. **Check if all cubes eventually lose debug info** - May be timing difference, not cube-1-specific
3. **Alternative: Don't clear screen if no content** - Modify `updateDisplay()` to check if anything would actually be drawn before clearing

## Files Modified
- `src/main.cpp`: Added UDP debug logging, `setup_complete_time` tracking

## Testing Notes
- Cube 1 MAC: 3C:8A:1F:77:B9:24 (v6 board)
- UDP debug port: 54322
- Control port: 54321
- Listen with: `nc -u -l 54322` or Python script
