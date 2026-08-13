#include "cube_slot_store.h"

#include <Preferences.h>

static const char* NVS_NAMESPACE = "cubepool";
static const char* KEY_SLOT = "slot";
static const char* KEY_GENERATION = "gen";
static const char* KEY_AUTHORITY = "auth";
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

void savePresenceBaseline(int baseline) {
  Preferences prefs;
  if (prefs.begin(NVS_NAMESPACE, false)) {
    prefs.putInt(KEY_PRESENCE_BASELINE, baseline);
    prefs.end();
  }
}

StoredSlot loadStoredSlot() {
  Preferences prefs;
  StoredSlot stored = {-1, 0, false};
  if (prefs.begin(NVS_NAMESPACE, true)) {
    stored.slot = prefs.getInt(KEY_SLOT, -1);
    stored.generation = prefs.getUInt(KEY_GENERATION, 0);
    stored.authority_latched = prefs.getUChar(KEY_AUTHORITY, 0) != 0;
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

void latchAuthority() {
  Preferences prefs;
  if (prefs.begin(NVS_NAMESPACE, false)) {
    prefs.putUChar(KEY_AUTHORITY, 1);
    prefs.end();
  }
}
