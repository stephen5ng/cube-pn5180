#include <unity.h>
#include <string.h>
#include "../../src/panel_text.h"

static const uint8_t PANEL = 64;
static const uint8_t SIZE = 2;

void setUp(void) {}
void tearDown(void) {}

static void assert_line(const PanelTextLine& line, const char* word) {
  TEST_ASSERT_EQUAL_UINT8(strlen(word), line.length);
  TEST_ASSERT_EQUAL_STRING_LEN(word, line.begin, line.length);
}

void test_game_over_is_a_word_per_line(void) {
  PanelTextLine lines[4];
  TEST_ASSERT_EQUAL_UINT8(2, layoutPanelText("GAME OVER", PANEL, PANEL, SIZE, 100, lines, 4));
  assert_line(lines[0], "GAME");
  assert_line(lines[1], "OVER");
}

void test_arrived_block_is_centered(void) {
  PanelTextLine lines[4];
  layoutPanelText("GAME OVER", PANEL, PANEL, SIZE, 100, lines, 4);
  // Two 16px lines centered on a 64px panel: 16 above, 16 below.
  TEST_ASSERT_EQUAL_INT16(16, lines[0].y);
  TEST_ASSERT_EQUAL_INT16(32, lines[1].y);
  // A four-glyph word is 46px of ink at size 2, so 9px of margin each side.
  TEST_ASSERT_EQUAL_INT16(9, lines[0].x);
  TEST_ASSERT_EQUAL_INT16(9, lines[1].x);
}

void test_the_block_starts_entirely_above_the_panel(void) {
  PanelTextLine lines[4];
  layoutPanelText("GAME OVER", PANEL, PANEL, SIZE, 0, lines, 4);
  // Its last row is the row above the panel's first.
  TEST_ASSERT_EQUAL_INT16(0, lines[1].y + PANEL_TEXT_GLYPH_H * SIZE);
}

void test_the_exit_leaves_entirely_below_the_panel(void) {
  PanelTextLine lines[4];
  // drawMessage walks the outgoing message from 100 to 200.
  layoutPanelText("GAME OVER", PANEL, PANEL, SIZE, 200, lines, 4);
  TEST_ASSERT_EQUAL_INT16(PANEL, lines[0].y);
}

void test_descent_is_monotonic(void) {
  PanelTextLine lines[4];
  int16_t previous = -32767;
  for (uint16_t pct = 0; pct <= 200; pct += 10) {
    layoutPanelText("GAME OVER", PANEL, PANEL, SIZE, pct, lines, 4);
    TEST_ASSERT_TRUE_MESSAGE(lines[0].y > previous, "block moved up");
    previous = lines[0].y;
  }
}

void test_runs_of_spaces_make_no_empty_lines(void) {
  PanelTextLine lines[4];
  TEST_ASSERT_EQUAL_UINT8(2, layoutPanelText("  GAME   OVER ", PANEL, PANEL, SIZE, 100, lines, 4));
  assert_line(lines[0], "GAME");
  assert_line(lines[1], "OVER");
}

void test_a_blank_message_has_no_lines(void) {
  PanelTextLine lines[4];
  TEST_ASSERT_EQUAL_UINT8(0, layoutPanelText("   ", PANEL, PANEL, SIZE, 100, lines, 4));
  TEST_ASSERT_EQUAL_UINT8(0, layoutPanelText("", PANEL, PANEL, SIZE, 100, lines, 4));
}

void test_words_past_the_line_budget_are_dropped(void) {
  PanelTextLine lines[2];
  TEST_ASSERT_EQUAL_UINT8(2, layoutPanelText("ONE TWO THREE", PANEL, PANEL, SIZE, 100, lines, 2));
  assert_line(lines[1], "TWO");
}

void test_a_word_wider_than_the_panel_starts_at_the_edge(void) {
  PanelTextLine lines[4];
  layoutPanelText("ABCDEFGHIJ", PANEL, PANEL, SIZE, 100, lines, 4);
  TEST_ASSERT_EQUAL_INT16(0, lines[0].x);
}

int main(int argc, char** argv) {
  UNITY_BEGIN();
  RUN_TEST(test_game_over_is_a_word_per_line);
  RUN_TEST(test_arrived_block_is_centered);
  RUN_TEST(test_the_block_starts_entirely_above_the_panel);
  RUN_TEST(test_the_exit_leaves_entirely_below_the_panel);
  RUN_TEST(test_descent_is_monotonic);
  RUN_TEST(test_runs_of_spaces_make_no_empty_lines);
  RUN_TEST(test_a_blank_message_has_no_lines);
  RUN_TEST(test_words_past_the_line_budget_are_dropped);
  RUN_TEST(test_a_word_wider_than_the_panel_starts_at_the_edge);
  return UNITY_END();
}
