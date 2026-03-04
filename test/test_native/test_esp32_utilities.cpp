#include <unity.h>
#include "../../src/cube_utilities.h"
#include "../../src/cube_tags.h"

// For testing, include the implementation directly
#include "../../src/cube_utilities.cpp"
#include "../../src/cube_tags.cpp"

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
    TEST_ASSERT_EQUAL(1, findMacAddressPosition("3C:8A:1F:77:DF:8C"));

    // Test sixth cube (position 5)
    TEST_ASSERT_EQUAL(5, findMacAddressPosition("14:33:5C:30:29:EC"));
}

void test_findMacAddressPosition_all_positions() {
    // Test all 12 MAC addresses in the table
    TEST_ASSERT_EQUAL(0, findMacAddressPosition("CC:DB:A7:9F:C2:84"));  // 1
    TEST_ASSERT_EQUAL(1, findMacAddressPosition("3C:8A:1F:77:DF:8C"));  // 2
    TEST_ASSERT_EQUAL(2, findMacAddressPosition("8C:4F:00:37:7C:DC"));  // 3
    TEST_ASSERT_EQUAL(3, findMacAddressPosition("3C:8A:1F:77:B9:24"));  // 4
    TEST_ASSERT_EQUAL(4, findMacAddressPosition("EC:E3:34:B4:8F:B4"));  // 5
    TEST_ASSERT_EQUAL(5, findMacAddressPosition("14:33:5C:30:29:EC"));  // 6
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
    pos = findMacAddressPosition("14:33:5C:30:29:EC");
    TEST_ASSERT_EQUAL(5, pos);
    TEST_ASSERT_EQUAL(6, calculateCubeIdentifier(pos));
    TEST_ASSERT_EQUAL(25, calculateCubeIpOctet(pos));
}

// ========== Cube Tags Tests ==========

void test_lookupCubeNumberByTag_all_known_tags() {
    // Test all 12 known tags
    TEST_ASSERT_EQUAL(1, lookupCubeNumberByTag("A9121466080104E0"));
    TEST_ASSERT_EQUAL(2, lookupCubeNumberByTag("B1FD1366080104E0"));
    TEST_ASSERT_EQUAL(3, lookupCubeNumberByTag("30071466080104E0"));
    TEST_ASSERT_EQUAL(4, lookupCubeNumberByTag("BD291466080104E0"));
    TEST_ASSERT_EQUAL(5, lookupCubeNumberByTag("71E81366080104E0"));
    TEST_ASSERT_EQUAL(6, lookupCubeNumberByTag("361E1466080104E0"));
    TEST_ASSERT_EQUAL(11, lookupCubeNumberByTag("C1A81366080104E0"));
    TEST_ASSERT_EQUAL(12, lookupCubeNumberByTag("829E1366080104E0"));
    TEST_ASSERT_EQUAL(13, lookupCubeNumberByTag("BFBD1366080104E0"));
    TEST_ASSERT_EQUAL(14, lookupCubeNumberByTag("6DB11366080104E0"));
    TEST_ASSERT_EQUAL(15, lookupCubeNumberByTag("32961366080104E0"));
    TEST_ASSERT_EQUAL(16, lookupCubeNumberByTag("FAADF7B8500104E0"));
}

void test_lookupCubeNumberByTag_unknown_tag() {
    // Test completely unknown tag
    TEST_ASSERT_EQUAL(0, lookupCubeNumberByTag("FFFFFFFFFFFFFFFF"));
    TEST_ASSERT_EQUAL(0, lookupCubeNumberByTag("0000000000000000"));
    TEST_ASSERT_EQUAL(0, lookupCubeNumberByTag("1234567890123456"));
}

void test_lookupCubeNumberByTag_null_pointer() {
    // Test NULL pointer handling
    TEST_ASSERT_EQUAL(0, lookupCubeNumberByTag(nullptr));
}

void test_lookupCubeNumberByTag_empty_string() {
    // Test empty string
    TEST_ASSERT_EQUAL(0, lookupCubeNumberByTag(""));
}

void test_lookupCubeNumberByTag_case_sensitivity() {
    // Test that lookup is case-sensitive (all known tags are uppercase)
    TEST_ASSERT_EQUAL(0, lookupCubeNumberByTag("a9121466080104e0"));  // lowercase
    TEST_ASSERT_EQUAL(0, lookupCubeNumberByTag("A9121466080104e0"));  // mixed case
    TEST_ASSERT_EQUAL(1, lookupCubeNumberByTag("A9121466080104E0"));  // uppercase (valid)
}

