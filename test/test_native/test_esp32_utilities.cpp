#include <unity.h>
#include "../../src/cube_utilities.h"
#include "../../src/hall_presence.h"

// For testing, include the implementation directly
#include "../../src/cube_utilities.cpp"


// ---------------------------------------------------------------------------
// Hall presence tracker
// ---------------------------------------------------------------------------
// The presence signal is tiny -- roughly 120 ADC counts at the installed gap -- and it
// rides on a baseline that drifts with the +3V3 rail, because the DRV5055 output is
// ratiometric to the rail while the ESP32 ADC references its own bandgap. A 100mV rail
// move is worth about 62 counts, over half the signal, which is why the old fixed
// thresholds could not work and why these tests care so much about drift.

static HallPresenceConfig test_presence_config() {
    // direction, on_delta, off_delta, fast_shift, base_shift, base_interval_ms
    return HallPresenceConfig{1, 60, 30, 3, 7, 250};
}

// Feed a steady value for long enough that both filters settle. id_mask is what
// the ID sensors read while it is fed, which the tracker uses to decide whether
// a neighbour is there.
static void settle(HallPresenceTracker& t, int raw, uint32_t& now, int steps = 4000,
                   uint8_t id_mask = 0) {
    for (int i = 0; i < steps; i++) t.update(raw, now += 1, id_mask);
}

void test_presence_starts_inactive_and_adopts_the_settled_reading() {
    HallPresenceTracker t; t.begin(test_presence_config());
    uint32_t now = 0;
    // One sample is not a reference. A cold boot reaches the first update before the
    // sensor output has settled, and slot 16 primed 148 counts low that way.
    TEST_ASSERT_FALSE(t.update(2035, now, 0));
    TEST_ASSERT_EQUAL(0, t.baseline());

    settle(t, 2035, now, 600);
    // Primed from the reading rather than from an assumed 2048 midpoint, so a cube
    // whose rail sits off-nominal does not boot already half-way to threshold.
    TEST_ASSERT_EQUAL(2035, t.baseline());
    TEST_ASSERT_EQUAL(0, t.delta());
}

void test_presence_asserts_above_on_delta_and_holds_through_hysteresis() {
    HallPresenceTracker t; t.begin(test_presence_config());
    uint32_t now = 0;
    settle(t, 2035, now);

    // Just under the threshold does nothing.
    settle(t, 2035 + 50, now, 200);
    TEST_ASSERT_FALSE(t.active());

    settle(t, 2035 + 120, now, 200);
    TEST_ASSERT_TRUE(t.active());

    // Falls back between off_delta and on_delta: still present. Without this a neighbour
    // sitting near the trip point chatters the published id.
    settle(t, 2035 + 45, now, 200);
    TEST_ASSERT_TRUE(t.active());

    settle(t, 2035 + 10, now, 200);
    TEST_ASSERT_FALSE(t.active());
}

void test_presence_ignores_the_wrong_direction() {
    HallPresenceTracker t; t.begin(test_presence_config());
    uint32_t now = 0;
    settle(t, 2035, now);
    // The DRV5055 is bipolar and the ID magnets are 32mm away; a deflection the other
    // way is their crosstalk, not the presence magnet, so it must not assert.
    settle(t, 2035 - 400, now, 200);
    TEST_ASSERT_FALSE(t.active());
}

void test_presence_tracks_slow_rail_drift_without_asserting() {
    HallPresenceTracker t; t.begin(test_presence_config());
    uint32_t now = 0;
    settle(t, 2035, now);
    // 100mV of rail sag, ramped in over 30s. Bigger than on_delta, so a fixed-threshold
    // implementation would report a neighbour that is not there.
    for (int i = 0; i <= 62; i++) settle(t, 2035 + i, now, 500);
    TEST_ASSERT_FALSE(t.active());
    // The baseline follows but lags -- it is a filter, not a tracker. Assert that it
    // moved most of the way rather than pinning an exact value, which would just be
    // restating the filter constants.
    TEST_ASSERT_TRUE(t.baseline() > 2035 + 20);
    TEST_ASSERT_TRUE(t.baseline() <= 2035 + 62);
}

