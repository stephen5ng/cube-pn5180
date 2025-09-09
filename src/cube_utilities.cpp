#include "cube_utilities.h"

// MAC Address Table - Main screens only
const char *CUBE_MAC_ADDRESSES[] = {
  "CC:DB:A7:95:E7:70",  // 1
  "D4:8A:FC:9F:B0:C0", // 2 long ext antenna
  "EC:C9:FF:51:4C:80",  // 3 square
  "C0:5D:89:A8:ED:2C",  // 4 square ext antenna
  "5C:01:3B:4A:CB:D4", // 5 long int antenna
  "D4:8A:FC:9F:BF:50", // 6 long ext antenna
  "94:54:C5:EE:89:4C",  // 7
  "C4:DD:57:8E:46:C8",  // 8
  "D8:BC:38:FD:D0:BC",  // 9
  "94:54:C5:F1:AF:00",  // 10
  "CC:DB:A7:9B:5D:9C",  // 11
  "94:54:C5:EE:87:F0",  // 12
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