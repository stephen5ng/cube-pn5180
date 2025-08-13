#include <unity.h>
#include "../../src/cube_utilities.h"

// For testing, include the implementation directly
#include "../../src/cube_utilities.cpp"

// Derived functions for testing cube configuration logic
uint8_t calculateCubeIpOctet(int mac_position) {
  if (mac_position == -1) {
    mac_position = 21;  // Default fallback from getCubeIpOctet
  }
  return 20 + mac_position;
}

int calculateCubeIdentifier(int mac_position) {
  return 1 + mac_position;  // Updated: each MAC = one cube
}


// Test functions
void setUp(void) {}
void tearDown(void) {}

void test_findMacAddressPosition_known_addresses() {
    // Test first cube main display
    TEST_ASSERT_EQUAL(0, findMacAddressPosition("CC:DB:A7:95:E7:70"));
    
    
    // Test second cube main display  
    TEST_ASSERT_EQUAL(1, findMacAddressPosition("EC:E3:34:B4:8F:B4"));
    
    // Test last cube main display
    TEST_ASSERT_EQUAL(5, findMacAddressPosition("EC:E3:34:79:9D:2C"));
}

void test_findMacAddressPosition_unknown_address() {
    TEST_ASSERT_EQUAL(-1, findMacAddressPosition("AA:BB:CC:DD:EE:FF"));
    TEST_ASSERT_EQUAL(-1, findMacAddressPosition(""));
    TEST_ASSERT_EQUAL(-1, findMacAddressPosition("INVALID"));
}

void test_convertNfcIdToHexString_full_id() {
    uint8_t nfc_id[] = {0xdd, 0x11, 0xf8, 0xb8, 0x50, 0x01, 0x04, 0xe0};
    char hex_buffer[17]; // 8 bytes * 2 + null terminator
    
    convertNfcIdToHexString(nfc_id, 8, hex_buffer);
    TEST_ASSERT_EQUAL_STRING("DD11F8B8500104E0", hex_buffer);
}

void test_convertNfcIdToHexString_partial_id() {
    uint8_t short_id[] = {0xaa, 0xbb, 0xcc};
    char hex_buffer[7];
    
    convertNfcIdToHexString(short_id, 3, hex_buffer);
    TEST_ASSERT_EQUAL_STRING("AABBCC", hex_buffer);
}

void test_convertNfcIdToHexString_edge_cases() {
    // Test with zero values
    uint8_t zero_id[] = {0x00, 0x00};
    char zero_buffer[5];
    convertNfcIdToHexString(zero_id, 2, zero_buffer);
    TEST_ASSERT_EQUAL_STRING("0000", zero_buffer);
    
    // Test with max values
    uint8_t max_id[] = {0xFF, 0xFF};
    char max_buffer[5];
    convertNfcIdToHexString(max_id, 2, max_buffer);
    TEST_ASSERT_EQUAL_STRING("FFFF", max_buffer);
}

void test_calculateCubeIpOctet() {
    // Test known positions
    TEST_ASSERT_EQUAL(20, calculateCubeIpOctet(0));   // First MAC
    TEST_ASSERT_EQUAL(21, calculateCubeIpOctet(1));   // Second MAC
    TEST_ASSERT_EQUAL(25, calculateCubeIpOctet(5));   // Sixth MAC
    
    // Test fallback case
    TEST_ASSERT_EQUAL(41, calculateCubeIpOctet(-1));  // Unknown MAC -> position 21 -> IP 41
}

void test_calculateCubeIdentifier() {
    // Test cube ID calculation (each MAC = one cube)
    TEST_ASSERT_EQUAL(1, calculateCubeIdentifier(0));  // Cube 1
    TEST_ASSERT_EQUAL(2, calculateCubeIdentifier(1));  // Cube 2
    TEST_ASSERT_EQUAL(3, calculateCubeIdentifier(2));  // Cube 3
    TEST_ASSERT_EQUAL(4, calculateCubeIdentifier(3));  // Cube 4
    TEST_ASSERT_EQUAL(5, calculateCubeIdentifier(4));  // Cube 5
    TEST_ASSERT_EQUAL(6, calculateCubeIdentifier(5));  // Cube 6
}


void test_mac_to_cube_configuration_integration() {
    // Test complete workflow: MAC -> position -> cube config
    
    // Test Cube 1 main display
    int pos = findMacAddressPosition("CC:DB:A7:95:E7:70");
    TEST_ASSERT_EQUAL(0, pos);
    TEST_ASSERT_EQUAL(1, calculateCubeIdentifier(pos));
    TEST_ASSERT_EQUAL(20, calculateCubeIpOctet(pos));
    
    // Test Cube 6 (last in table)
    pos = findMacAddressPosition("EC:E3:34:79:9D:2C");
    TEST_ASSERT_EQUAL(5, pos);
    TEST_ASSERT_EQUAL(6, calculateCubeIdentifier(pos));
    TEST_ASSERT_EQUAL(25, calculateCubeIpOctet(pos));
}

int main(void) {
    UNITY_BEGIN();
    
    // MAC address lookup tests
    RUN_TEST(test_findMacAddressPosition_known_addresses);
    RUN_TEST(test_findMacAddressPosition_unknown_address);
    
    // NFC ID conversion tests
    RUN_TEST(test_convertNfcIdToHexString_full_id);
    RUN_TEST(test_convertNfcIdToHexString_partial_id);
    RUN_TEST(test_convertNfcIdToHexString_edge_cases);
    
    // Cube configuration tests
    RUN_TEST(test_calculateCubeIpOctet);
    RUN_TEST(test_calculateCubeIdentifier);
    
    // Integration tests
    RUN_TEST(test_mac_to_cube_configuration_integration);
    
    return UNITY_END();
}