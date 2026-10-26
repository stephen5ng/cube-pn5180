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

// A wrong-pole excursion never latches, so without a freeze the baseline adapts
// down through it -- and when the magnet leaves, the restored reading stands
// on_delta clear of the poisoned reference and latches with no neighbour there.
// Observed on cube 13 with a hand-waved magnet: baseline 1893 -> 1793, then
// stuck active at delta +99 because the latch froze the baseline it needed to
// recover with. Anything at latch strength is signal, not drift, either way.
void test_presence_freezes_the_baseline_through_a_wrong_pole_excursion() {
    HallPresenceTracker t; t.begin(test_presence_config());
    uint32_t now = 0;
    settle(t, 2035, now);
    const int idle = t.baseline();

    for (int i = 0; i < 60; i++) settle(t, 2035 - 400, now, 1000);
    TEST_ASSERT_FALSE(t.active());
    TEST_ASSERT_EQUAL(idle, t.baseline());

    // Magnet gone: the reading matches the reference instead of standing proud
    // of a poisoned one, so there is no phantom latch.
    settle(t, 2035, now, 200);
    TEST_ASSERT_FALSE(t.active());
    TEST_ASSERT_TRUE(t.delta() > -60 && t.delta() < 60);
}

// The freeze must only catch excursions, not ordinary wander: a drift-sized
// negative move still has to be absorbed, or the reference can never follow
// the rail down.
void test_presence_still_adapts_through_drift_sized_negative_wander() {
    HallPresenceTracker t; t.begin(test_presence_config());
    uint32_t now = 0;
    settle(t, 2035, now);
    settle(t, 2035 - 40, now, 40000, 0);
    TEST_ASSERT_FALSE(t.active());
    TEST_ASSERT_TRUE(t.baseline() < 2035 - 20);
    TEST_ASSERT_TRUE(t.delta() > -20);
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

// Cube 11 slid one column reads as cube 4 while presence is still latched. The
// change is only believed at docked strength; below it the gate reports no
// neighbour rather than the wrong one.
static constexpr int kDocked = 70;

void test_id_change_to_another_cube_needs_a_docked_reading() {
    TEST_ASSERT_EQUAL_UINT8(0, gateHallNeighborChange(4, 11, 45, kDocked));
    TEST_ASSERT_EQUAL_UINT8(0, gateHallNeighborChange(15, 14, kDocked - 1, kDocked));
    TEST_ASSERT_EQUAL_UINT8(15, gateHallNeighborChange(15, 14, kDocked, kDocked));
}

// A loosely seated cube that drops out and comes back is the same cube; holding
// it to the docked bar would strand it at "no neighbour".
void test_id_change_gate_lets_the_seated_cube_return_weakly() {
    TEST_ASSERT_EQUAL_UINT8(11, gateHallNeighborChange(11, 11, 20, kDocked));
}

// Latching presence already required the docked reading, and "no neighbour" must
// never be held back.
void test_id_change_gate_passes_the_first_id_and_absence() {
    TEST_ASSERT_EQUAL_UINT8(13, gateHallNeighborChange(13, 0, 20, kDocked));
    TEST_ASSERT_EQUAL_UINT8(0, gateHallNeighborChange(0, 11, 200, kDocked));
}

// The tracker and the debounce together, sampled at the 1 kHz poll with the
// production presence tuning. What gets reported is what reaches cube/right.
struct HallRig {
    HallPresenceTracker tracker;
    HallNeighborDebounce neighbor;
    uint32_t now = 0;
    int reported = -1;       // last ID reported, -1 before the first
    bool reported_4 = false;

    explicit HallRig(uint16_t settle_reads) : neighbor{8, settle_reads, 70} {
        tracker.begin(HallPresenceConfig{1, 70, 15, 5, 7, 250});
        feed(kIdle, 0, 0, 1000);  // prime the baseline with nothing docked
    }
    static constexpr int kIdle = 1800;

    // `id` is what the ID magnets decode to while `mask` is what they read.
    void feed(int raw, uint8_t mask, uint8_t id, int ms) {
        for (int i = 0; i < ms; i++) {
            const bool active = tracker.update(raw, now += 1, mask);
            if (neighbor.update(active ? id : 0, active, tracker.delta()) &&
                neighbor.candidate != reported) {
                reported = neighbor.candidate;
                if (reported == 4) reported_4 = true;
                neighbor.reported();
            }
        }
    }
};

// Cube 11 docked, then slid one column so its magnets read as cube 4 (mask
// P4+P5). The filtered delta is still above docked strength for the first few
// ms of the slide, so without the settle the plain 8-read debounce reports 4.
void test_sliding_off_a_neighbour_never_reports_the_alias() {
    HallRig rig(100);
    rig.feed(HallRig::kIdle + 127, 0x30, 11, 500);
    TEST_ASSERT_EQUAL_INT(11, rig.reported);

    rig.feed(HallRig::kIdle + 45, 0x18, 4, 500);
    TEST_ASSERT_FALSE(rig.reported_4);
    TEST_ASSERT_EQUAL_INT(0, rig.reported);
}

// A genuine swap straight to another cube at docked strength still lands.
void test_a_docked_swap_to_another_cube_is_reported_after_settling() {
    HallRig rig(100);
    rig.feed(HallRig::kIdle + 127, 0x30, 11, 500);
    rig.feed(HallRig::kIdle + 127, 0x22, 12, 99);
    TEST_ASSERT_EQUAL_INT(11, rig.reported);
    rig.feed(HallRig::kIdle + 127, 0x22, 12, 1);
    TEST_ASSERT_EQUAL_INT(12, rig.reported);
}

// An ordinary dock is not slowed down by the settle.
void test_a_fresh_dock_is_reported_at_the_plain_debounce() {
    HallRig rig(100);
    rig.feed(HallRig::kIdle + 127, 0x30, 11, 60);
    TEST_ASSERT_EQUAL_INT(11, rig.reported);
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
    // Nothing left to copy out: the RTC cache reads baseline() every poll, so a
    // stale value here survives the recalibrate it was supposed to undo.
    TEST_ASSERT_EQUAL(0, t.baseline());
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

void test_findCubeIpOctet_primaries() {
    TEST_ASSERT_EQUAL(21, findCubeIpOctet("AA:AA:AA:AA:AA:AA"));
    TEST_ASSERT_EQUAL(26, findCubeIpOctet("FF:FF:FF:FF:FF:FF"));
    TEST_ASSERT_EQUAL(31, findCubeIpOctet("01:01:01:01:01:01"));
    TEST_ASSERT_EQUAL(36, findCubeIpOctet("06:06:06:06:06:06"));
}

void test_findCubeIpOctet_backup_is_unique() {
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

    // No record: the fallback is the slot NVS remembers, which is what lets an
    // assigned cube reboot correctly with the broker down.
    TEST_ASSERT_EQUAL(1, resolveAssignedSlot(ASSIGNMENT_MISSING, -1, false, 1));
    TEST_ASSERT_EQUAL(1, resolveAssignedSlot(ASSIGNMENT_MALFORMED, -1, false, 1));
    TEST_ASSERT_EQUAL(-1, resolveAssignedSlot(ASSIGNMENT_MISSING, -1, true, 1));
    TEST_ASSERT_EQUAL(-1, resolveAssignedSlot(ASSIGNMENT_MALFORMED, -1, true, 1));

    // A board that has never been assigned has nothing stored, so it stays
    // unassigned instead of adopting a slot someone else holds. loadStoredSlot()
    // yields -1 for blank NVS; 0 is covered too, since neither is a real slot.
    TEST_ASSERT_TRUE(resolveAssignedSlot(ASSIGNMENT_MISSING, -1, false, -1) <= 0);
    TEST_ASSERT_TRUE(resolveAssignedSlot(ASSIGNMENT_MALFORMED, -1, false, -1) <= 0);
    TEST_ASSERT_TRUE(resolveAssignedSlot(ASSIGNMENT_MISSING, -1, false, 0) <= 0);
    TEST_ASSERT_TRUE(resolveAssignedSlot(ASSIGNMENT_MALFORMED, -1, false, 0) <= 0);
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
  int reachable = -1;  // -1 until recordReachable() is called
  void recordReachable(bool r) override { reachable = r ? 1 : 0; }
  void clearSleepFlag() override { record("clearSleepFlag"); }
  void enterSleep() override { record("enterSleep"); }
  void stayAwake() override { record("stayAwake"); }
};

// No router or no Pi: each failed check-in doubles the sleep, so a rig packed
// away stops waking every 10 s to fail.
void test_checkInIntervalS_doubles_per_unreachable_checkin() {
    TEST_ASSERT_EQUAL_UINT32(10, checkInIntervalS(0, 10, 0, 5400));
    TEST_ASSERT_EQUAL_UINT32(20, checkInIntervalS(0, 10, 1, 5400));
    TEST_ASSERT_EQUAL_UINT32(80, checkInIntervalS(0, 10, 3, 5400));
}

// The doubling stops at the point the cube would give up anyway; a counter run
// up all week still lands there rather than overflowing.
void test_checkInIntervalS_never_sleeps_past_giving_up() {
    TEST_ASSERT_EQUAL_UINT32(5400, checkInIntervalS(0, 10, 10, 5400));
    TEST_ASSERT_EQUAL_UINT32(5400, checkInIntervalS(0, 10, 255, 5400));
}

// Nothing has touched this cube in an hour and a half, so the event is over
// and waking again buys nothing: 0 arms no timer, and only the button or a
// USB-C lead brings it back.
void test_checkInIntervalS_stops_waking_once_the_event_is_over() {
    TEST_ASSERT_EQUAL_UINT32(0, checkInIntervalS(5400, 10, 0, 5400));
    TEST_ASSERT_EQUAL_UINT32(0, checkInIntervalS(9000, 10, 3, 5400));
}

// The doubling must not sleep straight past the give-up point. Nine failed
// check-ins land a cube at 5110 s idle with a 5120 s double queued, which would
// have it waking at 2 h 50 min to give up on a 90 min rule.
void test_checkInIntervalS_backoff_does_not_overshoot_giving_up() {
    TEST_ASSERT_EQUAL_UINT32(290, checkInIntervalS(5110, 10, 9, 5400));
}

// One check-in short of giving up still wakes: the band is closed at the top,
// not approached asymptotically. The last step is whatever is left rather than
// a whole interval, so the cube wakes at the limit and not past it.
void test_checkInIntervalS_still_wakes_just_under_the_limit() {
    TEST_ASSERT_EQUAL_UINT32(10, checkInIntervalS(5390, 10, 0, 5400));
    TEST_ASSERT_EQUAL_UINT32(1, checkInIntervalS(5399, 10, 0, 5400));
}

// ---------------------------------------------------------------------------
// Low-battery latch
// ---------------------------------------------------------------------------
// A cube in play never auto-sleeps -- every letter and border resets the
// activity timer -- so without this gate a game night runs a cell from full to
// the DW01A cutoff at ~0.53A and costs it some life every time. The latch is
// what stops the cube staying awake while the cell is nearly empty.

// The cell sags under a lit panel, so one low reading is a white frame, not a
// flat cell.
void test_battery_latch_ignores_a_single_sag() {
    uint8_t low_run = 0;
    TEST_ASSERT_FALSE(updateBatteryLowLatch(3300, false, &low_run, BATTERY_READING_UNDER_LOAD));
}

void test_battery_latches_after_consecutive_low_readings() {
    uint8_t low_run = 0;
    bool latched = false;
    for (int i = 0; i < BATTERY_LOW_CONFIRM_READS - 1; i++) {
        latched = updateBatteryLowLatch(3300, latched, &low_run, BATTERY_READING_UNDER_LOAD);
        TEST_ASSERT_FALSE(latched);
    }
    TEST_ASSERT_TRUE(updateBatteryLowLatch(3300, latched, &low_run, BATTERY_READING_UNDER_LOAD));
}

// A good reading between sags starts the run over, so a cube at 3.5V flickering
// under full white never latches.
void test_battery_latch_resets_the_run_on_a_good_reading() {
    uint8_t low_run = 0;
    bool latched = false;
    for (int i = 0; i < BATTERY_LOW_CONFIRM_READS - 1; i++) {
        latched = updateBatteryLowLatch(3300, latched, &low_run, BATTERY_READING_UNDER_LOAD);
    }
    latched = updateBatteryLowLatch(3800, latched, &low_run, BATTERY_READING_UNDER_LOAD);
    TEST_ASSERT_FALSE(latched);
    TEST_ASSERT_FALSE(updateBatteryLowLatch(3300, latched, &low_run, BATTERY_READING_UNDER_LOAD));
}

// Hysteresis: recovering past the threshold is not enough, or a cube would
// oscillate between sleeping and waking as the unloaded cell rebounds.
void test_battery_latch_holds_until_well_clear_of_the_threshold() {
    uint8_t low_run = 0;
    TEST_ASSERT_TRUE(updateBatteryLowLatch(BATTERY_LOW_MV + 50, true, &low_run, BATTERY_READING_UNDER_LOAD));
    TEST_ASSERT_FALSE(updateBatteryLowLatch(BATTERY_LOW_CLEAR_MV, true, &low_run, BATTERY_READING_UNDER_LOAD));
}

// A cube awake only for a check-in has no panel lit, so its cell reads about
// 100 mV high. Trusting that to clear the latch would wake it onto a cell that
// is still nearly flat, and it would latch, rebound and wake again until the
// cutoff -- the oscillation the hysteresis exists to prevent, driven by load
// rather than by the threshold.
void test_battery_latch_does_not_clear_on_an_unloaded_rebound() {
    uint8_t low_run = 0;
    TEST_ASSERT_TRUE(updateBatteryLowLatch(BATTERY_LOW_CLEAR_MV + 100, true,
                                           &low_run, BATTERY_READING_UNLOADED));
}

// Latching is the whole point of reading the cell on a sleeping cube: it is
// what stops a stored cube spending its last charge on check-ins.
void test_battery_latch_still_sets_on_unloaded_readings() {
    uint8_t low_run = 0;
    bool latched = false;
    for (int i = 0; i < BATTERY_LOW_CONFIRM_READS; i++) {
        latched = updateBatteryLowLatch(3300, latched, &low_run,
                                        BATTERY_READING_UNLOADED);
    }
    TEST_ASSERT_TRUE(latched);
}

// A charged cell clears the latch, and the run with it, so the next sag needs
// a full confirmation again.
void test_battery_latch_clears_on_a_charged_cell() {
    uint8_t low_run = 2;
    TEST_ASSERT_FALSE(updateBatteryLowLatch(4100, true, &low_run, BATTERY_READING_UNDER_LOAD));
    TEST_ASSERT_EQUAL_UINT8(0, low_run);
}

// A BATTERY_SENSE build on a board whose divider was never wired reads near
// zero. No running cube can be below the DW01A cutoff, so that is missing
// hardware, not a flat cell -- latching on it would stop the cube sleeping
// anything but immediately.
void test_battery_latch_ignores_a_reading_no_running_cube_could_produce() {
    uint8_t low_run = 0;
    bool latched = false;
    for (int i = 0; i < BATTERY_LOW_CONFIRM_READS + 1; i++) {
        latched = updateBatteryLowLatch(0, latched, &low_run, BATTERY_READING_UNDER_LOAD);
    }
    TEST_ASSERT_FALSE(latched);
}

// A latched cube still has to answer its keep-alive check-ins, and on a flat
// cell the Wi-Fi join is the drain -- about 20 mA for the whole timeout when
// there is no broker to find. Deciding before the radio comes up is the point.
void test_battery_low_sends_a_timer_checkin_back_to_sleep() {
    TEST_ASSERT_TRUE(batteryLowEndsWake(WAKE_REASON_TIMER, true));
}

// A brownout on a nearly-flat cell reports as a reset, which runWakeCheckIn()
// deliberately keeps awake when it cannot reach the broker -- so an empty cube
// would brown out, stay awake ten minutes at full draw, and do it again.
void test_battery_low_sends_a_reset_back_to_sleep() {
    TEST_ASSERT_TRUE(batteryLowEndsWake(WAKE_REASON_OTHER, true));
}

// Someone is holding the cube: it comes up far enough to say why it is dark,
// which is the whole reason the warning exists.
void test_battery_low_still_answers_the_button_and_a_cold_boot() {
    TEST_ASSERT_FALSE(batteryLowEndsWake(WAKE_REASON_BUTTON, true));
    TEST_ASSERT_FALSE(batteryLowEndsWake(WAKE_REASON_POWER_ON, true));
}

void test_a_healthy_cell_ends_no_wake() {
    TEST_ASSERT_FALSE(batteryLowEndsWake(WAKE_REASON_TIMER, false));
    TEST_ASSERT_FALSE(batteryLowEndsWake(WAKE_REASON_OTHER, false));
    TEST_ASSERT_FALSE(batteryLowEndsWake(WAKE_REASON_BUTTON, false));
    TEST_ASSERT_FALSE(batteryLowEndsWake(WAKE_REASON_POWER_ON, false));
}

// An interval someone set longer than the give-up point is never shortened.
void test_checkInIntervalS_leaves_a_longer_configured_interval_alone() {
    TEST_ASSERT_EQUAL_UINT32(600, checkInIntervalS(0, 600, 0, 300));
    TEST_ASSERT_EQUAL_UINT32(600, checkInIntervalS(0, 600, 5, 300));
}

// Reaching the broker is what counts: a missing router and a missing Pi both
// fail; a check-in that connected but read no flag still reached it.
void test_runWakeCheckIn_records_whether_the_broker_was_reached() {
    FakeWakeCheckInPorts no_wifi; no_wifi.wifi_result = false;
    runWakeCheckIn(WAKE_REASON_TIMER, no_wifi);
    TEST_ASSERT_EQUAL_INT(0, no_wifi.reachable);

    FakeWakeCheckInPorts no_broker; no_broker.mqtt_result = false;
    runWakeCheckIn(WAKE_REASON_TIMER, no_broker);
    TEST_ASSERT_EQUAL_INT(0, no_broker.reachable);

    FakeWakeCheckInPorts slow_flag; slow_flag.flag_confirmed = false;
    runWakeCheckIn(WAKE_REASON_TIMER, slow_flag);
    TEST_ASSERT_EQUAL_INT(1, slow_flag.reachable);

    FakeWakeCheckInPorts up;
    runWakeCheckIn(WAKE_REASON_TIMER, up);
    TEST_ASSERT_EQUAL_INT(1, up.reachable);
}

// A button wake skips the network entirely, so it says nothing about it.
void test_runWakeCheckIn_button_wake_records_nothing() {
    FakeWakeCheckInPorts ports;
    runWakeCheckIn(WAKE_REASON_BUTTON, ports);
    TEST_ASSERT_EQUAL_INT(-1, ports.reachable);
}

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

// Unplugging USB-C cold-boots a battery cube. That is someone getting the cube
// out, so it wakes like the button even with the sleep flag set -- otherwise a
// cube that slept last night would see its own flag and go straight back.
void test_runWakeCheckIn_power_on_wakes_despite_a_set_sleep_flag() {
    FakeWakeCheckInPorts ports;
    ports.sleep_requested = true;
    runWakeCheckIn(WAKE_REASON_POWER_ON, ports);
    TEST_ASSERT_EQUAL_STRING("stayAwake,", ports.calls);
}

// A reset -- brownout, watchdog, crash -- is not someone deciding to use the
// cube. Until now any of them came up fully awake
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
        decideNfcObservation(true, false, "AABB", "-"));
}

