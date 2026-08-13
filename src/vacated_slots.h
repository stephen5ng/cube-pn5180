#pragma once

#include <stddef.h>

// Slots a cube has left whose retained records have not been deleted yet.
//
// A reassignment reboots, so the RAM retry queue does not survive it: the set
// has to be durable, and it has to be a set rather than one value, because a
// second reassignment can arrive while the first delete is still failing. 0 is
// the empty marker; slots are always positive.
static constexpr size_t VACATED_SLOT_CAPACITY = 4;

struct VacatedSlots {
  int slots[VACATED_SLOT_CAPACITY];
};

inline bool vacatedSlotsContain(const VacatedSlots& set, int slot) {
  for (size_t i = 0; i < VACATED_SLOT_CAPACITY; i++) {
    if (set.slots[i] == slot) return true;
  }
  return false;
}

// Oldest-first ordering is what makes eviction meaningful, so entries are kept
// packed rather than left with holes.
inline void addVacatedSlot(VacatedSlots* set, int slot) {
  if (slot <= 0 || vacatedSlotsContain(*set, slot)) return;

  for (size_t i = 0; i < VACATED_SLOT_CAPACITY; i++) {
    if (set->slots[i] == 0) {
      set->slots[i] = slot;
      return;
    }
  }

  // Full means that many distinct slots have gone undeleted, which needs the
  // broker to reject writes across as many reassignments. The oldest is the one
  // whose slot has been out of use longest.
  for (size_t i = 1; i < VACATED_SLOT_CAPACITY; i++) {
    set->slots[i - 1] = set->slots[i];
  }
  set->slots[VACATED_SLOT_CAPACITY - 1] = slot;
}

inline void removeVacatedSlot(VacatedSlots* set, int slot) {
  size_t out = 0;
  VacatedSlots packed = {};
  for (size_t i = 0; i < VACATED_SLOT_CAPACITY; i++) {
    if (set->slots[i] > 0 && set->slots[i] != slot) {
      packed.slots[out++] = set->slots[i];
    }
  }
  *set = packed;
}
