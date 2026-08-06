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

void test_findCubeIpOctet_primaries() {
    TEST_ASSERT_EQUAL(21, findCubeIpOctet("AA:AA:AA:AA:AA:AA"));
    TEST_ASSERT_EQUAL(26, findCubeIpOctet("FF:FF:FF:FF:FF:FF"));
    TEST_ASSERT_EQUAL(31, findCubeIpOctet("01:01:01:01:01:01"));
    TEST_ASSERT_EQUAL(36, findCubeIpOctet("06:06:06:06:06:06"));
}

void test_findCubeIpOctet_backup_is_unique() {
    TEST_ASSERT_EQUAL(1, findCubeId("A1:A1:A1:A1:A1:A1"));
    TEST_ASSERT_EQUAL(41, findCubeIpOctet("A1:A1:A1:A1:A1:A1"));
    TEST_ASSERT_NOT_EQUAL(findCubeIpOctet("AA:AA:AA:AA:AA:AA"),
                          findCubeIpOctet("A1:A1:A1:A1:A1:A1"));
}

void test_findCubeIpOctet_unknown() {
    TEST_ASSERT_EQUAL(-1, findCubeIpOctet("AA:BB:CC:DD:EE:FF"));
    TEST_ASSERT_EQUAL(-1, findCubeIpOctet(""));
}

void test_parseAssignmentRecord_assigned() {
    CubeAssignment assignment;
    TEST_ASSERT_EQUAL(ASSIGNMENT_OK,
        parseAssignmentRecord("{\"protocol\": 1, \"generation\": 3, \"slot\": 4}", &assignment));
    TEST_ASSERT_EQUAL(3, assignment.generation);
    TEST_ASSERT_EQUAL(4, assignment.slot);
    TEST_ASSERT_EQUAL(ASSIGNMENT_OK,
        parseAssignmentRecord("{\"slot\":16,\"generation\":7,\"protocol\":1}", &assignment));
    TEST_ASSERT_EQUAL(7, assignment.generation);
    TEST_ASSERT_EQUAL(16, assignment.slot);
}

void test_parseAssignmentRecord_unassigned() {
    CubeAssignment assignment;
    TEST_ASSERT_EQUAL(ASSIGNMENT_UNASSIGNED,
        parseAssignmentRecord("{\"protocol\": 1, \"generation\": 5, \"slot\": null}", &assignment));
    TEST_ASSERT_EQUAL(5, assignment.generation);
    TEST_ASSERT_EQUAL(-1, assignment.slot);
}

void test_parseAssignmentRecord_missing() {
    CubeAssignment assignment;
    TEST_ASSERT_EQUAL(ASSIGNMENT_MISSING, parseAssignmentRecord(nullptr, &assignment));
    TEST_ASSERT_EQUAL(ASSIGNMENT_MISSING, parseAssignmentRecord("", &assignment));
}

void test_parseAssignmentRecord_malformed() {
    CubeAssignment assignment;
    TEST_ASSERT_EQUAL(ASSIGNMENT_MALFORMED,
        parseAssignmentRecord("{\"protocol\": 2, \"generation\": 1, \"slot\": 4}", &assignment));
    TEST_ASSERT_EQUAL(ASSIGNMENT_MALFORMED,
        parseAssignmentRecord("{\"generation\": 1, \"slot\": 4}", &assignment));
    TEST_ASSERT_EQUAL(ASSIGNMENT_MALFORMED,
        parseAssignmentRecord("{\"protocol\": 1, \"slot\": 4}", &assignment));
    TEST_ASSERT_EQUAL(ASSIGNMENT_MALFORMED,
        parseAssignmentRecord("{\"protocol\": 1, \"generation\": 1}", &assignment));
    TEST_ASSERT_EQUAL(ASSIGNMENT_MALFORMED,
        parseAssignmentRecord("{\"protocol\": 1, \"generation\": 1, \"slot\": 0}", &assignment));
    TEST_ASSERT_EQUAL(ASSIGNMENT_MALFORMED,
        parseAssignmentRecord("{\"protocol\": 1, \"generation\": 1, \"slot\": 17}", &assignment));
    TEST_ASSERT_EQUAL(ASSIGNMENT_MALFORMED,
        parseAssignmentRecord("{\"protocol\": 1, \"generation\": 1, \"slot\": nullify}", &assignment));
    TEST_ASSERT_EQUAL(ASSIGNMENT_MALFORMED,
        parseAssignmentRecord("{\"protocol\": 1, \"generation\": 1x, \"slot\": 4}", &assignment));
    TEST_ASSERT_EQUAL(ASSIGNMENT_MALFORMED, parseAssignmentRecord("garbage", &assignment));
}

