#pragma once

#include <Arduino.h>

#include "vacated_slots.h"

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

// Slots a cube has left whose retained records are still to be deleted. A
// reassignment reboots rather than rebinding, so the outgoing topic names would
// otherwise die with the RAM. See vacated_slots.h for why this is a set.
VacatedSlots loadVacatedSlots();
bool saveVacatedSlots(const VacatedSlots& set);

StoredSlot loadStoredSlot();
void saveStoredSlot(int slot, uint32_t generation);
void latchAuthority();
