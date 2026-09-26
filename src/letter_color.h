#pragma once
#include <stdint.h>

// A cube's resting letter colour, from `cube/{id}/letter_color`: "0x" and
// exactly four hex digits of RGB565. Anything else, including empty, is the
// default. See cubes docs/2026-09-26-cubes-wear-the-rack-colour-design.md.
inline uint16_t parseLetterColor(const char* payload, uint16_t default_color) {
  if (!payload || payload[0] != '0' || (payload[1] != 'x' && payload[1] != 'X')) {
    return default_color;
  }
  uint16_t value = 0;
  int digits = 0;
  for (const char* p = payload + 2; *p; ++p, ++digits) {
    char c = *p;
    uint16_t nibble;
    if (c >= '0' && c <= '9') nibble = c - '0';
    else if (c >= 'a' && c <= 'f') nibble = c - 'a' + 10;
    else if (c >= 'A' && c <= 'F') nibble = c - 'A' + 10;
    else return default_color;
    if (digits >= 4) return default_color;
    value = (value << 4) | nibble;
  }
  return digits == 4 ? value : default_color;
}