void test_resolveAssignedSlot() {
    TEST_ASSERT_EQUAL(4, resolveAssignedSlot(ASSIGNMENT_OK, 4, false, 1));
    TEST_ASSERT_EQUAL(4, resolveAssignedSlot(ASSIGNMENT_OK, 4, true, 1));
    TEST_ASSERT_EQUAL(-1, resolveAssignedSlot(ASSIGNMENT_UNASSIGNED, -1, false, 1));
    TEST_ASSERT_EQUAL(-1, resolveAssignedSlot(ASSIGNMENT_UNASSIGNED, -1, true, 1));
    TEST_ASSERT_EQUAL(1, resolveAssignedSlot(ASSIGNMENT_MISSING, -1, false, 1));
    TEST_ASSERT_EQUAL(1, resolveAssignedSlot(ASSIGNMENT_MALFORMED, -1, false, 1));
    TEST_ASSERT_EQUAL(-1, resolveAssignedSlot(ASSIGNMENT_MISSING, -1, true, 1));
    TEST_ASSERT_EQUAL(-1, resolveAssignedSlot(ASSIGNMENT_MALFORMED, -1, true, 1));
    TEST_ASSERT_EQUAL(-1, resolveAssignedSlot(ASSIGNMENT_MISSING, -1, false, -1));
}