void test_decideNfcObservation_suppresses_an_unchanged_tag() {
    TEST_ASSERT_EQUAL(NFC_OBS_NONE,
        decideNfcObservation(true, false, "AABB", "AABB"));
}

void test_decideNfcObservation_reports_absence_when_both_sensors_agree() {
    TEST_ASSERT_EQUAL(NFC_OBS_ABSENT,
        decideNfcObservation(false, true, "", "AABB"));
}

void test_decideNfcObservation_suppresses_repeated_absence() {
    TEST_ASSERT_EQUAL(NFC_OBS_NONE,
        decideNfcObservation(false, true, "", "-"));
}

void test_decideNfcObservation_ignores_a_failed_read() {
    TEST_ASSERT_EQUAL(NFC_OBS_NONE,
        decideNfcObservation(false, false, "", "AABB"));
}

// ---------------------------------------------------------------------------
// NFC chatter gate
// ---------------------------------------------------------------------------
// Real hardware at the edge of NFC range flipped up to ~10 times/sec
// (measured: 51 flips in 15s, mostly 80-500ms apart). A tag's first
// departure-and-return is always instant -- that is either a new cube or a
// single dropped read on a solid pair, and neither should pay a confirmation
// cost. Only a SECOND flip of the same tag within NFC_CHATTER_WINDOW_MS
// means it is genuinely chattering.