void test_lookupCubeNumberByTag_partial_match() {
    // Test that partial matches don't work
    TEST_ASSERT_EQUAL(0, lookupCubeNumberByTag("A9121466080104"));    // too short
    TEST_ASSERT_EQUAL(0, lookupCubeNumberByTag("A9121466080104E0AA")); // too long
}

void test_lookupCubeNumberByTag_invalid_hex() {
    // Test with invalid hex characters
    TEST_ASSERT_EQUAL(0, lookupCubeNumberByTag("GGGGGGGGGGGGGGGG"));  // invalid hex chars
    TEST_ASSERT_EQUAL(0, lookupCubeNumberByTag("A9121466080104E "));   // space in middle
    TEST_ASSERT_EQUAL(0, lookupCubeNumberByTag("A912:1466080104E0"));  // with separator
}

void test_lookupCubeNumberByTag_similar_tags() {
    // Test tags that are similar but not exact matches
    // This tests that the entire tag must match exactly
    TEST_ASSERT_EQUAL(0, lookupCubeNumberByTag("A9121466080104E1"));  // last char different
    TEST_ASSERT_EQUAL(0, lookupCubeNumberByTag("09121466080104E0"));  // first char different
    TEST_ASSERT_EQUAL(0, lookupCubeNumberByTag("A9121466080104E00")); // extra char at end
}

void test_num_known_tags() {
    // Test that the known tags table has the expected size
    TEST_ASSERT_EQUAL(12, NUM_KNOWN_TAGS);
}

void test_lookupCubeNumberByTag_tag_to_cube_mapping() {
    // Test the complete tag-to-cube mapping workflow
    // This validates that each tag maps to the correct cube number

    // Primary cubes (1-6)
    int cube_num = lookupCubeNumberByTag("A9121466080104E0");
    TEST_ASSERT_EQUAL(1, cube_num);

    cube_num = lookupCubeNumberByTag("361E1466080104E0");
    TEST_ASSERT_EQUAL(6, cube_num);

    // Extended cubes (11-16)
    cube_num = lookupCubeNumberByTag("C1A81366080104E0");
    TEST_ASSERT_EQUAL(11, cube_num);

    cube_num = lookupCubeNumberByTag("FAADF7B8500104E0");
    TEST_ASSERT_EQUAL(16, cube_num);
}

void test_lookupCubeNumberByTag_whitespace_variations() {
    // Test with various whitespace issues
    TEST_ASSERT_EQUAL(0, lookupCubeNumberByTag(" A9121466080104E0"));  // leading space
    TEST_ASSERT_EQUAL(0, lookupCubeNumberByTag("A9121466080104E0 "));  // trailing space
    TEST_ASSERT_EQUAL(0, lookupCubeNumberByTag("\tA9121466080104E0")); // leading tab
}

// ========== String Utility Tests ==========

void test_removeColonsFromMac_standard_format() {
    // Test standard MAC address format
    char output[20];
    removeColonsFromMacC("CC:DB:A7:9F:C2:84", output, sizeof(output));
    TEST_ASSERT_EQUAL_STRING("CCDBA79FC284", output);

    removeColonsFromMacC("D8:AF:CF:9B:0C:C0", output, sizeof(output));
    TEST_ASSERT_EQUAL_STRING("D8AFCF9B0CC0", output);
}

void test_removeColonsFromMac_no_colons() {
    // Test MAC address without colons (should return as-is)
    char output[20];
    removeColonsFromMacC("CCDBA79FC284", output, sizeof(output));
    TEST_ASSERT_EQUAL_STRING("CCDBA79FC284", output);

    removeColonsFromMacC("123456", output, sizeof(output));
    TEST_ASSERT_EQUAL_STRING("123456", output);
}

void test_removeColonsFromMac_empty_string() {
    // Test empty string
    char output[20];
    removeColonsFromMacC("", output, sizeof(output));
    TEST_ASSERT_EQUAL_STRING("", output);
}

void test_removeColonsFromMac_only_colons() {
    // Test string with only colons
    char output[20];
    removeColonsFromMacC(":::::::", output, sizeof(output));
    TEST_ASSERT_EQUAL_STRING("", output);
}

