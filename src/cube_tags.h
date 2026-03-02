#pragma once

#ifdef NATIVE_TESTING
#include <string.h>
#include <stdint.h>
#else
#include <Arduino.h>
#endif

struct TagToCubeMapping {
  const char* tag_hex;   // Uppercase hex string (16 chars), no separators
  uint8_t cube_number;   // 1..16
};

extern const TagToCubeMapping KNOWN_TAGS[];
extern const size_t NUM_KNOWN_TAGS;

// Returns 0 if unknown; otherwise 1..16
int lookupCubeNumberByTag(const char* hex);