void test_chatterGate_first_connect_is_instant() {
    NfcChatterState state;
    NfcChatterResult result = applyNfcChatterGate(state, NFC_OBS_TAG, true, "AABB", 1000);
    TEST_ASSERT_EQUAL(NFC_OBS_TAG, result.action);
}

void test_chatterGate_first_departure_and_return_is_instant() {
    NfcChatterState state;
    NfcChatterResult connect = applyNfcChatterGate(state, NFC_OBS_TAG, true, "AABB", 1000);
    NfcChatterResult depart = applyNfcChatterGate(connect.state, NFC_OBS_ABSENT, false, "", 1050);
    // The tag's first return, shortly after its first (and only) departure:
    // still instant -- this is one flip, not chatter.
    NfcChatterResult reconnect = applyNfcChatterGate(depart.state, NFC_OBS_TAG, true, "AABB", 1100);
    TEST_ASSERT_EQUAL(NFC_OBS_TAG, reconnect.action);
}

void test_chatterGate_a_republish_with_no_real_change_is_not_a_flip() {
    // `cube/resend` and every MQTT (re)connect clear
    // `last_observation_published` in main.cpp, which makes
    // `decideNfcObservation` re-announce NFC_OBS_TAG for a tag that never
    // actually left -- no intervening NFC_OBS_ABSENT. That must not be
    // recorded as a flip, or it can wrongly gate the tag's real next
    // departure-and-return.
    NfcChatterState state;
    state = applyNfcChatterGate(state, NFC_OBS_TAG, true, "AABB", 1000).state;

    for (int i = 0; i < 5; i++) {
        NfcChatterResult republish =
            applyNfcChatterGate(state, NFC_OBS_TAG, true, "AABB", 1010 + i * 10);
        TEST_ASSERT_EQUAL(NFC_OBS_TAG, republish.action);  // always republished
        state = republish.state;
    }

    // The tag's REAL first departure-and-return must still be instant -- the
    // re-announces above must not have been mistaken for chatter history.
    state = applyNfcChatterGate(state, NFC_OBS_ABSENT, false, "", 1100).state;
    NfcChatterResult reconnect = applyNfcChatterGate(state, NFC_OBS_TAG, true, "AABB", 1150);
    TEST_ASSERT_EQUAL(NFC_OBS_TAG, reconnect.action);
}

