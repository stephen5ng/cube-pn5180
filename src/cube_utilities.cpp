#include "cube_utilities.h"
#include <ctype.h>
#include <stdlib.h>

#ifdef NATIVE_TESTING
// Test MAC table - stable values that never change
// Hardware replacements should NOT require test updates
const CubeMacEntry CUBE_MAC_TABLE[] = {
  {"AA:AA:AA:AA:AA:AA",  1, RGB_ORDER_BGR, 21},
  {"BB:BB:BB:BB:BB:BB",  2, RGB_ORDER_BGR, 22},
  {"CC:CC:CC:CC:CC:CC",  3, RGB_ORDER_BGR, 23},
  {"DD:DD:DD:DD:DD:DD",  4, RGB_ORDER_BGR, 24},
  {"EE:EE:EE:EE:EE:EE",  5, RGB_ORDER_BGR, 25},
  {"FF:FF:FF:FF:FF:FF",  6, RGB_ORDER_BGR, 26},
  {"01:01:01:01:01:01", 11, RGB_ORDER_RGB, 31},
  {"02:02:02:02:02:02", 12, RGB_ORDER_RGB, 32},
  {"03:03:03:03:03:03", 13, RGB_ORDER_RGB, 33},
  {"04:04:04:04:04:04", 14, RGB_ORDER_RGB, 34},
  {"05:05:05:05:05:05", 15, RGB_ORDER_RGB, 35},
  {"06:06:06:06:06:06", 16, RGB_ORDER_RGB, 36},
  {"A1:A1:A1:A1:A1:A1",  1, RGB_ORDER_RGB, 41},
};
#else
// Production MAC addresses - actual hardware
const CubeMacEntry CUBE_MAC_TABLE[] = {
  {"CC:DB:A7:9F:C2:84",  1, RGB_ORDER_BGR, 21},  // 30-pin
  {"3C:8A:1F:77:DF:8C",  2, RGB_ORDER_BGR, 22},  // 30-pin
  {"8C:4F:00:37:7C:DC",  3, RGB_ORDER_BGR, 23},  // 30-pin
  {"CC:DB:A7:9B:5D:9C",  4, RGB_ORDER_BGR, 24},  // 30-pin (moved from cube 11)
  {"04:83:08:59:76:98",  5, RGB_ORDER_BGR, 25},
  {"EC:E3:34:79:8A:BC",  6, RGB_ORDER_BGR, 26},  // 30-pin
  {"94:54:C5:F1:AF:00", 11, RGB_ORDER_RGB, 31},  // 30-pin (EMPTY - chip moved to cube 4)
  {"EC:E3:34:79:9D:2C", 12, RGB_ORDER_RGB, 32},  // 30-pin
  {"04:83:08:59:6E:74", 13, RGB_ORDER_RGB, 33},  // 30-pin
  {"94:54:C5:EE:89:4C", 14, RGB_ORDER_RGB, 34},  // 30-pin
  {"8C:4F:00:36:7A:88", 15, RGB_ORDER_RGB, 35},  // 30-pin
  {"D8:BC:38:F9:39:30", 16, RGB_ORDER_RGB, 36},  // 30-pin
  {"80:F3:DA:54:53:B8",  1, RGB_ORDER_BGR, 41},  // backup slot 1
  {"5C:01:3B:65:46:2C",  2, RGB_ORDER_RGB, 42},  // backup slot 2
  {"5C:01:3B:64:E2:84",  3, RGB_ORDER_RGB, 43},  // backup slot 3
  {"D4:8A:FC:9F:B0:C0",  4, RGB_ORDER_BGR, 44},  // backup slot 4
  {"D8:BC:38:E5:A8:38",  5, RGB_ORDER_RGB, 45},  // backup slot 5
  {"5C:01:3B:4A:87:4C",  6, RGB_ORDER_RGB, 46},  // backup slot 6
};
#endif
const int NUM_CUBE_MAC_ENTRIES = sizeof(CUBE_MAC_TABLE) / sizeof(CUBE_MAC_TABLE[0]);

// MQTT Topic Prefixes
const char* MQTT_TOPIC_PREFIX_CUBE = "cube/";
const char* MQTT_TOPIC_PREFIX_GAME = "game/";
const char* MQTT_TOPIC_PREFIX_NFC = "nfc/";
const char* MQTT_TOPIC_PREFIX_ECHO = "echo";
const char* MQTT_TOPIC_PREFIX_VERSION = "version";

