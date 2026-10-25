#pragma once

#include <Arduino.h>

struct StoredSlot {
  int slot;
  uint32_t generation;
  bool authority_latched;
};

// Presence baseline, kept in NVS purely as a cold-boot seed: RTC memory covers
// every wake but is garbage after a power cycle, which is exactly when a cube
// docked on the shelf would otherwise prime its baseline onto its neighbour's
// magnet. 0 means nothing stored.
int loadPresenceBaseline();
bool savePresenceBaseline(int baseline);

// Which hall board this cube carries, as an integer gain off the DRV5055A4
// thresholds (1 = A4 / v2.2, 4 = A2 / v2.3+). 0 means nothing stored, and the
// build-time default stands. Survives a power cycle, because a daughterboard
// does not change when a cube reboots.
int loadHallGain();
bool saveHallGain(int gain);

StoredSlot loadStoredSlot();
void saveStoredSlot(int slot, uint32_t generation);
void latchAuthority();
