#include "cube_utilities.h"

#ifdef NATIVE_TESTING
// Test MAC addresses - stable values that never change
// Hardware replacements should NOT require test updates
const char *CUBE_MAC_ADDRESSES[] = {
  "AA:AA:AA:AA:AA:AA",  // Test cube 1
  "BB:BB:BB:BB:BB:BB",  // Test cube 2
  "CC:CC:CC:CC:CC:CC",  // Test cube 3
  "DD:DD:DD:DD:DD:DD",  // Test cube 4
  "EE:EE:EE:EE:EE:EE",  // Test cube 5
  "FF:FF:FF:FF:FF:FF",  // Test cube 6
  "01:01:01:01:01:01",  // Test cube 7
  "02:02:02:02:02:02",  // Test cube 8
  "03:03:03:03:03:03",  // Test cube 9
  "04:04:04:04:04:04",  // Test cube 10
  "05:05:05:05:05:05",  // Test cube 11
  "06:06:06:06:06:06",  // Test cube 12
};
#else
// Production MAC addresses - actual hardware
const char *CUBE_MAC_ADDRESSES[] = {
  "CC:DB:A7:9F:C2:84",  // 1 30-pin
  "3C:8A:1F:77:DF:8C",  // 2 30-pin
  "8C:4F:00:37:7C:DC",  // 3 30-pin
  "94:54:C5:F1:AF:00",  // 4 30-pin
  "14:33:5C:30:25:98",  // 5 30-pin
  "EC:E3:34:79:8A:BC",  // 6 30-pin (Genuine Espressif replacement)
  "CC:DB:A7:9B:5D:9C",  // 7 -  11 30-pin
  "EC:E3:34:79:9D:2C",  // 8 -  12 30-pin
  "04:83:08:59:6E:74",  // 9  - 13 30-pin
  "94:54:C5:EE:89:4C",  // 10 - 14 30-pin
  "8C:4F:00:36:7A:88",  // 11 - 15 30-pin
  "D8:BC:38:F9:39:30",  // 12 - 16 30-pin
};
#endif
const int NUM_CUBE_MAC_ADDRESSES = sizeof(CUBE_MAC_ADDRESSES) / sizeof(CUBE_MAC_ADDRESSES[0]);

// MQTT Topic Prefixes
const char* MQTT_TOPIC_PREFIX_CUBE = "cube/";
const char* MQTT_TOPIC_PREFIX_GAME = "game/";
const char* MQTT_TOPIC_PREFIX_NFC = "nfc/";
const char* MQTT_TOPIC_PREFIX_ECHO = "echo";
const char* MQTT_TOPIC_PREFIX_VERSION = "version";

int findMacAddressPosition(const char *mac_address) {
  for (int i = 0; i < NUM_CUBE_MAC_ADDRESSES; i++) {
    if (strcmp(mac_address, CUBE_MAC_ADDRESSES[i]) == 0) {
      return i;
    }
  }
  return -1;
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