void test_presence_is_fooled_by_a_fast_rail_step() {
    HallPresenceTracker t; t.begin(test_presence_config());
    uint32_t now = 0;
    settle(t, 2035, now);
    // KNOWN LIMITATION, asserted so it cannot regress silently. The baseline has a ~32s
    // time constant, so it rejects drift only up to roughly on_delta/tau -- a couple of
    // counts per second. A step, such as the HUB75 rail moving when the display is
    // gated, outruns it and reads as a neighbour arriving.
    settle(t, 2035 + 120, now, 200);
    TEST_ASSERT_TRUE(t.active());
}

void test_presence_freezes_the_baseline_while_a_neighbour_is_present() {
    HallPresenceTracker t; t.begin(test_presence_config());
    uint32_t now = 0;
    settle(t, 2035, now);
    settle(t, 2035 + 120, now, 200);
    TEST_ASSERT_TRUE(t.active());
    const int base_at_assert = t.baseline();

    // Hold the magnet for two minutes. An adapting baseline would climb to meet it and
    // silently drop the neighbour.
    for (int i = 0; i < 120; i++) settle(t, 2035 + 120, now, 1000);
    TEST_ASSERT_TRUE(t.active());
    TEST_ASSERT_EQUAL(base_at_assert, t.baseline());
}

void test_presence_boots_blind_to_a_magnet_it_woke_up_next_to() {
    HallPresenceTracker t; t.begin(test_presence_config());
    uint32_t now = 0;
    // Waking already docked primes the baseline from a magnet-present sample, so the
    // magnet is subtracted out and never seen. Measured on slot 1 after an OTA flash:
    // the neighbour id collapsed while hall_debug still read the right ID pattern.
    settle(t, 2035 + 120, now);
    TEST_ASSERT_FALSE(t.active());
}

void test_presence_restores_a_saved_baseline_instead_of_priming_from_a_magnet() {
    HallPresenceTracker t; t.begin(test_presence_config(), 2035);
    uint32_t now = 0;
    settle(t, 2035 + 120, now, 200);
    TEST_ASSERT_TRUE(t.active());
}

void test_presence_ignores_an_absent_saved_baseline() {
    // 0 means nothing was saved -- a cold boot re-initialises RTC memory -- so the
    // reading must still prime the baseline once it has settled.
    HallPresenceTracker t; t.begin(test_presence_config(), 0);
    uint32_t now = 0;
    TEST_ASSERT_FALSE(t.update(2035, now, 0));
    settle(t, 2035, now, 600);
    TEST_ASSERT_EQUAL(2035, t.baseline());
}

// Seeding a cold boot from NVS is only as good as what gets written, and the value
// worth writing is a baseline taken with nothing magnetic nearby. These pin the
// guard that decides that, because a poisoned seed is permanent where a poisoned
// RTC value lasts one wake.
void test_baseline_is_worth_saving_only_with_no_magnet_in_sight() {
    // id_mask 0 is the physical proof of an undocked cube: no ID magnet is in range.
    TEST_ASSERT_TRUE(shouldSavePresenceBaseline(0, false, 1771, 1900));
    // Any ID magnet means a neighbour is docked, so its presence magnet is skewing
    // the reading even if the tracker has not asserted yet.
    TEST_ASSERT_FALSE(shouldSavePresenceBaseline(0b000101, false, 1771, 1900));
    TEST_ASSERT_FALSE(shouldSavePresenceBaseline(0b000001, false, 1771, 1900));
}

void test_baseline_is_not_saved_while_presence_is_asserted() {
    // The baseline is frozen while active, so it describes the moment the neighbour
    // arrived rather than the current rail.
    TEST_ASSERT_FALSE(shouldSavePresenceBaseline(0, true, 1771, 1900));
}

void test_baseline_saves_only_on_a_move_worth_a_flash_write() {
    // NVS erases a sector per write. The baseline wanders by a count or two a second,
    // so writing every change would burn the part out.
    TEST_ASSERT_FALSE(shouldSavePresenceBaseline(0, false, 1771, 1771));
    TEST_ASSERT_FALSE(shouldSavePresenceBaseline(0, false, 1780, 1771));
    TEST_ASSERT_TRUE(shouldSavePresenceBaseline(0, false, 1790, 1771));
    TEST_ASSERT_TRUE(shouldSavePresenceBaseline(0, false, 1750, 1771));
}

