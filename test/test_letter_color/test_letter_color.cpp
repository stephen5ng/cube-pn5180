#include <unity.h>
#include "../../src/letter_color.h"

static const uint16_t DEFAULT_COLOR = 0xFDCC;

void setUp(void) {}
void tearDown(void) {}

void test_rgb565_hex_parses(void) {
  TEST_ASSERT_EQUAL_HEX16(0xFD20, parseLetterColor("0xFD20", DEFAULT_COLOR));
  TEST_ASSERT_EQUAL_HEX16(0xC77F, parseLetterColor("0xc77f", DEFAULT_COLOR));
  TEST_ASSERT_EQUAL_HEX16(0x0000, parseLetterColor("0x0000", DEFAULT_COLOR));
}

void test_empty_restores_the_default(void) {
  TEST_ASSERT_EQUAL_HEX16(DEFAULT_COLOR, parseLetterColor("", DEFAULT_COLOR));
  TEST_ASSERT_EQUAL_HEX16(DEFAULT_COLOR, parseLetterColor(nullptr, DEFAULT_COLOR));
}

void test_garbage_restores_the_default(void) {
  const char* bad[] = {"FD20", "0x", "0xFD2", "0xFD200", "0xFDZ0", "orange", "0XFD20x"};
  for (const char* p : bad) {
    TEST_ASSERT_EQUAL_HEX16_MESSAGE(DEFAULT_COLOR, parseLetterColor(p, DEFAULT_COLOR), p);
  }
}

int main(int argc, char** argv) {
  UNITY_BEGIN();
  RUN_TEST(test_rgb565_hex_parses);
  RUN_TEST(test_empty_restores_the_default);
  RUN_TEST(test_garbage_restores_the_default);
  return UNITY_END();
}
