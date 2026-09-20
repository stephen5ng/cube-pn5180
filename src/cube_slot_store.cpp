#include "cube_slot_store.h"

#include <Preferences.h>

static const char* NVS_NAMESPACE = "cubepool";
static const char* KEY_SLOT = "slot";
static const char* KEY_GENERATION = "gen";
static const char* KEY_PRESENCE_BASELINE = "presbase";

int loadPresenceBaseline() {
  Preferences prefs;
  int baseline = 0;
  if (prefs.begin(NVS_NAMESPACE, true)) {
    baseline = prefs.getInt(KEY_PRESENCE_BASELINE, 0);
    prefs.end();
  }
  return baseline;
}

bool savePresenceBaseline(int baseline) {
  Preferences prefs;
  if (!prefs.begin(NVS_NAMESPACE, false)) return false;
  const bool written = prefs.putInt(KEY_PRESENCE_BASELINE, baseline) != 0;
  prefs.end();
  return written;
}

StoredSlot loadStoredSlot() {
  Preferences prefs;
  StoredSlot stored = {-1, 0};
  if (prefs.begin(NVS_NAMESPACE, true)) {
    stored.slot = prefs.getInt(KEY_SLOT, -1);
    stored.generation = prefs.getUInt(KEY_GENERATION, 0);
    prefs.end();
  }
  return stored;
}

void saveStoredSlot(int slot, uint32_t generation) {
  Preferences prefs;
  if (prefs.begin(NVS_NAMESPACE, false)) {
    prefs.putInt(KEY_SLOT, slot);
    prefs.putUInt(KEY_GENERATION, generation);
    prefs.end();
  }
}
