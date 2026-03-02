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
    // Test first cube (position 0)
    TEST_ASSERT_EQUAL(0, findMacAddressPosition("CC:DB:A7:9F:C2:84"));

    // Test second cube (position 1)
    TEST_ASSERT_EQUAL(1, findMacAddressPosition("D4:8A:FC:9F:B0:C0"));

    // Test sixth cube (position 5)
    TEST_ASSERT_EQUAL(5, findMacAddressPosition("14:33:5C:30:25:98"));
}

void test_findMacAddressPosition_all_positions() {
    // Test all 12 MAC addresses in the table
    TEST_ASSERT_EQUAL(0, findMacAddressPosition("CC:DB:A7:9F:C2:84"));  // 1
    TEST_ASSERT_EQUAL(1, findMacAddressPosition("D4:8A:FC:9F:B0:C0"));  // 2
    TEST_ASSERT_EQUAL(2, findMacAddressPosition("8C:4F:00:37:7C:DC"));  // 3
    TEST_ASSERT_EQUAL(3, findMacAddressPosition("5C:01:3B:64:E2:84"));  // 4
    TEST_ASSERT_EQUAL(4, findMacAddressPosition("EC:E3:34:B4:8F:B4"));  // 5
    TEST_ASSERT_EQUAL(5, findMacAddressPosition("14:33:5C:30:25:98"));  // 6
    TEST_ASSERT_EQUAL(6, findMacAddressPosition("CC:DB:A7:9B:5D:9C"));  // 7
    TEST_ASSERT_EQUAL(7, findMacAddressPosition("EC:E3:34:79:9D:2C"));  // 8
    TEST_ASSERT_EQUAL(8, findMacAddressPosition("04:83:08:59:6E:74"));  // 9
    TEST_ASSERT_EQUAL(9, findMacAddressPosition("94:54:C5:EE:89:4C"));  // 10
    TEST_ASSERT_EQUAL(10, findMacAddressPosition("8C:4F:00:36:7A:88")); // 11
    TEST_ASSERT_EQUAL(11, findMacAddressPosition("D8:BC:38:F9:39:30")); // 12
}

void test_findMacAddressPosition_unknown_address() {
    TEST_ASSERT_EQUAL(-1, findMacAddressPosition("AA:BB:CC:DD:EE:FF"));
    TEST_ASSERT_EQUAL(-1, findMacAddressPosition(""));
    TEST_ASSERT_EQUAL(-1, findMacAddressPosition("INVALID"));
}

void test_findMacAddressPosition_case_sensitivity() {
    // Test that MAC address lookup is case-sensitive
    // Lowercase should not match uppercase entries
    TEST_ASSERT_EQUAL(-1, findMacAddressPosition("cc:db:a7:9f:c2:84"));  // lowercase
    TEST_ASSERT_EQUAL(-1, findMacAddressPosition("CC:DB:A7:9F:C2:84:00"));  // too long
    TEST_ASSERT_EQUAL(-1, findMacAddressPosition("CC:DB:A7:9F:C2"));  // too short
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

void test_convertNfcIdToHexString_single_byte() {
    // Test single byte conversion
    uint8_t single_byte[] = {0xAB};
    char buffer[3];
    convertNfcIdToHexString(single_byte, 1, buffer);
    TEST_ASSERT_EQUAL_STRING("AB", buffer);
}

void test_convertNfcIdToHexString_mixed_values() {
    // Test with mixed byte values (low, mid, high)
    uint8_t mixed[] = {0x01, 0x80, 0xFF, 0x0A};
    char buffer[9];
    convertNfcIdToHexString(mixed, 4, buffer);
    TEST_ASSERT_EQUAL_STRING("0180FF0A", buffer);
}

void test_num_cube_mac_addresses() {
    // Test that the MAC address table has the expected size
    TEST_ASSERT_EQUAL(12, NUM_CUBE_MAC_ADDRESSES);
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

void test_calculateCubeIdentifier_extended() {
    // Test extended cube IDs (positions 6-11)
    TEST_ASSERT_EQUAL(7, calculateCubeIdentifier(6));   // Cube 7
    TEST_ASSERT_EQUAL(8, calculateCubeIdentifier(7));   // Cube 8
    TEST_ASSERT_EQUAL(9, calculateCubeIdentifier(8));   // Cube 9
    TEST_ASSERT_EQUAL(10, calculateCubeIdentifier(9));  // Cube 10
    TEST_ASSERT_EQUAL(11, calculateCubeIdentifier(10)); // Cube 11
    TEST_ASSERT_EQUAL(12, calculateCubeIdentifier(11)); // Cube 12
}


void test_mac_to_cube_configuration_integration() {
    // Test complete workflow: MAC -> position -> cube config

    // Test Cube 1 (position 0)
    int pos = findMacAddressPosition("CC:DB:A7:9F:C2:84");
    TEST_ASSERT_EQUAL(0, pos);
    TEST_ASSERT_EQUAL(1, calculateCubeIdentifier(pos));
    TEST_ASSERT_EQUAL(20, calculateCubeIpOctet(pos));

    // Test Cube 6 (position 5)
    pos = findMacAddressPosition("14:33:5C:30:25:98");
    TEST_ASSERT_EQUAL(5, pos);
    TEST_ASSERT_EQUAL(6, calculateCubeIdentifier(pos));
    TEST_ASSERT_EQUAL(25, calculateCubeIpOctet(pos));
}

int main(void) {
    UNITY_BEGIN();

    // MAC address lookup tests
    RUN_TEST(test_findMacAddressPosition_known_addresses);
    RUN_TEST(test_findMacAddressPosition_all_positions);
    RUN_TEST(test_findMacAddressPosition_unknown_address);
    RUN_TEST(test_findMacAddressPosition_case_sensitivity);

    // NFC ID conversion tests
    RUN_TEST(test_convertNfcIdToHexString_full_id);
    RUN_TEST(test_convertNfcIdToHexString_partial_id);
    RUN_TEST(test_convertNfcIdToHexString_single_byte);
    RUN_TEST(test_convertNfcIdToHexString_mixed_values);
    RUN_TEST(test_convertNfcIdToHexString_edge_cases);

    // Cube configuration tests
    RUN_TEST(test_calculateCubeIpOctet);
    RUN_TEST(test_calculateCubeIdentifier);
    RUN_TEST(test_calculateCubeIdentifier_extended);
    RUN_TEST(test_num_cube_mac_addresses);

    // Integration tests
    RUN_TEST(test_mac_to_cube_configuration_integration);

    return UNITY_END();
}