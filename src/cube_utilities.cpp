#include "cube_utilities.h"

#ifdef NATIVE_TESTING
// Test MAC table - stable values that never change
// Hardware replacements should NOT require test updates
const CubeMacEntry CUBE_MAC_TABLE[] = {
  {"AA:AA:AA:AA:AA:AA",  1, RGB_ORDER_BGR},
  {"BB:BB:BB:BB:BB:BB",  2, RGB_ORDER_BGR},
  {"CC:CC:CC:CC:CC:CC",  3, RGB_ORDER_BGR},
  {"DD:DD:DD:DD:DD:DD",  4, RGB_ORDER_BGR},
  {"EE:EE:EE:EE:EE:EE",  5, RGB_ORDER_BGR},
  {"FF:FF:FF:FF:FF:FF",  6, RGB_ORDER_BGR},
  {"01:01:01:01:01:01", 11, RGB_ORDER_RGB},
  {"02:02:02:02:02:02", 12, RGB_ORDER_RGB},
  {"03:03:03:03:03:03", 13, RGB_ORDER_RGB},
  {"04:04:04:04:04:04", 14, RGB_ORDER_RGB},
  {"05:05:05:05:05:05", 15, RGB_ORDER_RGB},
  {"06:06:06:06:06:06", 16, RGB_ORDER_RGB},
  {"A1:A1:A1:A1:A1:A1",  1, RGB_ORDER_RGB},  // backup with R/B swapped
};
#else
// Production MAC addresses - actual hardware
const CubeMacEntry CUBE_MAC_TABLE[] = {
  {"CC:DB:A7:9F:C2:84",  1, RGB_ORDER_BGR},  // 30-pin
  {"3C:8A:1F:77:DF:8C",  2, RGB_ORDER_BGR},  // 30-pin
  {"8C:4F:00:37:7C:DC",  3, RGB_ORDER_BGR},  // 30-pin
  {"CC:DB:A7:9B:5D:9C",  4, RGB_ORDER_BGR},  // 30-pin (moved from cube 11)
  {"5C:01:3B:4A:CB:D4",  5, RGB_ORDER_BGR},  // 38-pin
  {"EC:E3:34:79:8A:BC",  6, RGB_ORDER_BGR},  // 30-pin (Genuine Espressif replacement)
  {"94:54:C5:F1:AF:00", 11, RGB_ORDER_RGB},  // 30-pin (EMPTY - chip moved to cube 4)
  {"EC:E3:34:79:9D:2C", 12, RGB_ORDER_RGB},  // 30-pin
  {"04:83:08:59:6E:74", 13, RGB_ORDER_RGB},  // 30-pin
  {"94:54:C5:EE:89:4C", 14, RGB_ORDER_RGB},  // 30-pin
  {"8C:4F:00:36:7A:88", 15, RGB_ORDER_RGB},  // 30-pin
  {"D8:BC:38:F9:39:30", 16, RGB_ORDER_RGB},  // 30-pin
  {"80:F3:DA:54:53:B8",  1, RGB_ORDER_BGR},
  {"5C:01:3B:65:46:2C",  2, RGB_ORDER_RGB},
  {"D4:8A:FC:9F:B0:C0",  4, RGB_ORDER_BGR},  // 38-pin backup
  {"D8:BC:38:E5:A8:38",  5, RGB_ORDER_RGB},  // backup
  {"5C:01:3B:4A:87:4C",  6, RGB_ORDER_RGB},  // backup
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

void convertNfcIdToHexString(uint8_t* nfc_id, int id_length, char* hex_buffer) {
  for (int i = 0; i < id_length; i++) {
    snprintf(hex_buffer + (i * 2), 3, "%02X", nfc_id[i]);
  }
  hex_buffer[id_length * 2] = '\0';
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
#endif
