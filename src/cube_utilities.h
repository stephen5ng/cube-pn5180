#pragma once

#ifdef NATIVE_TESTING
#include <string.h>
#include <stdio.h>
#include <stdint.h>
#else
#include <Arduino.h>
#endif

// Constants
#define NFCID_LENGTH 8

// HUB75 panel R/B channel order. Varies per physical display, independent of
// chip and board. setupDisplay() picks bgr_pins vs rgb_pins accordingly.
enum RgbOrder {
  RGB_ORDER_BGR,
  RGB_ORDER_RGB,
};

// Physical facts about a board, keyed by the MAC that never changes: which
// static-IP octet it answers on and how its panel is wired. Which slot it plays
// is not among them -- the roster assigns that at run time, and a board with no
// assignment shows NO SLOT rather than guessing.
struct CubeMacEntry {
  const char *mac;
  RgbOrder rgb_order;
  int ip_octet;
};

extern const CubeMacEntry CUBE_MAC_TABLE[];
extern const int NUM_CUBE_MAC_ENTRIES;

// MQTT Topic Prefixes
extern const char* MQTT_TOPIC_PREFIX_CUBE;
extern const char* MQTT_TOPIC_PREFIX_ECHO;
extern const char* MQTT_TOPIC_PREFIX_VERSION;

// Returns pointer into CUBE_MAC_TABLE for the given MAC, or nullptr if unknown.
const CubeMacEntry* findCubeEntry(const char *mac_address);

// Returns the physical IP octet for the given MAC, or -1 if unknown.
int findCubeIpOctet(const char *mac_address);

enum AssignmentParseResult {
  ASSIGNMENT_OK,
  ASSIGNMENT_UNASSIGNED,
  ASSIGNMENT_MISSING,
  ASSIGNMENT_MALFORMED,
};

struct CubeAssignment {
  uint32_t generation;
  int slot;
};

AssignmentParseResult parseAssignmentRecord(const char* json, CubeAssignment* out);
bool assignmentRecordIsActionable(AssignmentParseResult result);
int resolveAssignedSlot(AssignmentParseResult result, int record_slot,
                        bool authority_latched, int fallback_slot);

// The boot screen's identity line: "c12 ip32".
//
// Both numbers are needed because neither alone identifies a cube. The slot is
// what the admin page calls it; the octet is what the network calls it; and for
// a spare the two are unrelated -- one sitting at .47 assigned to slot 1 reads
// "c1 ip47", which is precisely the pairing that cannot be guessed from either
// number on its own.
//
// slot comes from NVS. A board that has never been assigned has none, and
// prints "c?" rather than a wrong number -- applySlot() paints NO SLOT a moment
// later.
//
// Output stays inside the ~10 characters displayDebugMessage() fits at size 1
// on a 64px panel, above which it wraps onto a second line.
void formatBootIdentity(char* out, size_t out_size, int slot, int ip_octet);
void convertNfcIdToHexString(uint8_t* nfc_id, int id_length, char* hex_buffer);

enum NfcObservationAction {
  NFC_OBS_NONE,    // nothing to publish
  NFC_OBS_TAG,     // publish tag_hex
  NFC_OBS_ABSENT,  // publish "-"
};

NfcObservationAction decideNfcObservation(bool read_ok, bool no_card,
                                          bool hall_allows_neighbor,
                                          bool hall_says_present,
                                          const char* tag_hex,
                                          const char* last_published);

void buildObservationPayload(const char* boot_id, const char* tag,
                             char* out, size_t out_size);

// A tag right at the edge of NFC range reads as present/absent in rapid
// succession; on hardware that showed up to ~10 flips/sec. Almost always the
// player meant the cubes to be apart, not together -- so disconnects
// (NFC_OBS_ABSENT) are never held back, but a RECONNECT to a tag that has
// already flipped repeatedly and recently must hold steady for
// NFC_CHATTER_CONFIRM_MS before it is accepted. A tag's first
// departure-and-return is always instant (a single dropped read is not
// chatter); only the second flip onward within NFC_CHATTER_WINDOW_MS pays
// the confirmation cost.
#define NFC_CHATTER_WINDOW_MS 1000UL
#define NFC_CHATTER_CONFIRM_MS 200UL

struct NfcChatterState {
  char tag[NFCID_LENGTH * 2 + 1] = "";
  // Whether `tag` is physically present right now, by this gate's OWN
  // reckoning -- independent of `last_observation_published`, which
  // `cube/resend` and every (re)connect clear to force a re-announce even
  // when nothing physically changed (main.cpp). Without this, a forced
  // re-announce reads as NFC_OBS_TAG with no intervening NFC_OBS_ABSENT and
  // gets counted as a real flip, which can wrongly gate the tag's actual
  // next departure-and-return.
  bool connected = false;
  unsigned long flip_ms_newest = 0;
  unsigned long flip_ms_older = 0;
  //: 0 = no reconnect currently being held for confirmation.
  unsigned long confirm_since_ms = 0;
};

struct NfcChatterResult {
  NfcObservationAction action;
  NfcChatterState state;
};

// Applies chatter suppression on top of `decideNfcObservation`'s raw
// decision. `action` is that decision; `read_ok`/`tag_hex` are this cycle's
// raw read, needed to tell a broken confirmation streak from a continuing
// one. Returns the (possibly downgraded to NFC_OBS_NONE) action to actually
// publish, and the state to carry into the next cycle.
NfcChatterResult applyNfcChatterGate(const NfcChatterState& state,
                                     NfcObservationAction action,
                                     bool read_ok, const char* tag_hex,
                                     unsigned long now_ms);

enum WakeAction { WAKE_ACTION_STAY_ASLEEP, WAKE_ACTION_WAKE_FULL };

// The keep-alive check-in decision. Only a timer wake makes a check-in, so
// there is no wake-reason parameter; runWakeCheckIn() dispatches on that.
WakeAction resolveWakeAction(bool wifi_connected,
                             bool mqtt_connected,
                             bool device_requests_sleep);

enum WakeReason { WAKE_REASON_TIMER, WAKE_REASON_BUTTON, WAKE_REASON_OTHER };

struct WakeCheckInPorts {
  virtual ~WakeCheckInPorts() {}
  virtual bool awaitWifi() = 0;
  virtual bool connectMqtt() = 0;
  // Reads the retained sleep flag into `out`. Returns false when the read
  // could not be confirmed, which is not the same as "no flag is set": an
  // empty retained topic delivers nothing, so an unconfirmed read carries no
  // information at all.
  virtual bool readSleepFlag(bool* out) = 0;
  virtual void clearSleepFlag() = 0;
  // Disconnects an active keep-alive client, then sleeps. Does not return on
  // hardware, so every call site returns immediately after it.
  virtual void enterSleep() = 0;
  virtual void stayAwake() = 0;
};

void runWakeCheckIn(WakeReason wake_reason, WakeCheckInPorts& ports);

#ifdef NATIVE_TESTING
// Native C versions for testing
void removeColonsFromMacC(const char* mac_address, char* output, size_t output_size);
void createMqttTopicC(const char* cube_identifier, const char* suffix, char* output, size_t output_size);
void makeMqttClientIdC(const char* mac_address, const char* suffix, char* output, size_t output_size);
#else
// Arduino String versions for ESP32
String removeColonsFromMac(const String& mac_address);
String createMqttTopic(const String& cube_identifier, const char* suffix);
String makeMqttClientId(const String& mac_address, const char* suffix);
#endif