void test_baseline_saves_when_nothing_is_stored_yet() {
    // 0 is the unset marker, and is further from any real baseline than the
    // threshold, so a first write needs no special case.
    TEST_ASSERT_TRUE(shouldSavePresenceBaseline(0, false, 1771, 0));
}

// Distance and closeness exist so a consumer never has to know the cube law. These
// use the shipped on_delta of 95 and the readings measured on slot 1.
void test_distance_falls_as_the_cube_root_of_field() {
    // Reference point: at on_delta the cube is exactly at the latch distance.
    TEST_ASSERT_EQUAL(100, hallPresenceDistance(95, 95));
    // Eight times the field is half the distance, which is the whole point of the
    // conversion -- delta alone would read as an eightfold change.
    TEST_ASSERT_EQUAL(50, hallPresenceDistance(95 * 8, 95));
    TEST_ASSERT_EQUAL(200, hallPresenceDistance(95, 95 * 8));
}

void test_distance_reports_out_of_range_behind_the_baseline() {
    // A negative delta is the rail below its baseline, not a very distant magnet.
    TEST_ASSERT_EQUAL(999, hallPresenceDistance(0, 95));
    TEST_ASSERT_EQUAL(999, hallPresenceDistance(-30, 95));
}

void test_closeness_spans_nothing_to_docked() {
    // Nothing in range, including the out-of-range sentinel, must read as 0 rather
    // than leak 999 into an animation.
    TEST_ASSERT_EQUAL(0, hallPresenceCloseness(0, 95));
    TEST_ASSERT_EQUAL(0, hallPresenceCloseness(-30, 95));
    // 35 counts is the measured ADC noise floor: the first delta worth believing.
    TEST_ASSERT_EQUAL(0, hallPresenceCloseness(35, 95));
    // 127 is the docked reading measured on slot 1.
    TEST_ASSERT_EQUAL(100, hallPresenceCloseness(127, 95));
    // Closer still stays pinned rather than running over 100.
    TEST_ASSERT_EQUAL(100, hallPresenceCloseness(400, 95));
}

void test_closeness_rises_smoothly_between_the_endpoints() {
    // An animation needs every step of the approach, not just a jump at the trip
    // point, so assert it is strictly increasing across the usable span.
    int previous = -1;
    for (int delta = 36; delta <= 127; delta++) {
        const int closeness = hallPresenceCloseness(delta, 95);
        TEST_ASSERT_TRUE(closeness >= previous);
        TEST_ASSERT_TRUE(closeness >= 0 && closeness <= 100);
        previous = closeness;
    }
    // Latching happens partway up, not at the top: most of the travel is before it.
    const int at_latch = hallPresenceCloseness(95, 95);
    TEST_ASSERT_TRUE(at_latch > 50 && at_latch < 100);
}

// A neighbour close enough to trip the ID sensors but not the presence threshold
// left the baseline free to adapt, so it walked up to the magnet and the reading
// decayed to nothing over a couple of minutes -- observed on cube c. The ID
// sensors are digital and independent of this reading, so they are what says a
// neighbour is there while the analog signal is still below the trip point.
void test_baseline_holds_while_the_id_sensors_see_a_neighbour() {
    HallPresenceTracker t; t.begin(test_presence_config());
    uint32_t now = 0;
    settle(t, 2035, now);
    const int idle = t.baseline();

    // Below on_delta of 60, so presence never latches; P2+P5 say a cube is there.
    settle(t, 2035 + 40, now, 40000, 0b010010);

    TEST_ASSERT_EQUAL(idle, t.baseline());
    TEST_ASSERT_TRUE(t.delta() > 30);
}

// The freeze must not become permanent: with no neighbour the baseline still has
// to follow the rail, which is what keeps a slow drift from reading as approach.
void test_baseline_still_adapts_with_no_neighbour() {
    HallPresenceTracker t; t.begin(test_presence_config());
    uint32_t now = 0;
    settle(t, 2035, now);
    settle(t, 2035 + 40, now, 40000, 0);
    TEST_ASSERT_TRUE(t.baseline() > 2035 + 20);
    TEST_ASSERT_TRUE(t.delta() < 20);
}