void test_chatterGate_second_flip_within_window_is_held() {
    NfcChatterState state;
    state = applyNfcChatterGate(state, NFC_OBS_TAG, true, "AABB", 1000).state;
    state = applyNfcChatterGate(state, NFC_OBS_ABSENT, false, "", 1050).state;
    state = applyNfcChatterGate(state, NFC_OBS_TAG, true, "AABB", 1100).state;   // 1st return: instant
    state = applyNfcChatterGate(state, NFC_OBS_ABSENT, false, "", 1150).state;   // 2nd departure

    // 2nd return within the window: now chattering, held back.
    NfcChatterResult held = applyNfcChatterGate(state, NFC_OBS_TAG, true, "AABB", 1200);
    TEST_ASSERT_EQUAL(NFC_OBS_NONE, held.action);
}

void test_chatterGate_accepts_the_held_reconnect_once_it_settles() {
    NfcChatterState state;
    state = applyNfcChatterGate(state, NFC_OBS_TAG, true, "AABB", 1000).state;
    state = applyNfcChatterGate(state, NFC_OBS_ABSENT, false, "", 1050).state;
    state = applyNfcChatterGate(state, NFC_OBS_TAG, true, "AABB", 1100).state;
    state = applyNfcChatterGate(state, NFC_OBS_ABSENT, false, "", 1150).state;
    state = applyNfcChatterGate(state, NFC_OBS_TAG, true, "AABB", 1200).state; // held, confirm starts at 1200

    // Read stays on AABB (decideNfcObservation would keep saying NFC_OBS_TAG
    // every cycle since last_published is still "-"); after
    // NFC_CHATTER_CONFIRM_MS it is accepted.
    NfcChatterResult still_holding =
        applyNfcChatterGate(state, NFC_OBS_TAG, true, "AABB", 1200 + NFC_CHATTER_CONFIRM_MS - 1);
    TEST_ASSERT_EQUAL(NFC_OBS_NONE, still_holding.action);

    NfcChatterResult confirmed =
        applyNfcChatterGate(state, NFC_OBS_TAG, true, "AABB", 1200 + NFC_CHATTER_CONFIRM_MS);
    TEST_ASSERT_EQUAL(NFC_OBS_TAG, confirmed.action);
}

void test_chatterGate_a_dropout_mid_confirmation_resets_the_hold() {
    NfcChatterState state;
    state = applyNfcChatterGate(state, NFC_OBS_TAG, true, "AABB", 1000).state;
    state = applyNfcChatterGate(state, NFC_OBS_ABSENT, false, "", 1050).state;
    state = applyNfcChatterGate(state, NFC_OBS_TAG, true, "AABB", 1100).state;
    state = applyNfcChatterGate(state, NFC_OBS_ABSENT, false, "", 1150).state;
    state = applyNfcChatterGate(state, NFC_OBS_TAG, true, "AABB", 1200).state; // held, confirm starts at 1200

    // The read drops out again before confirmation completes -- reported as
    // NFC_OBS_NONE (last_published is still "-", so decideNfcObservation
    // would already suppress this), but it must break the confirmation hold.
    state = applyNfcChatterGate(state, NFC_OBS_NONE, false, "", 1250).state;

    // AABB comes back within NFC_CHATTER_CONFIRM_MS of the ORIGINAL hold
    // start (1200); if the hold had not reset, this would already be
    // confirmed. It must not be -- the streak broke and confirmation
    // restarts from here.
    NfcChatterResult result =
        applyNfcChatterGate(state, NFC_OBS_TAG, true, "AABB", 1200 + NFC_CHATTER_CONFIRM_MS);
    TEST_ASSERT_EQUAL(NFC_OBS_NONE, result.action);
}

// Drives `state` into a genuinely chattering hold on "AABB": connect, depart,
// reconnect (1st return, instant), depart, reconnect (2nd return -- this is
// the one that triggers the gate and is left PENDING, unconfirmed).
static NfcChatterState chattering_state_pending_on_aabb() {
    NfcChatterState state;
    state = applyNfcChatterGate(state, NFC_OBS_TAG, true, "AABB", 1000).state;
    state = applyNfcChatterGate(state, NFC_OBS_ABSENT, false, "", 1010).state;
    state = applyNfcChatterGate(state, NFC_OBS_TAG, true, "AABB", 1020).state;
    state = applyNfcChatterGate(state, NFC_OBS_ABSENT, false, "", 1030).state;
    NfcChatterResult pending = applyNfcChatterGate(state, NFC_OBS_TAG, true, "AABB", 1040);
    TEST_ASSERT_EQUAL(NFC_OBS_NONE, pending.action);  // sanity: genuinely held
    return pending.state;
}

