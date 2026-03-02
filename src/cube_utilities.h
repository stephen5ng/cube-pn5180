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

// MAC Address Table
extern const char *CUBE_MAC_ADDRESSES[];
extern const int NUM_CUBE_MAC_ADDRESSES;

// MQTT Topic Prefixes
extern const char* MQTT_TOPIC_PREFIX_CUBE;
extern const char* MQTT_TOPIC_PREFIX_GAME;
extern const char* MQTT_TOPIC_PREFIX_NFC;
extern const char* MQTT_TOPIC_PREFIX_ECHO;
extern const char* MQTT_TOPIC_PREFIX_VERSION;

// Utility functions
int findMacAddressPosition(const char *mac_address);
void convertNfcIdToHexString(uint8_t* nfc_id, int id_length, char* hex_buffer);

#ifdef NATIVE_TESTING
// Native C versions for testing
void removeColonsFromMacC(const char* mac_address, char* output, size_t output_size);
void createMqttTopicC(const char* cube_identifier, const char* suffix, char* output, size_t output_size);
#else
// Arduino String versions for ESP32
String removeColonsFromMac(const String& mac_address);
String createMqttTopic(const String& cube_identifier, const char* suffix);
#endif