const CubeMacEntry* findCubeEntry(const char *mac_address) {
  for (int i = 0; i < NUM_CUBE_MAC_ENTRIES; i++) {
    if (strcmp(mac_address, CUBE_MAC_TABLE[i].mac) == 0) {
      return &CUBE_MAC_TABLE[i];
    }
  }
  return nullptr;
}

int findCubeId(const char *mac_address) {
  const CubeMacEntry* entry = findCubeEntry(mac_address);
  return entry ? entry->cube_id : -1;
}

int findCubeIpOctet(const char *mac_address) {
  const CubeMacEntry* entry = findCubeEntry(mac_address);
  return entry ? entry->ip_octet : -1;
}

static const char* findJsonValue(const char* json, const char* key) {
  char pattern[24];
  snprintf(pattern, sizeof(pattern), "\"%s\"", key);
  const char* found = strstr(json, pattern);
  if (found == nullptr) {
    return nullptr;
  }
  const char* colon = strchr(found + strlen(pattern), ':');
  if (colon == nullptr) {
    return nullptr;
  }
  const char* value = colon + 1;
  while (isspace(static_cast<unsigned char>(*value))) {
    ++value;
  }
  return value;
}

static bool readJsonInt(const char* json, const char* key, long* out) {
  const char* value = findJsonValue(json, key);
  if (value == nullptr || *value < '0' || *value > '9') {
    return false;
  }
  char* end = nullptr;
  *out = strtol(value, &end, 10);
  while (isspace(static_cast<unsigned char>(*end))) {
    ++end;
  }
  return *end == ',' || *end == '}';
}

AssignmentParseResult parseAssignmentRecord(const char* json, CubeAssignment* out) {
  out->generation = 0;
  out->slot = -1;

  if (json == nullptr || json[0] == '\0') {
    return ASSIGNMENT_MISSING;
  }

  long protocol = 0;
  if (!readJsonInt(json, "protocol", &protocol) || protocol != 1) {
    return ASSIGNMENT_MALFORMED;
  }

  long generation = 0;
  if (!readJsonInt(json, "generation", &generation)) {
    return ASSIGNMENT_MALFORMED;
  }
  out->generation = static_cast<uint32_t>(generation);

  const char* slot_value = findJsonValue(json, "slot");
  if (slot_value == nullptr) {
    return ASSIGNMENT_MALFORMED;
  }
  if (strncmp(slot_value, "null", 4) == 0 &&
      (slot_value[4] == ',' || slot_value[4] == '}' ||
       isspace(static_cast<unsigned char>(slot_value[4])))) {
    return ASSIGNMENT_UNASSIGNED;
  }

  long slot = 0;
  if (!readJsonInt(json, "slot", &slot) || slot < 1 || slot > 16) {
    return ASSIGNMENT_MALFORMED;
  }
  out->slot = static_cast<int>(slot);
  return ASSIGNMENT_OK;
}

bool assignmentRecordIsActionable(AssignmentParseResult result) {
  return result == ASSIGNMENT_OK || result == ASSIGNMENT_UNASSIGNED;
}

int resolveAssignedSlot(AssignmentParseResult result, int record_slot,
                        bool authority_latched, int compiled_cube_id) {
  switch (result) {
    case ASSIGNMENT_OK:
      return record_slot;
    case ASSIGNMENT_UNASSIGNED:
      return -1;
    default:
      return authority_latched ? -1 : compiled_cube_id;
  }
}

WakeAction resolveWakeAction(bool wifi_connected, bool mqtt_connected,
                             bool has_slot_topic,
                             bool device_requests_sleep,
                             bool slot_requests_sleep) {
  if (!wifi_connected || !mqtt_connected) return WAKE_ACTION_STAY_ASLEEP;
  // An assigned cube (has a slot topic) obeys the slot flag, so tools/wake.sh
  // can wake it by clearing that topic; an unassigned cube has no slot topic
  // and falls back to the device flag.
  bool stay_asleep = has_slot_topic ? slot_requests_sleep
                                    : device_requests_sleep;
  return stay_asleep ? WAKE_ACTION_STAY_ASLEEP : WAKE_ACTION_WAKE_FULL;
}