void test_chatterGate_never_holds_back_a_disconnect() {
    NfcChatterState state = chattering_state_pending_on_aabb();
    // A reconnect is mid-confirmation-hold, but a disconnect must still
    // publish instantly -- never held, regardless of chatter state.
    NfcChatterResult departed = applyNfcChatterGate(state, NFC_OBS_ABSENT, false, "", 1050);
    TEST_ASSERT_EQUAL(NFC_OBS_ABSENT, departed.action);
}

void test_chatterGate_a_different_tag_connects_instantly_even_during_chatter() {
    NfcChatterState state = chattering_state_pending_on_aabb();
    // AABB has an unresolved chatter hold, but a genuinely different tag (a
    // new cube placed down) must connect instantly -- it has no flip
    // history of its own yet.
    NfcChatterResult result = applyNfcChatterGate(state, NFC_OBS_TAG, true, "CCDD", 1050);
    TEST_ASSERT_EQUAL(NFC_OBS_TAG, result.action);
}

void test_chatterGate_a_stale_flip_outside_the_window_is_not_chatter() {
    NfcChatterState state;
    state = applyNfcChatterGate(state, NFC_OBS_TAG, true, "AABB", 1000).state;
    state = applyNfcChatterGate(state, NFC_OBS_ABSENT, false, "", 1050).state;
    // 1st return: instant, and this is the connect the staleness check is
    // against (`flip_ms_older` becomes 1100 here).
    state = applyNfcChatterGate(state, NFC_OBS_TAG, true, "AABB", 1100).state;
    // Solidly connected for well over NFC_CHATTER_WINDOW_MS before dropping.
    unsigned long stale_departure = 1100 + NFC_CHATTER_WINDOW_MS + 50;
    state = applyNfcChatterGate(state, NFC_OBS_ABSENT, false, "", stale_departure).state;
    // The prior connect (1100) is now well outside the window -- this pair
    // settled, so the reconnect is instant again, not gated as chatter.
    NfcChatterResult result = applyNfcChatterGate(
        state, NFC_OBS_TAG, true, "AABB", stale_departure + 10);
    TEST_ASSERT_EQUAL(NFC_OBS_TAG, result.action);
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
    formatBootIdentity(buf, sizeof(buf), 12, 32);
    TEST_ASSERT_EQUAL_STRING("c12 ip32", buf);
}

void test_boot_identity_shows_an_assigned_slot_against_a_foreign_octet() {
    // The spare: octet 47 from its MAC, slot 1 from the admin page. The two are
    // unrelated, which is the confusion this line exists to end.
    char buf[64];
    formatBootIdentity(buf, sizeof(buf), 1, 47);
    TEST_ASSERT_EQUAL_STRING("c1 ip47", buf);
}

void test_boot_identity_admits_an_unknown_slot() {
    // An unassigned spare knows its octet and nothing else. "c?" is honest;
    // any number here would be a guess, and applySlot() paints NO SLOT next.
    char buf[64];
    formatBootIdentity(buf, sizeof(buf), 0, 47);
    TEST_ASSERT_EQUAL_STRING("c? ip47", buf);
}

void test_boot_identity_negative_stored_slot_is_not_treated_as_assigned() {
    // resolveAssignedSlot() returns -1 for a deliberately unassigned cube, and
    // that value reaches the stored record. It must not print as "c-1".
    char buf[64];
    formatBootIdentity(buf, sizeof(buf), -1, 36);
    TEST_ASSERT_EQUAL_STRING("c? ip36", buf);
}

void test_boot_identity_fits_the_debug_line() {
    // displayDebugMessage() wraps past 10 characters, which would push the
    // nfc/hall line down and reflow the screen this is meant to clarify.
    char buf[64];
    for (int slot = 1; slot <= 16; slot++) {
        for (int octet = 21; octet <= 48; octet++) {
            formatBootIdentity(buf, sizeof(buf), slot, octet);
            TEST_ASSERT_TRUE_MESSAGE(strlen(buf) <= 10, buf);
        }
    }
    formatBootIdentity(buf, sizeof(buf), 0, 48);
    TEST_ASSERT_TRUE_MESSAGE(strlen(buf) <= 10, buf);
}

// The fleet runs two hall board revs with different presence sensors, and the
// deltas in the firmware are the A4 numbers. Getting the gain wrong by a factor
// of four either latches at a quarter of the intended range or not at all, so
// the mapping is worth pinning rather than trusting to a comment.
void test_hall_gain_maps_every_drv5055_variant() {
    TEST_ASSERT_EQUAL(1, hallGainForRev("a4"));   // v2.2, 12.5 mV/mT, the baseline
    TEST_ASSERT_EQUAL(4, hallGainForRev("a2"));   // v2.3+, 50 mV/mT
    TEST_ASSERT_EQUAL(2, hallGainForRev("a3"));
    TEST_ASSERT_EQUAL(8, hallGainForRev("a1"));
    TEST_ASSERT_EQUAL(4, hallGainForRev("A2"));   // case does not matter on the wire
}

// A typo on the wire or a corrupt NVS read must leave the gain alone rather
// than scale every threshold by garbage. 0 is the "no opinion" answer.
void test_hall_gain_rejects_anything_it_cannot_parse() {
    TEST_ASSERT_EQUAL(0, hallGainForRev(""));
    TEST_ASSERT_EQUAL(0, hallGainForRev("a"));
    TEST_ASSERT_EQUAL(0, hallGainForRev("a5"));
    TEST_ASSERT_EQUAL(0, hallGainForRev("a22"));
    TEST_ASSERT_EQUAL(0, hallGainForRev("b2"));
    TEST_ASSERT_EQUAL(0, hallGainForRev("2"));
    TEST_ASSERT_EQUAL(0, hallGainForRev(nullptr));
    TEST_ASSERT_FALSE(plausibleHallGain(0));
    TEST_ASSERT_FALSE(plausibleHallGain(3));
    TEST_ASSERT_FALSE(plausibleHallGain(-4));
    TEST_ASSERT_TRUE(plausibleHallGain(1));
    TEST_ASSERT_TRUE(plausibleHallGain(4));
}

