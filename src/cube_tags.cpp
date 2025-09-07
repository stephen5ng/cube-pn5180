#include "cube_tags.h"
#include <string.h>

// Source: tag_ids.txt (authoritative mapping)
const TagToCubeMapping KNOWN_TAGS[] = {
  {"A9121466080104E0", 1},
  {"B1FD1366080104E0", 2},
  {"30071466080104E0", 3},
  {"BD291466080104E0", 4},
  {"71E81366080104E0", 5},
  {"361E1466080104E0", 6},
  {"8011F8B8500104E0", 11},
  {"AF11F8B8500104E0", 12},
  {"62F1F7B8500104E0", 13},
  {"B9F5F7B8500104E0", 14},
  {"1A14F8B8500104E0", 15},
  {"D4F0F7B8500104E0", 16},
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
