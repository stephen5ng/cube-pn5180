#pragma once

#include <Arduino.h>

struct StoredSlot {
  int slot;
  uint32_t generation;
  bool authority_latched;
};

StoredSlot loadStoredSlot();
void saveStoredSlot(int slot, uint32_t generation);
void latchAuthority();
