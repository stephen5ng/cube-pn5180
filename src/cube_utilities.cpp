#include "cube_utilities.h"

// MAC Address Table - Main screens only
const char *CUBE_MAC_ADDRESSES[] = {
  "CC:DB:A7:9F:C2:84",  // 1 30-pin
  "3C:8A:1F:77:DF:8C",  // 2 30-pin
  "8C:4F:00:37:7C:DC",  // 3 30-pin
  "3C:8A:1F:77:B9:24",  // 4 30-pin
  "EC:E3:34:B4:8F:B4",  // 5 30-pin (New chip)
  "04:83:08:58:2B:40",  // 6 30-pin
  "CC:DB:A7:9B:5D:9C",  // 7 -  11 30-pin 
  "EC:E3:34:79:9D:2C",  // 8 -  12 30-pin
  "04:83:08:59:6E:74",  // 9  - 13 30-pin
  "94:54:C5:EE:89:4C",  // 10 - 14 30-pin
  "8C:4F:00:36:7A:88",  // 11 - 15 30-pin
  "D8:BC:38:F9:39:30",  // 12 - 16 30-pin
};
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