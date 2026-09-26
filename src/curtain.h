#pragma once
#include <stdint.h>

// The "!"/"." background curtain: what `cube/curtain` last said.
// Payload: 'R' or 'G' then a row count; anything else, including empty,
// clears. See cubes docs/2026-09-25-the-cubes-see-the-curtain-design.md.
struct Curtain {
  uint8_t rows = 0;
  char color = 0;  // 'R', 'G', or 0 for none

  void clear() { rows = 0; color = 0; }

  // Returns true when what is on screen should change.
  bool set(const char* payload, uint8_t max_rows) {
    uint8_t new_rows = 0;
    char new_color = 0;
    if (payload && (payload[0] == 'R' || payload[0] == 'G') && payload[1]) {
      long value = 0;
      bool ok = true;
      for (const char* p = payload + 1; *p; ++p) {
        if (*p < '0' || *p > '9') { ok = false; break; }
        // Stop accumulating once past the cap, so a long digit string
        // cannot overflow `value`.
        if (value <= max_rows) value = value * 10 + (*p - '0');
      }
      if (ok) {
        new_color = payload[0];
        new_rows = value > max_rows ? max_rows : (uint8_t)value;
      }
    }
    bool changed = new_rows != rows || new_color != color;
    rows = new_rows;
    color = new_color;
    return changed;
  }
};
