#include <unity.h>
#include "../../src/wake_flicker.h"

static const uint16_t ASLEEP = 0x31A6;
static const uint16_t AWAKE = 0xFD20;

void setUp(void) {}
void tearDown(void) {}

void test_it_starts_asleep_and_ends_awake(void) {
  TEST_ASSERT_EQUAL_HEX16(ASLEEP, wakeFlickerColor(0, ASLEEP, AWAKE));
  TEST_ASSERT_EQUAL_HEX16(AWAKE, wakeFlickerColor(WAKE_FLICKER_MS, ASLEEP, AWAKE));
  TEST_ASSERT_EQUAL_HEX16(AWAKE, wakeFlickerColor(100000, ASLEEP, AWAKE));
}

void test_a_strike_fails_before_it_stays_on(void) {
  TEST_ASSERT_EQUAL_HEX16(AWAKE, wakeFlickerColor(300, ASLEEP, AWAKE));
  TEST_ASSERT_EQUAL_HEX16(ASLEEP, wakeFlickerColor(340, ASLEEP, AWAKE));
}

void test_steps_hold_until_the_next(void) {
  TEST_ASSERT_EQUAL_UINT8(25, wakePercent(100));
  TEST_ASSERT_EQUAL_UINT8(25, wakePercent(169));
  TEST_ASSERT_EQUAL_UINT8(0, wakePercent(170));
}

void test_a_glow_sits_between_asleep_and_awake_on_every_channel(void) {
  uint16_t glow = wakeFlickerColor(100, ASLEEP, AWAKE);
  TEST_ASSERT_NOT_EQUAL(ASLEEP, glow);
  TEST_ASSERT_NOT_EQUAL(AWAKE, glow);
  // A quarter of the way on each: red 6 -> 31, green 13 -> 41, blue 6 -> 0.
  TEST_ASSERT_EQUAL_INT(12, glow >> 11);
  TEST_ASSERT_EQUAL_INT(20, (glow >> 5) & 0x3F);
  TEST_ASSERT_EQUAL_INT(5, glow & 0x1F);
}

int main(int argc, char** argv) {
  UNITY_BEGIN();
  RUN_TEST(test_it_starts_asleep_and_ends_awake);
  RUN_TEST(test_a_strike_fails_before_it_stays_on);
  RUN_TEST(test_steps_hold_until_the_next);
  RUN_TEST(test_a_glow_sits_between_asleep_and_awake_on_every_channel);
  return UNITY_END();
}