// The reason hallRevAction exists rather than a bare "did the gain change?"
// check. Gain 4 is the v8 build default, so a v8 cube told "a2" is already
// running the right thresholds and needs no reboot -- but with nothing in NVS,
// reflashing that same daughterboard with a v6 hall build (default gain 1) puts
// an A2 board back on A4 thresholds and it reports a neighbour before docking.
// The write is what carries the setting across that reflash.
void test_a_rev_matching_the_build_default_is_still_written_to_nvs() {
    TEST_ASSERT_TRUE(HallRevAction::Persist ==
                     hallRevAction("a2", 0, 4));   // v8 default, NVS empty
    TEST_ASSERT_TRUE(HallRevAction::Persist ==
                     hallRevAction("a4", 0, 1));   // v6 default, NVS empty
    // Same board, gain already stored: nothing left to do.
    TEST_ASSERT_TRUE(HallRevAction::AlreadyApplied ==
                     hallRevAction("a2", 4, 4));
}

// A rev that disagrees with what is running has to reach NVS *and* reboot,
// because the thresholds are copied into the tracker at begin() rather than
// read live. The reboot is also what makes a retained message terminate: the
// cube comes back with running and stored both matching, which is
// AlreadyApplied.
void test_a_rev_that_changes_the_gain_asks_for_a_restart() {
    TEST_ASSERT_TRUE(HallRevAction::PersistAndApply ==
                     hallRevAction("a2", 0, 1));   // v6 build, A2 board fitted
    TEST_ASSERT_TRUE(HallRevAction::PersistAndApply ==
                     hallRevAction("a4", 4, 4));   // A2 board swapped for A4
    TEST_ASSERT_TRUE(HallRevAction::AlreadyApplied ==
                     hallRevAction("a2", 4, 4));   // ... and after the reboot
}

// Garbage on the wire must not be mistaken for a gain change and must not
// trigger a write or a reboot, whatever is stored.
void test_an_unparseable_rev_does_nothing_at_all() {
    TEST_ASSERT_TRUE(HallRevAction::Unparseable == hallRevAction("zz", 4, 4));
    TEST_ASSERT_TRUE(HallRevAction::Unparseable == hallRevAction("", 0, 1));
    TEST_ASSERT_TRUE(HallRevAction::Unparseable == hallRevAction(nullptr, 0, 4));
}

// The surveyed patterns, per player 0 cube. Measured 2026-10-25 with each cube
// read by its right-hand neighbour, three stable rounds each. These are
// hardware facts, so the test restates the measurement rather than the
// implementation: if someone reshuffles the table, this is what disagrees.
void test_every_player0_cube_decodes_from_its_measured_magnets() {
    TEST_ASSERT_EQUAL(1, hallCubeIdForMask(0b000011, 3));  // P1+P2
    TEST_ASSERT_EQUAL(2, hallCubeIdForMask(0b101000, 3));  // P4+P6
    TEST_ASSERT_EQUAL(3, hallCubeIdForMask(0b100100, 4));  // P3+P6
    TEST_ASSERT_EQUAL(4, hallCubeIdForMask(0b011000, 5));  // P4+P5
    TEST_ASSERT_EQUAL(5, hallCubeIdForMask(0b001001, 6));  // P1+P4
    TEST_ASSERT_EQUAL(6, hallCubeIdForMask(0b000110, 2));  // P2+P3
}

void test_every_player1_cube_still_decodes_as_before() {
    TEST_ASSERT_EQUAL(11, hallCubeIdForMask(0b110000, 12));
    TEST_ASSERT_EQUAL(12, hallCubeIdForMask(0b100010, 13));
    TEST_ASSERT_EQUAL(13, hallCubeIdForMask(0b001100, 14));
    TEST_ASSERT_EQUAL(14, hallCubeIdForMask(0b101000, 15));
    TEST_ASSERT_EQUAL(15, hallCubeIdForMask(0b000101, 16));
    TEST_ASSERT_EQUAL(16, hallCubeIdForMask(0b100100, 11));
}

// Why the decode takes the reader's slot at all. Two patterns are fitted in
// both groups, so the same mask has to resolve to different cubes depending on
// who is reading it -- a single table cannot hold both, and a player 0 cube
// reporting "14" would be naming a cube that is not in its game.
void test_the_shared_patterns_resolve_per_player() {
    TEST_ASSERT_EQUAL(2,  hallCubeIdForMask(0b101000, 1));   // P4+P6, read by P0
    TEST_ASSERT_EQUAL(14, hallCubeIdForMask(0b101000, 11));  // same mask, P1
    TEST_ASSERT_EQUAL(3,  hallCubeIdForMask(0b100100, 1));   // P3+P6, read by P0
    TEST_ASSERT_EQUAL(16, hallCubeIdForMask(0b100100, 11));  // same mask, P1
}

// A cube that has not been assigned a slot has no table to read, and a slot
// outside both groups is not a cube. Both must stay silent rather than guess a
// group: naming a neighbour wrongly is worse than naming none.
void test_a_reader_with_no_group_names_nobody() {
    TEST_ASSERT_EQUAL(0, hallCubeIdForMask(0b000011, -1));  // unresolved
    TEST_ASSERT_EQUAL(0, hallCubeIdForMask(0b000011, 0));
    TEST_ASSERT_EQUAL(0, hallCubeIdForMask(0b000011, 7));   // between groups
    TEST_ASSERT_EQUAL(0, hallCubeIdForMask(0b000011, 10));
    TEST_ASSERT_EQUAL(0, hallCubeIdForMask(0b000011, 17));
}

// A mask no cube is fitted with must name nobody. 0b010110 is not hypothetical:
// it is what cube 4 read off cube 6 on the bench while a positioning magnet sat
// over its P5 sensor, and decoding it would have reported a neighbour that was
// not there. Weight is not checked anywhere -- these decode to 0 by being
// absent from the table, which is the property under test.
void test_a_mask_no_cube_is_fitted_with_names_nobody() {
    TEST_ASSERT_EQUAL(0, hallCubeIdForMask(0b000000, 3));  // nothing docked
    TEST_ASSERT_EQUAL(0, hallCubeIdForMask(0b000001, 3));  // one magnet
    TEST_ASSERT_EQUAL(0, hallCubeIdForMask(0b010110, 3));  // P2+P3+P5, the
                                                           // positioning-magnet
                                                           // reading off cube 6
    TEST_ASSERT_EQUAL(0, hallCubeIdForMask(0b111111, 3));  // every line low
    // Weight 2 but no cube fitted with it: still nothing.
    TEST_ASSERT_EQUAL(0, hallCubeIdForMask(0b000101, 3));  // P1+P3 is player 1's
}

// Player 1's boards face right, so such a cube sees the relation the server
// wants stated and is its own subject -- unchanged behaviour.
void test_a_right_facing_cube_asserts_on_its_own_topic() {
    HallEdgeAssertion e = hallEdgeAssertion(12, 13);
    TEST_ASSERT_EQUAL(12, e.topic_slot);    // right/12 ...
    TEST_ASSERT_EQUAL(13, e.subject_slot);  // ... = 13
    // Neighbour gone: it still owns the topic, and says so.
    e = hallEdgeAssertion(12, 0);
    TEST_ASSERT_EQUAL(12, e.topic_slot);
    TEST_ASSERT_EQUAL(0, e.subject_slot);   // publishes "-"
}