void test_assignmentRecordIsActionable() {
    TEST_ASSERT_TRUE(assignmentRecordIsActionable(ASSIGNMENT_OK));
    TEST_ASSERT_TRUE(assignmentRecordIsActionable(ASSIGNMENT_UNASSIGNED));
    TEST_ASSERT_FALSE(assignmentRecordIsActionable(ASSIGNMENT_MISSING));
    TEST_ASSERT_FALSE(assignmentRecordIsActionable(ASSIGNMENT_MALFORMED));
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

void test_lookupCubeNumberByTag_replacement_sticker() {
    // Cube 4's location-B sticker was replaced in the field. A cube whose tag
    // is unknown still reads (/nfc carries the raw id) but never resolves to a
    // cube number, so /right stays empty and the neighbour is invisible to the
    // game -- which is silent unless you are watching MQTT.
    TEST_ASSERT_EQUAL(4, lookupCubeNumberByTag("CE41D303530104E0"));
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
    // 12 primary + 12 backup + 1 field replacement = 25 total
    TEST_ASSERT_EQUAL(25, NUM_KNOWN_TAGS);
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

void test_makeMqttClientId_full_and_keepalive() {
    char buf[40];
    makeMqttClientIdC("CC:DB:A7:9F:C2:84", "", buf, sizeof(buf));
    TEST_ASSERT_EQUAL_STRING("cube-CCDBA79FC284", buf);

    makeMqttClientIdC("CC:DB:A7:9F:C2:84", "-ka", buf, sizeof(buf));
    TEST_ASSERT_EQUAL_STRING("cube-CCDBA79FC284-ka", buf);

    char other[40];
    makeMqttClientIdC("80:F3:DA:54:53:B8", "", other, sizeof(other));
    TEST_ASSERT_EQUAL_STRING("cube-80F3DA5453B8", other);
}

void test_resolveWakeAction_network_failure_stays_asleep() {
    TEST_ASSERT_EQUAL(WAKE_ACTION_STAY_ASLEEP,
                      resolveWakeAction(false, false, false, true, true));
    TEST_ASSERT_EQUAL(WAKE_ACTION_STAY_ASLEEP,
                      resolveWakeAction(true, false, false, true, true));
}

void test_resolveWakeAction_assigned_cube_obeys_slot_flag() {
    TEST_ASSERT_EQUAL(WAKE_ACTION_STAY_ASLEEP,
                      resolveWakeAction(true, true, true, false, true));
    TEST_ASSERT_EQUAL(WAKE_ACTION_WAKE_FULL,
                      resolveWakeAction(true, true, true, true, false));
}

void test_resolveWakeAction_unassigned_cube_obeys_device_flag() {
    TEST_ASSERT_EQUAL(WAKE_ACTION_STAY_ASLEEP,
                      resolveWakeAction(true, true, false, true, false));
    TEST_ASSERT_EQUAL(WAKE_ACTION_WAKE_FULL,
                      resolveWakeAction(true, true, false, false, true));
}

struct FakeWakeCheckInPorts : public WakeCheckInPorts {
  bool wifi_result = true;
  bool mqtt_result = true;
  bool slot_topic_result = false;
  SleepFlags flags = {false, false};

  char calls[128] = "";
  bool called_after_sleep = false;

  void record(const char* name) {
    if (strstr(calls, "enterSleep,") != NULL) called_after_sleep = true;
    strncat(calls, name, sizeof(calls) - strlen(calls) - 1);
    strncat(calls, ",", sizeof(calls) - strlen(calls) - 1);
  }
  bool sawCall(const char* name) {
    char needle[32];
    snprintf(needle, sizeof(needle), "%s,", name);
    return strstr(calls, needle) != NULL;
  }

  bool awaitWifi() override { record("awaitWifi"); return wifi_result; }
  bool connectMqtt() override { record("connectMqtt"); return mqtt_result; }
  bool hasSlotTopic() override { record("hasSlotTopic"); return slot_topic_result; }
  bool flags_confirmed = true;
  bool readSleepFlags(SleepFlags* out) override {
    record("readSleepFlags");
    *out = flags;
    return flags_confirmed;
  }
  void clearSleepFlags() override { record("clearSleepFlags"); }
  void enterSleep() override { record("enterSleep"); }
  void stayAwake() override { record("stayAwake"); }
};

void test_runWakeCheckIn_wifi_timeout() {
    FakeWakeCheckInPorts ports;
    ports.wifi_result = false;
    runWakeCheckIn(WAKE_REASON_TIMER, ports);
    TEST_ASSERT_TRUE(ports.sawCall("enterSleep"));
    TEST_ASSERT_FALSE(ports.sawCall("stayAwake"));
    TEST_ASSERT_FALSE(ports.sawCall("connectMqtt"));
    TEST_ASSERT_FALSE(ports.sawCall("hasSlotTopic"));
    TEST_ASSERT_FALSE(ports.sawCall("readSleepFlags"));
    TEST_ASSERT_FALSE(ports.called_after_sleep);
}

void test_runWakeCheckIn_mqtt_connect_fails() {
    FakeWakeCheckInPorts ports;
    ports.mqtt_result = false;
    runWakeCheckIn(WAKE_REASON_TIMER, ports);
    TEST_ASSERT_TRUE(ports.sawCall("enterSleep"));
    TEST_ASSERT_FALSE(ports.sawCall("stayAwake"));
    TEST_ASSERT_FALSE(ports.sawCall("hasSlotTopic"));
    TEST_ASSERT_FALSE(ports.sawCall("readSleepFlags"));
    TEST_ASSERT_FALSE(ports.sawCall("clearSleepFlags"));
    TEST_ASSERT_FALSE(ports.called_after_sleep);
}

void test_runWakeCheckIn_flag_set_sleeps_without_clearing() {
    FakeWakeCheckInPorts ports;
    ports.flags.device_requests_sleep = true;
    runWakeCheckIn(WAKE_REASON_TIMER, ports);
    TEST_ASSERT_TRUE(ports.sawCall("enterSleep"));
    TEST_ASSERT_FALSE(ports.sawCall("clearSleepFlags"));
    TEST_ASSERT_FALSE(ports.sawCall("stayAwake"));
    TEST_ASSERT_FALSE(ports.called_after_sleep);
}

void test_runWakeCheckIn_flag_clear_clears_then_stays_awake() {
    FakeWakeCheckInPorts ports;
    runWakeCheckIn(WAKE_REASON_TIMER, ports);
    TEST_ASSERT_EQUAL_STRING(
        "awaitWifi,connectMqtt,hasSlotTopic,readSleepFlags,clearSleepFlags,stayAwake,",
        ports.calls);
    TEST_ASSERT_FALSE(ports.sawCall("enterSleep"));
}

void test_runWakeCheckIn_assigned_cube_wakes_on_stale_device_flag() {
    FakeWakeCheckInPorts ports;
    ports.slot_topic_result = true;
    ports.flags.slot_requests_sleep = false;
    ports.flags.device_requests_sleep = true;
    runWakeCheckIn(WAKE_REASON_TIMER, ports);
    TEST_ASSERT_TRUE(ports.sawCall("clearSleepFlags"));
    TEST_ASSERT_TRUE(ports.sawCall("stayAwake"));
    TEST_ASSERT_FALSE(ports.sawCall("enterSleep"));
}

void test_runWakeCheckIn_button_wake_ignores_network() {
    FakeWakeCheckInPorts ports;
    ports.wifi_result = false;
    runWakeCheckIn(WAKE_REASON_BUTTON, ports);
    TEST_ASSERT_EQUAL_STRING("stayAwake,", ports.calls);
}

// A reset -- brownout, watchdog, crash, a jostled battery contact -- is not
// someone deciding to use the cube. Until now any of them came up fully awake
// and ignored the sleep flag entirely, so a stored cube woke for 10 minutes
// every time it glitched, and a weak battery made the next glitch likelier.
// The window for the retained flag is finite, so a slow broker or weak RF can
// end it with nothing received -- which looks identical to no flag being set,
// because an empty retained topic delivers nothing. Reading that silence as
// "no flag" made the cube clear its own flag and stay awake for ten minutes,
// and the weak battery causing the slow link is what pays for it.
void test_runWakeCheckIn_unconfirmed_flag_read_does_not_clear_or_wake() {
    FakeWakeCheckInPorts ports;
    ports.slot_topic_result = true;
    ports.flags_confirmed = false;
    runWakeCheckIn(WAKE_REASON_TIMER, ports);
    TEST_ASSERT_TRUE(ports.sawCall("enterSleep"));
    TEST_ASSERT_FALSE(ports.sawCall("clearSleepFlags"));
    TEST_ASSERT_FALSE(ports.sawCall("stayAwake"));
}

// A reset cannot fall back to sleep on an unreadable flag for the same reason
// it cannot on an unreachable broker: it would make a working cube look dead.
void test_runWakeCheckIn_reset_with_unconfirmed_flag_read_stays_awake() {
    FakeWakeCheckInPorts ports;
    ports.slot_topic_result = true;
    ports.flags_confirmed = false;
    runWakeCheckIn(WAKE_REASON_OTHER, ports);
    TEST_ASSERT_TRUE(ports.sawCall("stayAwake"));
    TEST_ASSERT_FALSE(ports.sawCall("enterSleep"));
    TEST_ASSERT_FALSE(ports.sawCall("clearSleepFlags"));
}

void test_runWakeCheckIn_reset_obeys_a_set_sleep_flag() {
    FakeWakeCheckInPorts ports;
    ports.slot_topic_result = true;
    ports.flags = {false, true};
    runWakeCheckIn(WAKE_REASON_OTHER, ports);
    TEST_ASSERT_TRUE(ports.sawCall("enterSleep"));
    TEST_ASSERT_FALSE(ports.sawCall("stayAwake"));
}

// Asymmetry with the timer path, and deliberate: a timer wake was already
// asleep, so silence just leaves it there. A reset has no such prior. Sleeping
// because the broker happened to be unreachable would turn every cube into a
// dead one for anyone standing at the cabinet during an outage.
void test_runWakeCheckIn_reset_without_network_stays_awake() {
    FakeWakeCheckInPorts ports;
    ports.wifi_result = false;
    runWakeCheckIn(WAKE_REASON_OTHER, ports);
    TEST_ASSERT_TRUE(ports.sawCall("stayAwake"));
    TEST_ASSERT_FALSE(ports.sawCall("enterSleep"));
}

void test_runWakeCheckIn_reset_with_no_flag_set_stays_awake() {
    FakeWakeCheckInPorts ports;
    ports.slot_topic_result = true;
    ports.flags = {false, false};
    runWakeCheckIn(WAKE_REASON_OTHER, ports);
    TEST_ASSERT_TRUE(ports.sawCall("stayAwake"));
    TEST_ASSERT_FALSE(ports.sawCall("enterSleep"));
}

// An unassigned cube has no slot topic and falls back to the device flag, the
// same rule the timer path uses.
void test_runWakeCheckIn_reset_unassigned_cube_obeys_device_flag() {
    FakeWakeCheckInPorts ports;
    ports.slot_topic_result = false;
    ports.flags = {true, false};
    runWakeCheckIn(WAKE_REASON_OTHER, ports);
    TEST_ASSERT_TRUE(ports.sawCall("enterSleep"));
    TEST_ASSERT_FALSE(ports.sawCall("stayAwake"));
}

void test_decideNfcObservation_publishes_a_new_tag() {
    TEST_ASSERT_EQUAL(NFC_OBS_TAG,
        decideNfcObservation(true, false, true, false, "AABB", "-"));
}

void test_decideNfcObservation_suppresses_an_unchanged_tag() {
    TEST_ASSERT_EQUAL(NFC_OBS_NONE,
        decideNfcObservation(true, false, true, false, "AABB", "AABB"));
}

void test_decideNfcObservation_respects_the_hall_gate() {
    TEST_ASSERT_EQUAL(NFC_OBS_NONE,
        decideNfcObservation(true, false, false, false, "AABB", "-"));
}

void test_decideNfcObservation_reports_absence_when_both_sensors_agree() {
    TEST_ASSERT_EQUAL(NFC_OBS_ABSENT,
        decideNfcObservation(false, true, true, false, "", "AABB"));
}

void test_decideNfcObservation_keeps_the_neighbor_when_hall_still_sees_it() {
    // A hall-present guard on an NFC flake: this is what stops a dropped read
    // from breaking a word in play.
    TEST_ASSERT_EQUAL(NFC_OBS_NONE,
        decideNfcObservation(false, true, true, true, "", "AABB"));
}

void test_decideNfcObservation_suppresses_repeated_absence() {
    TEST_ASSERT_EQUAL(NFC_OBS_NONE,
        decideNfcObservation(false, true, true, false, "", "-"));
}

void test_decideNfcObservation_ignores_a_failed_read() {
    TEST_ASSERT_EQUAL(NFC_OBS_NONE,
        decideNfcObservation(false, false, true, false, "", "AABB"));
}

void test_buildObservationPayload_carries_protocol_boot_id_and_tag() {
    char buf[160];
    buildObservationPayload("A3F9", "0A40D303530104E0", buf, sizeof(buf));
    TEST_ASSERT_EQUAL_STRING(
        "{\"protocol\":1,\"boot_id\":\"A3F9\",\"tag\":\"0A40D303530104E0\"}", buf);
}

void test_buildObservationPayload_encodes_no_neighbor() {
    char buf[160];
    buildObservationPayload("A3F9", "-", buf, sizeof(buf));
    TEST_ASSERT_EQUAL_STRING("{\"protocol\":1,\"boot_id\":\"A3F9\",\"tag\":\"-\"}", buf);
}

void test_buildObservationPayload_has_no_provenance_fields() {
    // The spec is explicit: no generation, no sequence. Validating provenance
    // was tried and removed -- it kills neighbor detection on bench-flashed
    // cubes -- so the field must not exist to be tempting.
    char buf[160];
    buildObservationPayload("A3F9", "AABB", buf, sizeof(buf));
    TEST_ASSERT_NULL(strstr(buf, "generation"));
    TEST_ASSERT_NULL(strstr(buf, "sequence"));
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
    RUN_TEST(test_findCubeIpOctet_primaries);
    RUN_TEST(test_findCubeIpOctet_backup_is_unique);
    RUN_TEST(test_findCubeIpOctet_unknown);
    RUN_TEST(test_parseAssignmentRecord_assigned);
    RUN_TEST(test_parseAssignmentRecord_unassigned);
    RUN_TEST(test_parseAssignmentRecord_missing);
    RUN_TEST(test_parseAssignmentRecord_malformed);
    RUN_TEST(test_resolveAssignedSlot);
    RUN_TEST(test_assignmentRecordIsActionable);

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
    RUN_TEST(test_lookupCubeNumberByTag_replacement_sticker);
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
    RUN_TEST(test_makeMqttClientId_full_and_keepalive);

    // Wake decision tests
    RUN_TEST(test_resolveWakeAction_network_failure_stays_asleep);
    RUN_TEST(test_resolveWakeAction_assigned_cube_obeys_slot_flag);
    RUN_TEST(test_resolveWakeAction_unassigned_cube_obeys_device_flag);
    RUN_TEST(test_runWakeCheckIn_wifi_timeout);
    RUN_TEST(test_runWakeCheckIn_mqtt_connect_fails);
    RUN_TEST(test_runWakeCheckIn_flag_set_sleeps_without_clearing);
    RUN_TEST(test_runWakeCheckIn_flag_clear_clears_then_stays_awake);
    RUN_TEST(test_runWakeCheckIn_assigned_cube_wakes_on_stale_device_flag);
    RUN_TEST(test_runWakeCheckIn_button_wake_ignores_network);
    RUN_TEST(test_runWakeCheckIn_unconfirmed_flag_read_does_not_clear_or_wake);
    RUN_TEST(test_runWakeCheckIn_reset_with_unconfirmed_flag_read_stays_awake);
    RUN_TEST(test_runWakeCheckIn_reset_obeys_a_set_sleep_flag);
    RUN_TEST(test_runWakeCheckIn_reset_without_network_stays_awake);
    RUN_TEST(test_runWakeCheckIn_reset_with_no_flag_set_stays_awake);
    RUN_TEST(test_runWakeCheckIn_reset_unassigned_cube_obeys_device_flag);

    // Neighbor observation protocol tests
    RUN_TEST(test_decideNfcObservation_publishes_a_new_tag);
    RUN_TEST(test_decideNfcObservation_suppresses_an_unchanged_tag);
    RUN_TEST(test_decideNfcObservation_respects_the_hall_gate);
    RUN_TEST(test_decideNfcObservation_reports_absence_when_both_sensors_agree);
    RUN_TEST(test_decideNfcObservation_keeps_the_neighbor_when_hall_still_sees_it);
    RUN_TEST(test_decideNfcObservation_suppresses_repeated_absence);
    RUN_TEST(test_decideNfcObservation_ignores_a_failed_read);
    RUN_TEST(test_buildObservationPayload_carries_protocol_boot_id_and_tag);
    RUN_TEST(test_buildObservationPayload_encodes_no_neighbor);
    RUN_TEST(test_buildObservationPayload_has_no_provenance_fields);

    return UNITY_END();
}
