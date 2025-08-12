#include "cube_utilities.h"

// MAC Address Table
const char *CUBE_MAC_ADDRESSES[] = {
  // MAIN SCREEN, FRONT SCREEN
  "CC:DB:A7:95:E7:70", "Z94:54:C5:EE:87:F0",
  "EC:E3:34:B4:8F:B4", "ZD8:BC:38:FD:E0:98",
  "EC:E3:34:79:8A:BC", "ZCC:DB:A7:99:0F:E0", 
  "04:83:08:59:6E:74", "ZD8:BC:38:FD:D0:BC",
  "14:33:5C:30:25:98", "Z8C:4F:00:2E:58:40",
  "EC:E3:34:79:9D:2C", "z8C:4F:00:2E:58:40",
};
const int NUM_CUBE_MAC_ADDRESSES = sizeof(CUBE_MAC_ADDRESSES) / sizeof(CUBE_MAC_ADDRESSES[0]);

// MQTT Topic Prefixes
const char* MQTT_TOPIC_PREFIX_CUBE = "cube/";
const char* MQTT_TOPIC_PREFIX_GAME = "game/";
const char* MQTT_TOPIC_PREFIX_NFC = "nfc/";
const char* MQTT_TOPIC_PREFIX_ECHO = "echo";

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

#ifndef NATIVE_TESTING
String removeColonsFromMac(const String& mac_address) {
  String result;
  result.reserve(mac_address.length());
  for (size_t i = 0; i < mac_address.length(); i++) {
    if (mac_address.charAt(i) != ':') {
      result += mac_address.charAt(i); 
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