// Player 0's boards face left, so the cube it senses is to ITS left: the same
// edge has to be keyed on the neighbour, with this cube as the subject.
// Asserting it the other way round is what reversed the row.
void test_a_left_facing_cube_asserts_on_its_neighbours_topic() {
    const HallEdgeAssertion e = hallEdgeAssertion(3, 1);
    TEST_ASSERT_EQUAL(1, e.topic_slot);    // right/1 ...
    TEST_ASSERT_EQUAL(3, e.subject_slot);  // ... = 3, because 3 is right of 1
}

// With nothing sensed, a left-facing cube has no subject AND no topic: it is
// not the subject of its own cube/right topic, so it must not write there --
// the cube to its right owns that one. The caller retires the topic it last
// wrote instead.
void test_a_left_facing_cube_with_no_neighbour_asserts_nothing() {
    const HallEdgeAssertion e = hallEdgeAssertion(3, 0);
    TEST_ASSERT_EQUAL(0, e.topic_slot);
    TEST_ASSERT_EQUAL(0, e.subject_slot);
}

void test_an_unassigned_cube_asserts_nothing() {
    TEST_ASSERT_EQUAL(0, hallEdgeAssertion(-1, 4).topic_slot);
    TEST_ASSERT_EQUAL(0, hallEdgeAssertion(0, 4).topic_slot);
    TEST_ASSERT_EQUAL(0, hallEdgeAssertion(7, 4).topic_slot);   // between groups
    TEST_ASSERT_EQUAL(0, hallEdgeAssertion(17, 4).topic_slot);
}

void test_the_sensor_side_is_per_player() {
    for (int slot = 1; slot <= 6; slot++) {
        TEST_ASSERT_TRUE(HallSensorSide::Left == hallSensorSideForSlot(slot));
    }
    for (int slot = 11; slot <= 16; slot++) {
        TEST_ASSERT_TRUE(HallSensorSide::Right == hallSensorSideForSlot(slot));
    }
    TEST_ASSERT_TRUE(HallSensorSide::None == hallSensorSideForSlot(-1));
    TEST_ASSERT_TRUE(HallSensorSide::None == hallSensorSideForSlot(7));
}

// The bug this exists to stop, stated as the game sees it. A physical row is
// laid out left to right; each cube's assertion is collected; the row is then
// walked from its head. It has to come back out in the order it went in.
//
// Measured on the bench 2026-10-25: the row 1,3,4,5,6 reported right/3=1,
// right/4=3, right/5=4, right/6=5, which walks out as 6,5,4,3,1 -- the word
// backwards.
static void collectRow(const int* row, int count, int* right_of) {
    for (int i = 0; i < count; i++) {
        // Each cube senses whichever neighbour its own board faces.
        const int sensed =
            hallSensorSideForSlot(row[i]) == HallSensorSide::Left
                ? (i > 0 ? row[i - 1] : 0)
                : (i + 1 < count ? row[i + 1] : 0);
        const HallEdgeAssertion e = hallEdgeAssertion(row[i], (uint8_t)sensed);
        if (e.topic_slot != 0 && e.subject_slot != 0) {
            right_of[e.topic_slot] = e.subject_slot;
        }
    }
}

void test_a_physical_row_reconstructs_in_the_order_it_was_laid_out() {
    // Player 0, left-facing boards.
    const int row0[5] = {1, 3, 4, 5, 6};
    int right_of[20] = {0};
    collectRow(row0, 5, right_of);
    TEST_ASSERT_EQUAL(3, right_of[1]);
    TEST_ASSERT_EQUAL(4, right_of[3]);
    TEST_ASSERT_EQUAL(5, right_of[4]);
    TEST_ASSERT_EQUAL(6, right_of[5]);
    TEST_ASSERT_EQUAL(0, right_of[6]);  // last cube, nothing to its right

    // Player 1, right-facing boards: the same walk, the same direction.
    const int row1[4] = {14, 11, 16, 12};
    int right_of1[20] = {0};
    collectRow(row1, 4, right_of1);
    TEST_ASSERT_EQUAL(11, right_of1[14]);
    TEST_ASSERT_EQUAL(16, right_of1[11]);
    TEST_ASSERT_EQUAL(12, right_of1[16]);
    TEST_ASSERT_EQUAL(0, right_of1[12]);
}

// A right-facing cube is the subject of its own topic, so it clears it on
// connect: a standing "no neighbour" while it has a hall board, and an outright
// delete once it does not, so a hall-era edge cannot outlive the cable.
void test_a_right_facing_cube_clears_its_own_topic() {
    TEST_ASSERT_TRUE(HallEdgeCleanup::ClearOwnStanding ==
                     hallEdgeCleanupOnConnect(12, true));
    TEST_ASSERT_TRUE(HallEdgeCleanup::ClearOwnRetire ==
                     hallEdgeCleanupOnConnect(12, false));
}

// The cable-swap case, and the reason the left-facing branch cannot sit inside
// a magnets-only check. A left-facing cube writes its edge on its NEIGHBOUR's
// topic. Replugged with an NFC reader it stops writing cube/right at all, so
// that hall-era record is precisely the one nothing will ever overwrite -- and
// it cannot clear its own topic instead, because the cube to its right owns
// that one. It has to watch for records naming itself in BOTH modes.
void test_a_left_facing_cube_watches_for_self_claims_in_either_mode() {
    TEST_ASSERT_TRUE(HallEdgeCleanup::WatchSelfClaims ==
                     hallEdgeCleanupOnConnect(3, true));   // hall board fitted
    TEST_ASSERT_TRUE(HallEdgeCleanup::WatchSelfClaims ==
                     hallEdgeCleanupOnConnect(3, false));  // swapped to NFC
}

void test_a_cube_with_no_group_has_nothing_to_retire() {
    TEST_ASSERT_TRUE(HallEdgeCleanup::None == hallEdgeCleanupOnConnect(-1, true));
    TEST_ASSERT_TRUE(HallEdgeCleanup::None == hallEdgeCleanupOnConnect(0, false));
    TEST_ASSERT_TRUE(HallEdgeCleanup::None == hallEdgeCleanupOnConnect(7, true));
    TEST_ASSERT_TRUE(HallEdgeCleanup::None == hallEdgeCleanupOnConnect(17, false));
}

