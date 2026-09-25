#include <unity.h>
#include "../../src/border_transition.h"

static const unsigned long DURATION = 1000;
static const unsigned long WINDOW = 16;
static const uint16_t WHITE = 0xFFFF;

static BorderSides endLeft() {
  BorderSides s;
  s.top = s.bottom = s.left = WHITE;
  return s;
}

static BorderSides none() { return BorderSides(); }

// The firmware ticks the animation every loop pass, far more often than
// messages arrive, so the tests tick every millisecond up to each delivery.
static unsigned long clock_ms = 0;

static void advance(BorderTransition& b, unsigned long until) {
  for (; clock_ms < until; clock_ms++) b.tick(clock_ms, DURATION);
  b.tick(until, DURATION);
}

static void deliver(BorderTransition& b, unsigned long now, const BorderSides& target) {
  advance(b, now);
  b.set(target, now, WINDOW);
}

static void settle(BorderTransition& b, unsigned long now) {
  advance(b, now + 10 * DURATION);
}

void setUp(void) { clock_ms = 0; }
void tearDown(void) {}

// Cube 1's border traffic on the rig (server timestamps, ms): the chain
// flickered on and off and the last message cleared it, but the cube kept
// showing its left-end border.
void test_the_last_border_sent_is_the_one_shown(void) {
  BorderTransition b;
  deliver(b, 52869, endLeft());
  deliver(b, 53070, none());
  deliver(b, 53336, endLeft());
  deliver(b, 53802, none());
  deliver(b, 54103, endLeft());
  deliver(b, 54171, none());
  deliver(b, 54570, endLeft());
  deliver(b, 54636, none());
  settle(b, 54636);
  TEST_ASSERT_FALSE(b.active);
  TEST_ASSERT_TRUE(b.to == none());
}

// The smallest form of the same thing: while animating toward A, B is queued,
// then A is asked for again. A is the latest request, so B must not follow.
void test_a_repeat_of_the_target_drops_what_was_queued(void) {
  BorderTransition b;
  deliver(b, 0, endLeft());
  deliver(b, 100, none());
  deliver(b, 200, endLeft());
  settle(b, 200);
  TEST_ASSERT_TRUE(b.to == endLeft());
}

void test_a_border_queued_mid_animation_follows_it(void) {
  BorderTransition b;
  deliver(b, 0, endLeft());
  deliver(b, 100, none());
  TEST_ASSERT_TRUE(b.to == endLeft());
  b.tick(DURATION, DURATION);
  TEST_ASSERT_TRUE(b.active);
  TEST_ASSERT_TRUE(b.from == endLeft());
  TEST_ASSERT_TRUE(b.to == none());
}

void test_a_repeat_of_a_settled_border_starts_no_animation(void) {
  BorderTransition b;
  deliver(b, 0, endLeft());
  settle(b, 0);
  deliver(b, 5000, endLeft());
  TEST_ASSERT_FALSE(b.active);
}

void test_an_update_inside_the_window_replaces_the_target(void) {
  BorderTransition b;
  deliver(b, 0, endLeft());
  deliver(b, WINDOW, none());
  TEST_ASSERT_TRUE(b.active);
  TEST_ASSERT_FALSE(b.has_pending);
  TEST_ASSERT_TRUE(b.to == none());
}

int main(int argc, char** argv) {
  UNITY_BEGIN();
  RUN_TEST(test_the_last_border_sent_is_the_one_shown);
  RUN_TEST(test_a_repeat_of_the_target_drops_what_was_queued);
  RUN_TEST(test_a_border_queued_mid_animation_follows_it);
  RUN_TEST(test_a_repeat_of_a_settled_border_starts_no_animation);
  RUN_TEST(test_an_update_inside_the_window_replaces_the_target);
  return UNITY_END();
}