// Priming from a sample taken with a neighbour present subtracts the magnet into
// the baseline and the cube never sees it. Cubes are powered up in whatever
// arrangement they were left in, so this is a coin flip on every cold boot.
void test_priming_waits_for_a_sample_with_no_neighbour() {
    HallPresenceTracker t; t.begin(test_presence_config());
    uint32_t now = 0;
    settle(t, 2035 + 200, now, 500, 0b010010);
    // Nothing can be reported without a reference, so it stays quiet rather than
    // guessing.
    TEST_ASSERT_FALSE(t.active());
    TEST_ASSERT_EQUAL(0, t.delta());

    settle(t, 2035, now, 4000, 0);
    const int idle = t.baseline();
    TEST_ASSERT_TRUE(idle > 2000 && idle < 2070);

    // And the neighbour it could not see at boot is visible once there is one.
    settle(t, 2035 + 200, now, 500, 0b010010);
    TEST_ASSERT_TRUE(t.active());
}

// A saved baseline is the other half: it lets a cube powered up already docked
// see the neighbour on the first sample, with no clean reading to prime from.
void test_a_saved_baseline_sees_a_neighbour_present_at_boot() {
    HallPresenceTracker t; t.begin(test_presence_config(), 2035);
    uint32_t now = 0;
    settle(t, 2035 + 200, now, 500, 0b010010);
    TEST_ASSERT_TRUE(t.active());
}

// An unprimed tracker and a cube with no neighbour both report inactive with zero
// delta, so from outside they are the same reading -- but one is a fault and the
// other is not. primed() is what the diagnostics use to tell them apart.
void test_primed_reports_whether_a_reference_exists() {
    HallPresenceTracker t; t.begin(test_presence_config());
    TEST_ASSERT_FALSE(t.primed());
    uint32_t now = 0;
    // A neighbour there from the first sample blocks priming entirely.
    settle(t, 2035 + 200, now, 500, 0b010010);
    TEST_ASSERT_FALSE(t.primed());
    settle(t, 2035, now, 600, 0);
    TEST_ASSERT_TRUE(t.primed());
}

// A saved baseline is a reference, so a cube restored from one is primed before
// it has sampled anything.
void test_a_saved_baseline_counts_as_primed() {
    HallPresenceTracker t; t.begin(test_presence_config(), 2035);
    TEST_ASSERT_TRUE(t.primed());
}

// Slot 16 cold-booted, sampled 1702 while the sensor output was still rising to
// its ~1850 idle, primed there, latched on the 148-count difference, and had
// persisted the bad value before the activation closed the save gate. The
// reference has to come from the settled reading, not the first one.
void test_priming_ignores_an_unsettled_first_sample() {
    HallPresenceTracker t; t.begin(test_presence_config());
    uint32_t now = 0;
    t.update(1702, now += 1, 0);
    settle(t, 1850, now, 600);
    TEST_ASSERT_TRUE(t.primed());
    TEST_ASSERT_TRUE(t.baseline() > 1800);
    TEST_ASSERT_FALSE(t.active());
}

// What the recalibrate command relies on: it clears the stored reference and calls
// begin() again, so a tracker latched on a bad baseline must come back clean
// rather than carry the activation across.
void test_begin_clears_a_latched_bad_baseline() {
    HallPresenceTracker t; t.begin(test_presence_config(), 1702);
    uint32_t now = 0;
    settle(t, 1850, now, 200, 0);
    TEST_ASSERT_TRUE(t.active());

    t.begin(test_presence_config(), 0);
    TEST_ASSERT_FALSE(t.primed());
    settle(t, 1850, now, 600, 0);
    TEST_ASSERT_TRUE(t.primed());
    TEST_ASSERT_FALSE(t.active());
    TEST_ASSERT_TRUE(t.baseline() > 1800);
}