int main(void) {
    UNITY_BEGIN();

    // MAC-to-cube-ID lookup tests
    RUN_TEST(test_num_cube_mac_entries);
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
    RUN_TEST(test_battery_latch_ignores_a_single_sag);
    RUN_TEST(test_battery_low_sends_a_timer_checkin_back_to_sleep);
    RUN_TEST(test_battery_low_sends_a_reset_back_to_sleep);
    RUN_TEST(test_battery_low_still_answers_the_button_and_a_cold_boot);
    RUN_TEST(test_a_healthy_cell_ends_no_wake);
    RUN_TEST(test_battery_latches_after_consecutive_low_readings);
    RUN_TEST(test_battery_latch_resets_the_run_on_a_good_reading);
    RUN_TEST(test_battery_latch_holds_until_well_clear_of_the_threshold);
    RUN_TEST(test_battery_latch_does_not_clear_on_an_unloaded_rebound);
    RUN_TEST(test_battery_latch_still_sets_on_unloaded_readings);
    RUN_TEST(test_battery_latch_clears_on_a_charged_cell);
    RUN_TEST(test_battery_latch_ignores_a_reading_no_running_cube_could_produce);
    RUN_TEST(test_checkInIntervalS_doubles_per_unreachable_checkin);
    RUN_TEST(test_checkInIntervalS_leaves_a_longer_configured_interval_alone);
    RUN_TEST(test_checkInIntervalS_never_sleeps_past_giving_up);
    RUN_TEST(test_checkInIntervalS_stops_waking_once_the_event_is_over);
    RUN_TEST(test_checkInIntervalS_still_wakes_just_under_the_limit);
    RUN_TEST(test_checkInIntervalS_backoff_does_not_overshoot_giving_up);
    RUN_TEST(test_runWakeCheckIn_records_whether_the_broker_was_reached);
    RUN_TEST(test_runWakeCheckIn_button_wake_records_nothing);
    RUN_TEST(test_runWakeCheckIn_wifi_timeout);
    RUN_TEST(test_runWakeCheckIn_mqtt_connect_fails);
    RUN_TEST(test_runWakeCheckIn_flag_set_sleeps_without_clearing);
    RUN_TEST(test_runWakeCheckIn_flag_clear_clears_then_stays_awake);
        RUN_TEST(test_runWakeCheckIn_button_wake_ignores_network);
    RUN_TEST(test_runWakeCheckIn_power_on_wakes_despite_a_set_sleep_flag);
    RUN_TEST(test_runWakeCheckIn_unconfirmed_flag_read_does_not_clear_or_wake);
    RUN_TEST(test_runWakeCheckIn_reset_with_unconfirmed_flag_read_stays_awake);
    RUN_TEST(test_runWakeCheckIn_reset_obeys_a_set_sleep_flag);
    RUN_TEST(test_runWakeCheckIn_reset_without_network_stays_awake);
    RUN_TEST(test_runWakeCheckIn_reset_with_no_flag_set_stays_awake);
    RUN_TEST(test_runWakeCheckIn_reset_obeys_the_device_flag);

    // Neighbor observation protocol tests
    RUN_TEST(test_decideNfcObservation_publishes_a_new_tag);
    RUN_TEST(test_decideNfcObservation_suppresses_an_unchanged_tag);
    RUN_TEST(test_decideNfcObservation_reports_absence_when_both_sensors_agree);
    RUN_TEST(test_decideNfcObservation_suppresses_repeated_absence);
    RUN_TEST(test_decideNfcObservation_ignores_a_failed_read);
    RUN_TEST(test_chatterGate_first_connect_is_instant);
    RUN_TEST(test_chatterGate_first_departure_and_return_is_instant);
    RUN_TEST(test_chatterGate_a_republish_with_no_real_change_is_not_a_flip);
    RUN_TEST(test_chatterGate_second_flip_within_window_is_held);
    RUN_TEST(test_chatterGate_accepts_the_held_reconnect_once_it_settles);
    RUN_TEST(test_chatterGate_a_dropout_mid_confirmation_resets_the_hold);
    RUN_TEST(test_chatterGate_never_holds_back_a_disconnect);
    RUN_TEST(test_chatterGate_a_different_tag_connects_instantly_even_during_chatter);
    RUN_TEST(test_chatterGate_a_stale_flip_outside_the_window_is_not_chatter);
    RUN_TEST(test_buildObservationPayload_carries_protocol_boot_id_and_tag);
    RUN_TEST(test_buildObservationPayload_encodes_no_neighbor);
    RUN_TEST(test_buildObservationPayload_has_no_provenance_fields);

    // Hall presence tracker
    RUN_TEST(test_presence_starts_inactive_and_adopts_the_settled_reading);
    RUN_TEST(test_presence_asserts_above_on_delta_and_holds_through_hysteresis);
    RUN_TEST(test_presence_ignores_the_wrong_direction);
    RUN_TEST(test_presence_freezes_the_baseline_through_a_wrong_pole_excursion);
    RUN_TEST(test_presence_still_adapts_through_drift_sized_negative_wander);
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
    RUN_TEST(test_id_change_to_another_cube_needs_a_docked_reading);
    RUN_TEST(test_id_change_gate_lets_the_seated_cube_return_weakly);
    RUN_TEST(test_id_change_gate_passes_the_first_id_and_absence);
    RUN_TEST(test_sliding_off_a_neighbour_never_reports_the_alias);
    RUN_TEST(test_a_docked_swap_to_another_cube_is_reported_after_settling);
    RUN_TEST(test_a_fresh_dock_is_reported_at_the_plain_debounce);
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
    RUN_TEST(test_boot_identity_shows_an_assigned_slot_against_a_foreign_octet);
    RUN_TEST(test_boot_identity_admits_an_unknown_slot);
    RUN_TEST(test_boot_identity_negative_stored_slot_is_not_treated_as_assigned);
    RUN_TEST(test_boot_identity_fits_the_debug_line);

    RUN_TEST(test_hall_gain_maps_every_drv5055_variant);
    RUN_TEST(test_hall_gain_rejects_anything_it_cannot_parse);
    RUN_TEST(test_a_rev_matching_the_build_default_is_still_written_to_nvs);
    RUN_TEST(test_a_rev_that_changes_the_gain_asks_for_a_restart);
    RUN_TEST(test_an_unparseable_rev_does_nothing_at_all);
    RUN_TEST(test_every_player0_cube_decodes_from_its_measured_magnets);
    RUN_TEST(test_every_player1_cube_still_decodes_as_before);
    RUN_TEST(test_the_shared_patterns_resolve_per_player);
    RUN_TEST(test_a_reader_with_no_group_names_nobody);
    RUN_TEST(test_a_mask_no_cube_is_fitted_with_names_nobody);
    RUN_TEST(test_a_right_facing_cube_asserts_on_its_own_topic);
    RUN_TEST(test_a_left_facing_cube_asserts_on_its_neighbours_topic);
    RUN_TEST(test_a_left_facing_cube_with_no_neighbour_asserts_nothing);
    RUN_TEST(test_an_unassigned_cube_asserts_nothing);
    RUN_TEST(test_the_sensor_side_is_per_player);
    RUN_TEST(test_a_physical_row_reconstructs_in_the_order_it_was_laid_out);
    RUN_TEST(test_a_right_facing_cube_clears_its_own_topic);
    RUN_TEST(test_a_left_facing_cube_watches_for_self_claims_in_either_mode);
    RUN_TEST(test_a_cube_with_no_group_has_nothing_to_retire);

    return UNITY_END();
}
