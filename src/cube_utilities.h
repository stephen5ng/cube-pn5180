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

// MAC-to-cube mapping. ip_octet identifies the physical device and is
// independent of cube_id, the logical/default slot.
struct CubeMacEntry {
  const char *mac;
  int cube_id;
  RgbOrder rgb_order;
  int ip_octet;
};

// cube_id for a board with no default slot -- an uncommissioned spare.
//
// The fallback below hands back compiled_cube_id when no assignment record
// arrives, which is a safety net for a fielded cube whose record was lost but
// a live hazard for a spare: a freshly flashed board has blank NVS, so
// authority_latched is false and it would adopt a real cube's slot on the
// fleet network. Every consumer treats a non-positive slot as unassigned, so
// this makes the fallback inert instead of a collision. A spare that has been
// assigned keeps its own safety net, because the fallback prefers the stored
// slot once it has one.
constexpr int CUBE_ID_NONE = 0;

extern const CubeMacEntry CUBE_MAC_TABLE[];
extern const int NUM_CUBE_MAC_ENTRIES;

// MQTT Topic Prefixes
extern const char* MQTT_TOPIC_PREFIX_CUBE;
extern const char* MQTT_TOPIC_PREFIX_GAME;
extern const char* MQTT_TOPIC_PREFIX_NFC;
extern const char* MQTT_TOPIC_PREFIX_ECHO;
extern const char* MQTT_TOPIC_PREFIX_VERSION;

// Returns pointer into CUBE_MAC_TABLE for the given MAC, or nullptr if unknown.
const CubeMacEntry* findCubeEntry(const char *mac_address);

// Returns cube_id for the given MAC address, or -1 if unknown.
int findCubeId(const char *mac_address);
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
                        bool authority_latched, int compiled_cube_id);
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

enum WakeAction { WAKE_ACTION_STAY_ASLEEP, WAKE_ACTION_WAKE_FULL };

// The keep-alive check-in decision. Only a timer wake makes a check-in, so
// there is no wake-reason parameter; runWakeCheckIn() dispatches on that.
WakeAction resolveWakeAction(bool wifi_connected,
                             bool mqtt_connected,
                             bool has_slot_topic,
                             bool device_requests_sleep,
                             bool slot_requests_sleep);

enum WakeReason { WAKE_REASON_TIMER, WAKE_REASON_BUTTON, WAKE_REASON_OTHER };

struct SleepFlags { bool device_requests_sleep; bool slot_requests_sleep; };

struct WakeCheckInPorts {
  virtual ~WakeCheckInPorts() {}
  virtual bool awaitWifi() = 0;
  virtual bool connectMqtt() = 0;
  virtual bool hasSlotTopic() = 0;
  // Reads the retained sleep flags into `out`. Returns false when the read
  // could not be confirmed, which is not the same as "no flag is set": an
  // empty retained topic delivers nothing, so an unconfirmed read carries no
  // information at all.
  virtual bool readSleepFlags(SleepFlags* out) = 0;
  virtual void clearSleepFlags() = 0;
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