void test_presence_delta_is_monotonic_with_approach() {
    HallPresenceTracker t; t.begin(test_presence_config());
    uint32_t now = 0;
    settle(t, 2035, now);
    // delta() is what a distance animation would read, so it has to rise smoothly rather
    // than only be meaningful at the trip point.
    settle(t, 2035 + 30, now, 200);  int near_far = t.delta();
    settle(t, 2035 + 90, now, 200);  int near_mid = t.delta();
    settle(t, 2035 + 200, now, 200); int near_close = t.delta();
    TEST_ASSERT_TRUE(near_far < near_mid);
    TEST_ASSERT_TRUE(near_mid < near_close);
}

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

    // An uncommissioned spare must not adopt a real cube's slot when no
    // assignment record arrives. Blank NVS means authority_latched is false,
    // which is exactly the branch that hands back the compiled id, so the
    // sentinel is the only thing standing between a freshly flashed board and
    // a slot collision on the live fleet.
    TEST_ASSERT_TRUE(
        resolveAssignedSlot(ASSIGNMENT_MISSING, -1, false, CUBE_ID_NONE) <= 0);
    TEST_ASSERT_TRUE(
        resolveAssignedSlot(ASSIGNMENT_MALFORMED, -1, false, CUBE_ID_NONE) <= 0);
}

void test_assignmentRecordIsActionable() {
    TEST_ASSERT_TRUE(assignmentRecordIsActionable(ASSIGNMENT_OK));
    TEST_ASSERT_TRUE(assignmentRecordIsActionable(ASSIGNMENT_UNASSIGNED));
    TEST_ASSERT_FALSE(assignmentRecordIsActionable(ASSIGNMENT_MISSING));
    TEST_ASSERT_FALSE(assignmentRecordIsActionable(ASSIGNMENT_MALFORMED));
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
                      resolveWakeAction(false, false, true));
    TEST_ASSERT_EQUAL(WAKE_ACTION_STAY_ASLEEP,
                      resolveWakeAction(true, false, true));
}

// Every cube obeys the same flag, whether or not it holds a slot: the flag is
// keyed by MAC, which a cube has before anyone assigns it anything.
void test_resolveWakeAction_obeys_the_device_flag() {
    TEST_ASSERT_EQUAL(WAKE_ACTION_STAY_ASLEEP,
                      resolveWakeAction(true, true, true));
    TEST_ASSERT_EQUAL(WAKE_ACTION_WAKE_FULL,
                      resolveWakeAction(true, true, false));
}

struct FakeWakeCheckInPorts : public WakeCheckInPorts {
  bool wifi_result = true;
  bool mqtt_result = true;
  bool sleep_requested = false;

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
  bool flag_confirmed = true;
  bool readSleepFlag(bool* out) override {
    record("readSleepFlag");
    *out = sleep_requested;
    return flag_confirmed;
  }
  void clearSleepFlag() override { record("clearSleepFlag"); }
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
    TEST_ASSERT_FALSE(ports.sawCall("readSleepFlag"));
    TEST_ASSERT_FALSE(ports.sawCall("readSleepFlag"));
    TEST_ASSERT_FALSE(ports.called_after_sleep);
}

void test_runWakeCheckIn_mqtt_connect_fails() {
    FakeWakeCheckInPorts ports;
    ports.mqtt_result = false;
    runWakeCheckIn(WAKE_REASON_TIMER, ports);
    TEST_ASSERT_TRUE(ports.sawCall("enterSleep"));
    TEST_ASSERT_FALSE(ports.sawCall("stayAwake"));
    TEST_ASSERT_FALSE(ports.sawCall("readSleepFlag"));
    TEST_ASSERT_FALSE(ports.sawCall("readSleepFlag"));
    TEST_ASSERT_FALSE(ports.sawCall("clearSleepFlag"));
    TEST_ASSERT_FALSE(ports.called_after_sleep);
}

void test_runWakeCheckIn_flag_set_sleeps_without_clearing() {
    FakeWakeCheckInPorts ports;
    ports.sleep_requested = true;
    runWakeCheckIn(WAKE_REASON_TIMER, ports);
    TEST_ASSERT_TRUE(ports.sawCall("enterSleep"));
    TEST_ASSERT_FALSE(ports.sawCall("clearSleepFlag"));
    TEST_ASSERT_FALSE(ports.sawCall("stayAwake"));
    TEST_ASSERT_FALSE(ports.called_after_sleep);
}

