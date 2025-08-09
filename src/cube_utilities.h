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

// Utility functions
int findMacAddressPosition(const char *mac_address);
void convertNfcIdToHexString(uint8_t* nfc_id, int id_length, char* hex_buffer);

#ifndef NATIVE_TESTING
// Arduino-specific functions
String removeColonsFromMac(const String& mac_address);
String createMqttTopic(const String& cube_identifier, const char* suffix);
#endif