void test_removeColonsFromMac_mixed_separators() {
    // Test that only colons are removed, other characters preserved
    char output[30];
    removeColonsFromMacC("CC:-:DB:-:A7:-:9F:-:C2:-:84", output, sizeof(output));
    TEST_ASSERT_EQUAL_STRING("CC-DB-A7-9F-C2-84", output);
}

void test_removeColonsFromMac_single_colon() {
    // Test single colon
    char output[20];
    removeColonsFromMacC("ABCDE:F", output, sizeof(output));
    TEST_ASSERT_EQUAL_STRING("ABCDEF", output);

    removeColonsFromMacC(":ABCDEF", output, sizeof(output));
    TEST_ASSERT_EQUAL_STRING("ABCDEF", output);

    removeColonsFromMacC("ABCDEF:", output, sizeof(output));
    TEST_ASSERT_EQUAL_STRING("ABCDEF", output);
}

void test_createMqttTopic_basic() {
    // Test basic topic creation
    char output[50];
    createMqttTopicC("1", "echo", output, sizeof(output));
    TEST_ASSERT_EQUAL_STRING("cube/1/echo", output);

    createMqttTopicC("5", "brightness", output, sizeof(output));
    TEST_ASSERT_EQUAL_STRING("cube/5/brightness", output);

    createMqttTopicC("12", "nfc", output, sizeof(output));
    TEST_ASSERT_EQUAL_STRING("cube/12/nfc", output);
}

void test_createMqttTopic_empty_suffix() {
    // Test with empty suffix
    char output[50];
    createMqttTopicC("1", "", output, sizeof(output));
    TEST_ASSERT_EQUAL_STRING("cube/1/", output);
}

void test_createMqttTopic_special_characters() {
    // Test with special characters in suffix
    char output[50];
    createMqttTopicC("1", "test_123", output, sizeof(output));
    TEST_ASSERT_EQUAL_STRING("cube/1/test_123", output);

    createMqttTopicC("5", "sub/topic", output, sizeof(output));
    TEST_ASSERT_EQUAL_STRING("cube/5/sub/topic", output);
}

void test_createMqttTopic_long_identifiers() {
    // Test with longer cube identifiers
    char output[50];
    createMqttTopicC("cube123", "echo", output, sizeof(output));
    TEST_ASSERT_EQUAL_STRING("cube/cube123/echo", output);
}

void test_createMqttTopic_constants() {
    // Test that prefix constant is used correctly
    char output[50];
    createMqttTopicC("1", "test", output, sizeof(output));

    // Should start with "cube/"
    TEST_ASSERT_TRUE(strncmp(output, "cube/", 5) == 0);

    // Should contain the identifier
    TEST_ASSERT_TRUE(strstr(output, "1") != NULL);

    // Should contain the suffix
    TEST_ASSERT_TRUE(strstr(output, "test") != NULL);
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

    // Cube tags lookup tests
    RUN_TEST(test_lookupCubeNumberByTag_all_known_tags);
    RUN_TEST(test_lookupCubeNumberByTag_tag_to_cube_mapping);
    RUN_TEST(test_lookupCubeNumberByTag_unknown_tag);
    RUN_TEST(test_lookupCubeNumberByTag_null_pointer);
    RUN_TEST(test_lookupCubeNumberByTag_empty_string);
    RUN_TEST(test_lookupCubeNumberByTag_case_sensitivity);
    RUN_TEST(test_lookupCubeNumberByTag_partial_match);
    RUN_TEST(test_lookupCubeNumberByTag_invalid_hex);
    RUN_TEST(test_lookupCubeNumberByTag_similar_tags);
    RUN_TEST(test_lookupCubeNumberByTag_whitespace_variations);
    RUN_TEST(test_num_known_tags);

    // String utility tests
    RUN_TEST(test_removeColonsFromMac_standard_format);
    RUN_TEST(test_removeColonsFromMac_no_colons);
    RUN_TEST(test_removeColonsFromMac_empty_string);
    RUN_TEST(test_removeColonsFromMac_only_colons);
    RUN_TEST(test_removeColonsFromMac_mixed_separators);
    RUN_TEST(test_removeColonsFromMac_single_colon);
    RUN_TEST(test_createMqttTopic_basic);
    RUN_TEST(test_createMqttTopic_empty_suffix);
    RUN_TEST(test_createMqttTopic_special_characters);
    RUN_TEST(test_createMqttTopic_long_identifiers);
    RUN_TEST(test_createMqttTopic_constants);

    return UNITY_END();
}