void test_runWakeCheckIn_flag_clear_clears_then_stays_awake() {
    FakeWakeCheckInPorts ports;
    runWakeCheckIn(WAKE_REASON_TIMER, ports);
    TEST_ASSERT_EQUAL_STRING(
        "awaitWifi,connectMqtt,readSleepFlag,clearSleepFlag,stayAwake,",
        ports.calls);
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
    ports.flag_confirmed = false;
    runWakeCheckIn(WAKE_REASON_TIMER, ports);
    TEST_ASSERT_TRUE(ports.sawCall("enterSleep"));
    TEST_ASSERT_FALSE(ports.sawCall("clearSleepFlag"));
    TEST_ASSERT_FALSE(ports.sawCall("stayAwake"));
}

// A reset cannot fall back to sleep on an unreadable flag for the same reason
// it cannot on an unreachable broker: it would make a working cube look dead.
void test_runWakeCheckIn_reset_with_unconfirmed_flag_read_stays_awake() {
    FakeWakeCheckInPorts ports;
    ports.flag_confirmed = false;
    runWakeCheckIn(WAKE_REASON_OTHER, ports);
    TEST_ASSERT_TRUE(ports.sawCall("stayAwake"));
    TEST_ASSERT_FALSE(ports.sawCall("enterSleep"));
    TEST_ASSERT_FALSE(ports.sawCall("clearSleepFlag"));
}

void test_runWakeCheckIn_reset_obeys_a_set_sleep_flag() {
    FakeWakeCheckInPorts ports;
    ports.sleep_requested = true;
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
    ports.sleep_requested = false;
    runWakeCheckIn(WAKE_REASON_OTHER, ports);
    TEST_ASSERT_TRUE(ports.sawCall("stayAwake"));
    TEST_ASSERT_FALSE(ports.sawCall("enterSleep"));
}

