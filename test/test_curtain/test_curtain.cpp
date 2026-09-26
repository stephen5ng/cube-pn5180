#include <unity.h>
#include "../../src/curtain.h"

static const uint8_t ROWS = 64;

void setUp(void) {}
void tearDown(void) {}

void test_red_and_green_parse(void) {
  Curtain c;
  TEST_ASSERT_TRUE(c.set("R12", ROWS));
  TEST_ASSERT_EQUAL_CHAR('R', c.color);
  TEST_ASSERT_EQUAL_UINT8(12, c.rows);
  TEST_ASSERT_TRUE(c.set("G64", ROWS));
  TEST_ASSERT_EQUAL_CHAR('G', c.color);
  TEST_ASSERT_EQUAL_UINT8(64, c.rows);
}

void test_same_payload_is_not_a_change(void) {
  Curtain c;
  c.set("R12", ROWS);
  TEST_ASSERT_FALSE(c.set("R12", ROWS));
}

void test_empty_clears(void) {
  Curtain c;
  c.set("R12", ROWS);
  TEST_ASSERT_TRUE(c.set("", ROWS));
  TEST_ASSERT_EQUAL_CHAR(0, c.color);
  TEST_ASSERT_EQUAL_UINT8(0, c.rows);
}

void test_garbage_clears(void) {
  const char* bad[] = {"X12", "R", "R-3", "Rabc", "r12"};
  for (const char* p : bad) {
    Curtain c;
    c.set("G5", ROWS);
    c.set(p, ROWS);
    TEST_ASSERT_EQUAL_CHAR_MESSAGE(0, c.color, p);
    TEST_ASSERT_EQUAL_UINT8_MESSAGE(0, c.rows, p);
  }
}

void test_oversized_clamps(void) {
  Curtain c;
  c.set("R999", ROWS);
  TEST_ASSERT_EQUAL_UINT8(64, c.rows);
}

int main(int argc, char** argv) {
  UNITY_BEGIN();
  RUN_TEST(test_red_and_green_parse);
  RUN_TEST(test_same_payload_is_not_a_change);
  RUN_TEST(test_empty_clears);
  RUN_TEST(test_garbage_clears);
  RUN_TEST(test_oversized_clamps);
  return UNITY_END();
}
