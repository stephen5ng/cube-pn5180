#include "cube_tags.h"
#include <string.h>

// Source: tag_ids.txt (authoritative mapping)
const TagToCubeMapping KNOWN_TAGS[] = {
  // Primary set (location A)
  {"A9121466080104E0", 1},
  {"B1FD1366080104E0", 2},
  {"30071466080104E0", 3},
  {"BD291466080104E0", 4},
  {"71E81366080104E0", 5},
  {"361E1466080104E0", 6},
  {"C1A81366080104E0", 11},
  {"829E1366080104E0", 12},
  {"BFBD1366080104E0", 13},
  {"6DB11366080104E0", 14},
  {"32961366080104E0", 15},
  {"FAADF7B8500104E0", 16},

  // Backup set (location B) - replace with actual NFC tag IDs (16 hex chars each)
  {"BA00000000000001", 1},
  {"BA00000000000002", 2},
  {"BA00000000000003", 3},
  {"BA00000000000004", 4},
  {"BA00000000000005", 5},
  {"BA00000000000006", 6},
  {"BA00000000000011", 11},
  {"BA00000000000012", 12},
  {"BA00000000000013", 13},
  {"BA00000000000014", 14},
  {"BA00000000000015", 15},
  {"BA00000000000016", 16},
};

const size_t NUM_KNOWN_TAGS = sizeof(KNOWN_TAGS) / sizeof(KNOWN_TAGS[0]);

int lookupCubeNumberByTag(const char* hex) {
  if (hex == nullptr) {
    return 0;
  }
  for (size_t i = 0; i < NUM_KNOWN_TAGS; ++i) {
    if (strcmp(KNOWN_TAGS[i].tag_hex, hex) == 0) {
      return KNOWN_TAGS[i].cube_number;
    }
  }
  return 0;
}