void test_runWakeCheckIn_reset_obeys_the_device_flag() {
    FakeWakeCheckInPorts ports;
    ports.sleep_requested = true;
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

// ---------------------------------------------------------------------------
// Boot screen identity line
// ---------------------------------------------------------------------------
// A cube in the slot its octet was chosen for makes the two numbers look
// redundant. The spare is why they are not.

void test_boot_identity_pairs_slot_with_octet() {
    char buf[64];
    formatBootIdentity(buf, sizeof(buf), 12, 12, 32);
    TEST_ASSERT_EQUAL_STRING("c12 ip32", buf);
}

void test_boot_identity_prefers_the_stored_slot_over_the_compiled_one() {
    // The spare: octet 47 from its MAC, slot 1 from the admin page. Showing the
    // compiled CUBE_ID_NONE here would print "c?" for a cube that is playing as
    // cube 1, which is the confusion this line exists to end.
    char buf[64];
    formatBootIdentity(buf, sizeof(buf), 1, CUBE_ID_NONE, 47);
    TEST_ASSERT_EQUAL_STRING("c1 ip47", buf);
}

void test_boot_identity_falls_back_to_the_compiled_slot() {
    // No stored assignment yet: a cube sitting in its own slot still names
    // itself correctly from the compiled table.
    char buf[64];
    formatBootIdentity(buf, sizeof(buf), CUBE_ID_NONE, 12, 32);
    TEST_ASSERT_EQUAL_STRING("c12 ip32", buf);
}

void test_boot_identity_admits_an_unknown_slot() {
    // An unassigned spare knows its octet and nothing else. "c?" is honest;
    // any number here would be a guess, and applySlot() paints NO SLOT next.
    char buf[64];
    formatBootIdentity(buf, sizeof(buf), CUBE_ID_NONE, CUBE_ID_NONE, 47);
    TEST_ASSERT_EQUAL_STRING("c? ip47", buf);
}

void test_boot_identity_negative_stored_slot_is_not_treated_as_assigned() {
    // resolveAssignedSlot() returns -1 for a deliberately unassigned cube, and
    // that value reaches the stored record. It must not print as "c-1".
    char buf[64];
    formatBootIdentity(buf, sizeof(buf), -1, 16, 36);
    TEST_ASSERT_EQUAL_STRING("c16 ip36", buf);
}

void test_boot_identity_fits_the_debug_line() {
    // displayDebugMessage() wraps past 10 characters, which would push the
    // nfc/hall line down and reflow the screen this is meant to clarify.
    char buf[64];
    for (int slot = 1; slot <= 16; slot++) {
        for (int octet = 21; octet <= 48; octet++) {
            formatBootIdentity(buf, sizeof(buf), slot, slot, octet);
            TEST_ASSERT_TRUE_MESSAGE(strlen(buf) <= 10, buf);
        }
    }
    formatBootIdentity(buf, sizeof(buf), CUBE_ID_NONE, CUBE_ID_NONE, 48);
    TEST_ASSERT_TRUE_MESSAGE(strlen(buf) <= 10, buf);
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
        RUN_TEST(test_resolveWakeAction_obeys_the_device_flag);
    RUN_TEST(test_runWakeCheckIn_wifi_timeout);
    RUN_TEST(test_runWakeCheckIn_mqtt_connect_fails);
    RUN_TEST(test_runWakeCheckIn_flag_set_sleeps_without_clearing);
    RUN_TEST(test_runWakeCheckIn_flag_clear_clears_then_stays_awake);
        RUN_TEST(test_runWakeCheckIn_button_wake_ignores_network);
    RUN_TEST(test_runWakeCheckIn_unconfirmed_flag_read_does_not_clear_or_wake);
    RUN_TEST(test_runWakeCheckIn_reset_with_unconfirmed_flag_read_stays_awake);
    RUN_TEST(test_runWakeCheckIn_reset_obeys_a_set_sleep_flag);
    RUN_TEST(test_runWakeCheckIn_reset_without_network_stays_awake);
    RUN_TEST(test_runWakeCheckIn_reset_with_no_flag_set_stays_awake);
    RUN_TEST(test_runWakeCheckIn_reset_obeys_the_device_flag);

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

    // Hall presence tracker
    RUN_TEST(test_presence_starts_inactive_and_adopts_the_settled_reading);
    RUN_TEST(test_presence_asserts_above_on_delta_and_holds_through_hysteresis);
    RUN_TEST(test_presence_ignores_the_wrong_direction);
    RUN_TEST(test_presence_tracks_slow_rail_drift_without_asserting);
    RUN_TEST(test_presence_is_fooled_by_a_fast_rail_step);
    RUN_TEST(test_presence_freezes_the_baseline_while_a_neighbour_is_present);
    RUN_TEST(test_presence_boots_blind_to_a_magnet_it_woke_up_next_to);
    RUN_TEST(test_presence_restores_a_saved_baseline_instead_of_priming_from_a_magnet);
    RUN_TEST(test_presence_ignores_an_absent_saved_baseline);
    RUN_TEST(test_baseline_is_worth_saving_only_with_no_magnet_in_sight);
    RUN_TEST(test_baseline_is_not_saved_while_presence_is_asserted);
    RUN_TEST(test_baseline_saves_only_on_a_move_worth_a_flash_write);
    RUN_TEST(test_baseline_saves_when_nothing_is_stored_yet);
    RUN_TEST(test_distance_falls_as_the_cube_root_of_field);
    RUN_TEST(test_distance_reports_out_of_range_behind_the_baseline);
    RUN_TEST(test_closeness_spans_nothing_to_docked);
    RUN_TEST(test_closeness_rises_smoothly_between_the_endpoints);
    RUN_TEST(test_baseline_holds_while_the_id_sensors_see_a_neighbour);
    RUN_TEST(test_baseline_still_adapts_with_no_neighbour);
    RUN_TEST(test_priming_waits_for_a_sample_with_no_neighbour);
    RUN_TEST(test_a_saved_baseline_sees_a_neighbour_present_at_boot);
    RUN_TEST(test_primed_reports_whether_a_reference_exists);
    RUN_TEST(test_a_saved_baseline_counts_as_primed);
    RUN_TEST(test_priming_ignores_an_unsettled_first_sample);
    RUN_TEST(test_begin_clears_a_latched_bad_baseline);
    RUN_TEST(test_presence_delta_is_monotonic_with_approach);

    // Sensor-mode discriminator

    // Boot screen identity line
    RUN_TEST(test_boot_identity_pairs_slot_with_octet);
    RUN_TEST(test_boot_identity_prefers_the_stored_slot_over_the_compiled_one);
    RUN_TEST(test_boot_identity_falls_back_to_the_compiled_slot);
    RUN_TEST(test_boot_identity_admits_an_unknown_slot);
    RUN_TEST(test_boot_identity_negative_stored_slot_is_not_treated_as_assigned);
    RUN_TEST(test_boot_identity_fits_the_debug_line);

    return UNITY_END();
}
