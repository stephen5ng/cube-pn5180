#include <unity.h>
#include "../../src/cube_utilities.h"
#include "../../src/cube_tags.h"

// For testing, include the implementation directly
#include "../../src/cube_utilities.cpp"
#include "../../src/cube_tags.cpp"

// Test functions
void setUp(void) {}
void tearDown(void) {}

void test_findCubeId_known_addresses() {
    TEST_ASSERT_EQUAL(1, findCubeId("AA:AA:AA:AA:AA:AA"));
    TEST_ASSERT_EQUAL(2, findCubeId("BB:BB:BB:BB:BB:BB"));
    TEST_ASSERT_EQUAL(6, findCubeId("FF:FF:FF:FF:FF:FF"));
}

void test_findCubeId_all_cubes() {
    // Test all cube IDs using test MACs
    TEST_ASSERT_EQUAL( 1, findCubeId("AA:AA:AA:AA:AA:AA"));
    TEST_ASSERT_EQUAL( 2, findCubeId("BB:BB:BB:BB:BB:BB"));
    TEST_ASSERT_EQUAL( 3, findCubeId("CC:CC:CC:CC:CC:CC"));
    TEST_ASSERT_EQUAL( 4, findCubeId("DD:DD:DD:DD:DD:DD"));
    TEST_ASSERT_EQUAL( 5, findCubeId("EE:EE:EE:EE:EE:EE"));
    TEST_ASSERT_EQUAL( 6, findCubeId("FF:FF:FF:FF:FF:FF"));
    TEST_ASSERT_EQUAL(11, findCubeId("01:01:01:01:01:01"));
    TEST_ASSERT_EQUAL(12, findCubeId("02:02:02:02:02:02"));
    TEST_ASSERT_EQUAL(13, findCubeId("03:03:03:03:03:03"));
    TEST_ASSERT_EQUAL(14, findCubeId("04:04:04:04:04:04"));
    TEST_ASSERT_EQUAL(15, findCubeId("05:05:05:05:05:05"));
    TEST_ASSERT_EQUAL(16, findCubeId("06:06:06:06:06:06"));
}

void test_findCubeId_unknown_address() {
    TEST_ASSERT_EQUAL(-1, findCubeId("AA:BB:CC:DD:EE:FF"));
    TEST_ASSERT_EQUAL(-1, findCubeId(""));
    TEST_ASSERT_EQUAL(-1, findCubeId("INVALID"));
}

void test_findCubeId_case_sensitivity() {
    TEST_ASSERT_EQUAL(-1, findCubeId("cc:db:a7:9f:c2:84"));  // lowercase
    TEST_ASSERT_EQUAL(-1, findCubeId("CC:DB:A7:9F:C2:84:00"));  // too long
    TEST_ASSERT_EQUAL(-1, findCubeId("CC:DB:A7:9F:C2"));  // too short
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

void test_num_cube_mac_entries() {
    TEST_ASSERT_EQUAL(13, NUM_CUBE_MAC_ENTRIES);
}

void test_findCubeId_backup_cubes() {
    // Backup MAC should return same cube ID as primary
    TEST_ASSERT_EQUAL(1, findCubeId("A1:A1:A1:A1:A1:A1"));
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

    // MAC-to-cube-ID lookup tests
    RUN_TEST(test_findCubeId_known_addresses);
    RUN_TEST(test_findCubeId_all_cubes);
    RUN_TEST(test_findCubeId_unknown_address);
    RUN_TEST(test_findCubeId_case_sensitivity);
    RUN_TEST(test_num_cube_mac_entries);
    RUN_TEST(test_findCubeId_backup_cubes);

    // NFC ID conversion tests
    RUN_TEST(test_convertNfcIdToHexString_full_id);
    RUN_TEST(test_convertNfcIdToHexString_partial_id);
    RUN_TEST(test_convertNfcIdToHexString_single_byte);
    RUN_TEST(test_convertNfcIdToHexString_mixed_values);
    RUN_TEST(test_convertNfcIdToHexString_edge_cases);

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