void runWakeCheckIn(WakeReason wake_reason, WakeCheckInPorts& ports) {
  if (wake_reason == WAKE_REASON_BUTTON) { ports.stayAwake(); return; }

  // A reset -- brownout, watchdog, crash, a jostled battery contact -- reads
  // the sleep flag just as a timer wake does. It is not someone deciding to
  // use the cube, so a stored cube that glitches goes back to sleep instead of
  // burning ten minutes of battery and making the next glitch likelier.
  const bool is_reset = (wake_reason == WAKE_REASON_OTHER);

  bool wifi = ports.awaitWifi();
  bool mqtt = wifi && ports.connectMqtt();
  if (!mqtt) {
    // The two wakes disagree about what silence means. A timer wake was
    // already asleep, so leaving it there costs nothing. A reset has no such
    // prior: sleeping because the broker happened to be unreachable would make
    // a working cube look dead to whoever is standing at the cabinet.
    if (is_reset) ports.stayAwake(); else ports.enterSleep();
    return;
  }

  bool has_slot_topic = ports.hasSlotTopic();
  SleepFlags flags = {false, false};
  if (!ports.readSleepFlags(&flags)) {
    // Unconfirmed is not "no flag". Treating it as one is how a cube that
    // merely had a slow link cleared its own sleep flag and stayed awake for
    // ten minutes -- and a weak battery, which causes the slow link, is
    // exactly the cube that can least afford it.
    if (is_reset) ports.stayAwake(); else ports.enterSleep();
    return;
  }
  WakeAction action = resolveWakeAction(wifi, mqtt, has_slot_topic,
                                        flags.device_requests_sleep,
                                        flags.slot_requests_sleep);
  if (action == WAKE_ACTION_STAY_ASLEEP) { ports.enterSleep(); return; }
  ports.clearSleepFlags();
  ports.stayAwake();
}

void convertNfcIdToHexString(uint8_t* nfc_id, int id_length, char* hex_buffer) {
  for (int i = 0; i < id_length; i++) {
    snprintf(hex_buffer + (i * 2), 3, "%02X", nfc_id[i]);
  }
  hex_buffer[id_length * 2] = '\0';
}

NfcObservationAction decideNfcObservation(bool read_ok, bool no_card,
                                          bool hall_allows_neighbor,
                                          bool hall_says_present,
                                          const char* tag_hex,
                                          const char* last_published) {
  if (read_ok) {
    if (!hall_allows_neighbor || tag_hex == nullptr) {
      return NFC_OBS_NONE;
    }
    return strcmp(tag_hex, last_published) == 0 ? NFC_OBS_NONE : NFC_OBS_TAG;
  }
  if (no_card) {
    // "-" only when both sensors agree: hall-present guards an NFC flake.
    if (hall_says_present || strcmp(last_published, "-") == 0) {
      return NFC_OBS_NONE;
    }
    return NFC_OBS_ABSENT;
  }
  return NFC_OBS_NONE;
}

void buildObservationPayload(const char* boot_id, const char* tag,
                             char* out, size_t out_size) {
  // protocol, boot_id, tag. Nothing else: the server validates no provenance,
  // so a field carried here would be complexity with no behaviour.
  snprintf(out, out_size, "{\"protocol\":1,\"boot_id\":\"%s\",\"tag\":\"%s\"}",
           boot_id, tag);
}

#ifdef NATIVE_TESTING
// C versions for native testing
void removeColonsFromMacC(const char* mac_address, char* output, size_t output_size) {
  size_t j = 0;
  for (size_t i = 0; mac_address[i] != '\0' && j < output_size - 1; i++) {
    if (mac_address[i] != ':') {
      output[j++] = mac_address[i];
    }
  }
  output[j] = '\0';
}

void createMqttTopicC(const char* cube_identifier, const char* suffix, char* output, size_t output_size) {
  snprintf(output, output_size, "%s%s/%s", MQTT_TOPIC_PREFIX_CUBE, cube_identifier, suffix);
}

void makeMqttClientIdC(const char* mac_address, const char* suffix, char* output, size_t output_size) {
  char nocolons[32];
  removeColonsFromMacC(mac_address, nocolons, sizeof(nocolons));
  snprintf(output, output_size, "cube-%s%s", nocolons, suffix);
}
#else
// Arduino String versions for ESP32
String removeColonsFromMac(const String& mac_address) {
  String result;
  result.reserve(mac_address.length());
  for (size_t i = 0; i < mac_address.length(); i++) {
    char c = mac_address[i];
    if (c != ':') {
      result += c;
    }
  }
  return result;
}

String createMqttTopic(const String& cube_identifier, const char* suffix) {
  String topic = MQTT_TOPIC_PREFIX_CUBE;
  topic += cube_identifier;
  topic += '/';
  topic += suffix;
  return topic;
}

String makeMqttClientId(const String& mac_address, const char* suffix) {
  return "cube-" + removeColonsFromMac(mac_address) + suffix;
}
#endif
