#include <unity.h>
#include <string.h>
#include "../../src/name_entry_display.h"

void setUp(void) {}
void tearDown(void) {}

void test_palette_matches_the_spec_hex(void) {
  // Independently converted from the spec's section 8 RGB tuples, not from
  // the macro under test.
  TEST_ASSERT_EQUAL_HEX16(0xFF20, ENTRY_YELLOW);
  TEST_ASSERT_EQUAL_HEX16(0xF7BF, ENTRY_WHITE);
  TEST_ASSERT_EQUAL_HEX16(0x07FF, ENTRY_CYAN);
  TEST_ASSERT_EQUAL_HEX16(0xFDCC, ENTRY_AMBER);
  TEST_ASSERT_EQUAL_HEX16(0xD405, ENTRY_STREAM_AMBER);
  TEST_ASSERT_EQUAL_HEX16(0x5182, ENTRY_STREAM_DIM);
  TEST_ASSERT_EQUAL_HEX16(0x2987, ENTRY_DARK_SLATE);
  TEST_ASSERT_EQUAL_HEX16(0x1966, ENTRY_RAIL_DOT);
}

void test_ui_mode_parses_all_five_modes(void) {
  UiMode mode = UI_MODE_GAME_NORMAL;
  TEST_ASSERT_TRUE(parseUiMode("{\"mode\": \"NAME_ENTRY\"}", &mode));
  TEST_ASSERT_EQUAL(UI_MODE_NAME_ENTRY, mode);
  TEST_ASSERT_TRUE(parseUiMode("{\"mode\": \"LETTER_STREAM\"}", &mode));
  TEST_ASSERT_EQUAL(UI_MODE_LETTER_STREAM, mode);
  TEST_ASSERT_TRUE(parseUiMode("{\"mode\": \"OK_BOX\"}", &mode));
  TEST_ASSERT_EQUAL(UI_MODE_OK_BOX, mode);
  TEST_ASSERT_TRUE(parseUiMode("{\"mode\": \"ARROW_BIDIRECTIONAL\"}", &mode));
  TEST_ASSERT_EQUAL(UI_MODE_ARROW_BIDIRECTIONAL, mode);
  TEST_ASSERT_TRUE(parseUiMode("{\"mode\": \"GAME_NORMAL\"}", &mode));
  TEST_ASSERT_EQUAL(UI_MODE_GAME_NORMAL, mode);
}

void test_ui_mode_rejects_the_unknown(void) {
  UiMode mode = UI_MODE_NAME_ENTRY;
  TEST_ASSERT_FALSE(parseUiMode("{\"mode\": \"NOPE\"}", &mode));
  TEST_ASSERT_FALSE(parseUiMode("not json", &mode));
  TEST_ASSERT_FALSE(parseUiMode("", &mode));
  TEST_ASSERT_EQUAL(UI_MODE_NAME_ENTRY, mode);  // untouched
}

void test_name_entry_state_parses_typing_and_submit(void) {
  NameEntryState state;
  TEST_ASSERT_TRUE(parseNameEntryState(
      "{\"initials\": \"SNG\", \"active_slot\": 2, \"submit_focused\": false}", &state));
  TEST_ASSERT_EQUAL_STRING("SNG", state.initials);
  TEST_ASSERT_EQUAL_INT8(2, state.active_slot);
  TEST_ASSERT_FALSE(state.submit_focused);
  TEST_ASSERT_TRUE(parseNameEntryState(
      "{\"initials\": \"SNG\", \"active_slot\": -1, \"submit_focused\": true}", &state));
  TEST_ASSERT_EQUAL_INT8(-1, state.active_slot);
  TEST_ASSERT_TRUE(state.submit_focused);
}

void test_name_entry_state_rejects_and_leaves_state_untouched(void) {
  NameEntryState state;
  strcpy(state.initials, "AAA");
  state.active_slot = 0;
  state.submit_focused = false;
  NameEntryState before = state;
  // Short, long, and non-letter initials.
  TEST_ASSERT_FALSE(parseNameEntryState(
      "{\"initials\": \"SN\", \"active_slot\": 0, \"submit_focused\": false}", &state));
  TEST_ASSERT_FALSE(parseNameEntryState(
      "{\"initials\": \"SNGX\", \"active_slot\": 0, \"submit_focused\": false}", &state));
  TEST_ASSERT_FALSE(parseNameEntryState(
      "{\"initials\": \"SN!\", \"active_slot\": 0, \"submit_focused\": false}", &state));
  // Slots past SUBMIT (-1) and slot 2, and a number with junk on it.
  TEST_ASSERT_FALSE(parseNameEntryState(
      "{\"initials\": \"SNG\", \"active_slot\": 3, \"submit_focused\": false}", &state));
  TEST_ASSERT_FALSE(parseNameEntryState(
      "{\"initials\": \"SNG\", \"active_slot\": -2, \"submit_focused\": false}", &state));
  TEST_ASSERT_FALSE(parseNameEntryState(
      "{\"initials\": \"SNG\", \"active_slot\": 2x, \"submit_focused\": false}", &state));
  // A missing key and a non-bool flag.
  TEST_ASSERT_FALSE(parseNameEntryState("{\"initials\": \"SNG\", \"active_slot\": 0}", &state));
  TEST_ASSERT_FALSE(parseNameEntryState(
      "{\"initials\": \"SNG\", \"active_slot\": 0, \"submit_focused\": \"no\"}", &state));
  TEST_ASSERT_EQUAL_MEMORY(&before, &state, sizeof(state));
}

void test_stream_state_parses_and_rejects(void) {
  StreamState state;
  TEST_ASSERT_TRUE(parseStreamState(
      "{\"center_letter\": \"G\", \"direction\": \"FORWARD\", \"speed\": \"FAST\"}", &state));
  TEST_ASSERT_EQUAL_CHAR('G', state.center_letter);
  TEST_ASSERT_EQUAL(STREAM_FORWARD, state.direction);
  TEST_ASSERT_EQUAL(STREAM_FAST, state.speed);
  TEST_ASSERT_TRUE(parseStreamState(
      "{\"center_letter\": \"A\", \"direction\": \"STOP\", \"speed\": \"SLOW\"}", &state));
  TEST_ASSERT_EQUAL(STREAM_STOP, state.direction);

  StreamState before = state;
  TEST_ASSERT_FALSE(parseStreamState(
      "{\"center_letter\": \"g\", \"direction\": \"STOP\", \"speed\": \"SLOW\"}", &state));
  TEST_ASSERT_FALSE(parseStreamState(
      "{\"center_letter\": \"GG\", \"direction\": \"STOP\", \"speed\": \"SLOW\"}", &state));
  TEST_ASSERT_FALSE(parseStreamState(
      "{\"center_letter\": \"G\", \"direction\": \"SIDEWAYS\", \"speed\": \"SLOW\"}", &state));
  TEST_ASSERT_FALSE(parseStreamState(
      "{\"center_letter\": \"G\", \"direction\": \"STOP\", \"speed\": \"WARP\"}", &state));
  TEST_ASSERT_EQUAL_MEMORY(&before, &state, sizeof(state));
}

void test_arrow_state_parses_and_rejects(void) {
  ArrowState state;
  TEST_ASSERT_TRUE(parseArrowState(
      "{\"active\": true, \"active_direction\": \"LEFT\"}", &state));
  TEST_ASSERT_TRUE(state.active);
  TEST_ASSERT_EQUAL(ARROW_LEFT, state.direction);
  TEST_ASSERT_TRUE(parseArrowState(
      "{\"active\": false, \"active_direction\": \"NONE\"}", &state));
  TEST_ASSERT_FALSE(state.active);
  TEST_ASSERT_EQUAL(ARROW_NONE, state.direction);
  // Key order swapped: "active" must still read the bool, not the direction.
  TEST_ASSERT_TRUE(parseArrowState(
      "{\"active_direction\": \"RIGHT\", \"active\": true}", &state));
  TEST_ASSERT_TRUE(state.active);
  TEST_ASSERT_EQUAL(ARROW_RIGHT, state.direction);

  ArrowState before = state;
  TEST_ASSERT_FALSE(parseArrowState(
      "{\"active\": true, \"active_direction\": \"UP\"}", &state));
  TEST_ASSERT_FALSE(parseArrowState(
      "{\"active\": \"yes\", \"active_direction\": \"LEFT\"}", &state));
  TEST_ASSERT_EQUAL_MEMORY(&before, &state, sizeof(state));
}

void test_header_and_prompt_center_on_the_panel(void) {
  // "HIGH SCORE!" is 11 glyphs of 6px: 65px of ink on a 64px panel, so the
  // last column -- the trailing gap, never ink -- clips at x = 0.
  TEST_ASSERT_EQUAL_INT16(0, entryCenteredX(11));
  TEST_ASSERT_EQUAL_INT16(2, entryCenteredX(10));  // "YOUR NAME:"
  TEST_ASSERT_EQUAL_INT16(29, entryCenteredX(1));
}

void test_slots_center_on_10_31_and_52(void) {
  TEST_ASSERT_EQUAL_INT16(10, entrySlotCenterX(0));
  TEST_ASSERT_EQUAL_INT16(31, entrySlotCenterX(1));
  TEST_ASSERT_EQUAL_INT16(52, entrySlotCenterX(2));
  TEST_ASSERT_EQUAL_INT16(5, entrySlotTextX(0));
  TEST_ASSERT_EQUAL_INT16(26, entrySlotTextX(1));
  TEST_ASSERT_EQUAL_INT16(47, entrySlotTextX(2));
  // 10px of ink each: inside the panel, never overlapping.
  TEST_ASSERT_TRUE(entrySlotTextX(0) + 10 <= entrySlotTextX(1));
  TEST_ASSERT_TRUE(entrySlotTextX(1) + 10 <= entrySlotTextX(2));
  TEST_ASSERT_TRUE(entrySlotTextX(2) + 10 <= 64);
}

void test_active_slot_is_cyan_rest_amber(void) {
  TEST_ASSERT_EQUAL_HEX16(ENTRY_CYAN, entrySlotColor(1, 1));
  TEST_ASSERT_EQUAL_HEX16(ENTRY_AMBER, entrySlotColor(0, 1));
  TEST_ASSERT_EQUAL_HEX16(ENTRY_AMBER, entrySlotColor(2, 1));
  // SUBMIT focused (-1): every slot amber.
  TEST_ASSERT_EQUAL_HEX16(ENTRY_AMBER, entrySlotColor(0, -1));
}

void test_chevron_grows_from_a_2px_tip(void) {
  TEST_ASSERT_EQUAL_UINT8(1, entryChevronHalfWidth(0));
  TEST_ASSERT_EQUAL_UINT8(4, entryChevronHalfWidth(3));
}

void test_submit_glyphs_cover_submit_only(void) {
  static const uint8_t s[5] = {0x7, 0x4, 0x7, 0x1, 0x7};
  TEST_ASSERT_EQUAL_MEMORY(s, entrySubmitGlyph('S'), 5);
  for (const char* word = "SUBMIT"; *word; ++word) {
    const uint8_t* glyph = entrySubmitGlyph(*word);
    TEST_ASSERT_NOT_NULL(glyph);
    for (uint8_t row = 0; row < 5; ++row) {
      TEST_ASSERT_LESS_THAN_UINT8(8, glyph[row]);  // 3px wide
    }
  }
  TEST_ASSERT_NULL(entrySubmitGlyph('A'));
  // Six glyphs of 3px + 1px gap fit the 30x9 button.
  TEST_ASSERT_EQUAL_INT16(20, ENTRY_SUBMIT_TEXT_X);
  TEST_ASSERT_TRUE(ENTRY_SUBMIT_TEXT_X + 6 * 4 - 1 <= ENTRY_SUBMIT_X + ENTRY_SUBMIT_W);
  TEST_ASSERT_TRUE(ENTRY_SUBMIT_TEXT_Y + 5 <= ENTRY_SUBMIT_Y + ENTRY_SUBMIT_H);
}

void test_cylinder_centers_and_clamps(void) {
  TEST_ASSERT_FLOAT_WITHIN(0.001f, 31.5f, streamLetterX(0.0f));
  TEST_ASSERT_FLOAT_WITHIN(0.001f, 63.5f, streamLetterX(2.5f));
  TEST_ASSERT_FLOAT_WITHIN(0.001f, -0.5f, streamLetterX(-2.5f));
  TEST_ASSERT_FLOAT_WITHIN(0.001f, 63.5f, streamLetterX(9.0f));
  TEST_ASSERT_FLOAT_WITHIN(0.001f, -0.5f, streamLetterX(-9.0f));
  TEST_ASSERT_FLOAT_WITHIN(0.02f, 50.31f, streamLetterX(1.0f));
  for (float d = -2.5f; d <= 2.5f; d += 0.25f) {
    TEST_ASSERT_FLOAT_WITHIN(0.001f, 63.0f, streamLetterX(d) + streamLetterX(-d));
  }
}

void test_stream_letters_wrap_the_alphabet(void) {
  TEST_ASSERT_EQUAL_CHAR('Z', streamLetterAt('A', -1));
  TEST_ASSERT_EQUAL_CHAR('A', streamLetterAt('Z', 1));
  TEST_ASSERT_EQUAL_CHAR('G', streamLetterAt('G', 0));
  TEST_ASSERT_EQUAL_CHAR('I', streamLetterAt('G', 2));
  TEST_ASSERT_EQUAL_CHAR('Z', streamLetterAt('C', -3));
}

void test_center_glyph_is_big_rest_small(void) {
  TEST_ASSERT_EQUAL_UINT8(2, streamGlyphSize(0.0f));
  TEST_ASSERT_EQUAL_UINT8(2, streamGlyphSize(0.49f));
  TEST_ASSERT_EQUAL_UINT8(1, streamGlyphSize(0.5f));
  TEST_ASSERT_EQUAL_UINT8(1, streamGlyphSize(1.0f));
  TEST_ASSERT_EQUAL_UINT8(1, streamGlyphSize(-2.0f));
}

void test_stream_origins_clamp_off_panel_edges(void) {
  // 1x ink is 5px: origins pin to 0..59, interior untouched.
  TEST_ASSERT_EQUAL_INT16(0, streamClampedX0(-2, 1));  // left edge mid-glide
  TEST_ASSERT_EQUAL_INT16(0, streamClampedX0(-1, 1));  // left edge at rest
  TEST_ASSERT_EQUAL_INT16(0, streamClampedX0(0, 1));
  TEST_ASSERT_EQUAL_INT16(27, streamClampedX0(27, 1));
  TEST_ASSERT_EQUAL_INT16(59, streamClampedX0(59, 1));
  TEST_ASSERT_EQUAL_INT16(59, streamClampedX0(60, 1));  // right edge at rest
  TEST_ASSERT_EQUAL_INT16(59, streamClampedX0(62, 1));  // right edge mid-glide
  // 2x ink is 10px: origins pin to 0..54.
  TEST_ASSERT_EQUAL_INT16(27, streamClampedX0(27, 2));
  TEST_ASSERT_EQUAL_INT16(54, streamClampedX0(60, 2));
  TEST_ASSERT_EQUAL_INT16(0, streamClampedX0(-1, 2));
}

void test_every_drum_position_lands_its_ink_on_the_panel(void) {
  // The clamp is load-bearing: the drum's rest extremes overhang raw.
  TEST_ASSERT_LESS_THAN_INT16(0, (int16_t)(streamLetterX(-2.0f) + 0.5f) - 2);
  TEST_ASSERT_GREATER_THAN_INT16(59, (int16_t)(streamLetterX(2.0f) + 0.5f) - 2);
  // Every letter the drum can show, at rest and at full glide in both
  // directions, keeps its ink fully on the panel once clamped. The origin
  // formula mirrors drawLetterStream's call site exactly.
  for (int8_t step = -2; step <= 2; ++step) {
    for (float glide = -2.0f; glide <= 2.0f; glide += 0.5f) {
      float delta = (float)step + glide;
      float xmid = streamLetterX(delta);
      uint8_t size = streamGlyphSize(delta);
      int16_t x0 = streamClampedX0((int16_t)(xmid + 0.5f) - (size == 2 ? 5 : 2),
                                   size);
      TEST_ASSERT_TRUE(x0 >= 0 && x0 + 5 * size <= 64);
    }
  }
}

void test_spotlight_endpoints(void) {
  TEST_ASSERT_EQUAL_HEX16(ENTRY_CYAN, streamLetterColor(0.0f));
  TEST_ASSERT_EQUAL_HEX16(ENTRY_STREAM_AMBER, streamLetterColor(0.30f));
  TEST_ASSERT_EQUAL_HEX16(ENTRY_STREAM_AMBER, streamLetterColor(-0.30f));
  TEST_ASSERT_EQUAL_HEX16(ENTRY_STREAM_DIM, streamLetterColor(2.0f));
  TEST_ASSERT_EQUAL_HEX16(ENTRY_STREAM_DIM, streamLetterColor(-2.0f));
  TEST_ASSERT_EQUAL_HEX16(ENTRY_STREAM_DIM, streamLetterColor(9.0f));
  for (float d = -2.0f; d <= 2.0f; d += 0.25f) {
    TEST_ASSERT_EQUAL_HEX16(streamLetterColor(d), streamLetterColor(-d));
  }
}

static uint8_t red5(uint16_t c) { return (c >> 11) & 0x1F; }
static uint8_t blue5(uint16_t c) { return c & 0x1F; }

void test_spotlight_midpoints_move_toward_their_targets(void) {
  // Halfway under the pointers: between amber and cyan on every channel.
  uint16_t mid = streamLetterColor(0.15f);
  TEST_ASSERT_LESS_THAN_UINT8(red5(ENTRY_STREAM_AMBER), red5(mid));
  TEST_ASSERT_GREATER_THAN_UINT8(blue5(ENTRY_STREAM_AMBER), blue5(mid));
  // One step out: between amber and dim on red.
  uint16_t out = streamLetterColor(1.0f);
  TEST_ASSERT_LESS_THAN_UINT8(red5(ENTRY_STREAM_AMBER), red5(out));
  TEST_ASSERT_GREATER_THAN_UINT8(red5(ENTRY_STREAM_DIM), red5(out));
}

void test_pointers_widen_from_a_2px_tip(void) {
  int16_t x0;
  uint8_t w;
  TEST_ASSERT_TRUE(streamPointerRow(true, 11, &x0, &w));
  TEST_ASSERT_EQUAL_INT16(31, x0);
  TEST_ASSERT_EQUAL_UINT8(2, w);
  TEST_ASSERT_TRUE(streamPointerRow(true, 7, &x0, &w));
  TEST_ASSERT_EQUAL_INT16(27, x0);
  TEST_ASSERT_EQUAL_UINT8(10, w);
  TEST_ASSERT_FALSE(streamPointerRow(true, 6, &x0, &w));
  TEST_ASSERT_FALSE(streamPointerRow(true, 12, &x0, &w));
  TEST_ASSERT_TRUE(streamPointerRow(false, 52, &x0, &w));
  TEST_ASSERT_EQUAL_INT16(31, x0);
  TEST_ASSERT_EQUAL_UINT8(2, w);
  TEST_ASSERT_TRUE(streamPointerRow(false, 56, &x0, &w));
  TEST_ASSERT_EQUAL_INT16(27, x0);
  TEST_ASSERT_EQUAL_UINT8(10, w);
  TEST_ASSERT_FALSE(streamPointerRow(false, 51, &x0, &w));
  TEST_ASSERT_FALSE(streamPointerRow(false, 57, &x0, &w));
}

void test_ok_border_is_a_2px_chamfered_frame(void) {
  // Chamfered corners are cut.
  TEST_ASSERT_FALSE(okBoxBorderPixel(5, 12));
  TEST_ASSERT_FALSE(okBoxBorderPixel(6, 13));
  TEST_ASSERT_FALSE(okBoxBorderPixel(58, 51));
  // ... but the frame resumes right past them.
  TEST_ASSERT_TRUE(okBoxBorderPixel(8, 12));
  TEST_ASSERT_TRUE(okBoxBorderPixel(7, 13));
  // Top edge: 2px of frame, then the hollow middle.
  TEST_ASSERT_TRUE(okBoxBorderPixel(32, 12));
  TEST_ASSERT_TRUE(okBoxBorderPixel(32, 13));
  TEST_ASSERT_FALSE(okBoxBorderPixel(32, 14));
  TEST_ASSERT_FALSE(okBoxBorderPixel(32, 31));
  // Left edge: the same 2px.
  TEST_ASSERT_TRUE(okBoxBorderPixel(5, 30));
  TEST_ASSERT_TRUE(okBoxBorderPixel(6, 30));
  TEST_ASSERT_FALSE(okBoxBorderPixel(7, 30));
  // Outside the box is outside.
  TEST_ASSERT_FALSE(okBoxBorderPixel(4, 12));
  TEST_ASSERT_FALSE(okBoxBorderPixel(59, 30));
  TEST_ASSERT_FALSE(okBoxBorderPixel(32, 52));
}

void test_left_arrow_shaft_and_head(void) {
  // Shaft 6..18 x 27..36.
  TEST_ASSERT_TRUE(entryArrowLeftPixel(6, 27));
  TEST_ASSERT_TRUE(entryArrowLeftPixel(18, 36));
  TEST_ASSERT_FALSE(entryArrowLeftPixel(5, 30));
  TEST_ASSERT_FALSE(entryArrowLeftPixel(18, 37));
  // Head base column spans 17..46.
  TEST_ASSERT_TRUE(entryArrowLeftPixel(19, 17));
  TEST_ASSERT_TRUE(entryArrowLeftPixel(19, 46));
  TEST_ASSERT_FALSE(entryArrowLeftPixel(19, 16));
  TEST_ASSERT_FALSE(entryArrowLeftPixel(19, 47));
  // Tip column is the even center: rows 31..32.
  TEST_ASSERT_TRUE(entryArrowLeftPixel(28, 31));
  TEST_ASSERT_TRUE(entryArrowLeftPixel(28, 32));
  TEST_ASSERT_FALSE(entryArrowLeftPixel(28, 30));
  TEST_ASSERT_FALSE(entryArrowLeftPixel(29, 31));
}

void test_arrows_mirror_each_other(void) {
  int left_count = 0;
  int right_count = 0;
  for (int16_t x = 0; x < 64; ++x) {
    for (int16_t y = 0; y < 64; ++y) {
      bool left = entryArrowLeftPixel(x, y);
      // Mirror images across the panel middle...
      TEST_ASSERT_EQUAL(left, entryArrowRightPixel(63 - x, y));
      // ... and each vertically symmetric about 31.5.
      TEST_ASSERT_EQUAL(left, entryArrowLeftPixel(x, 63 - y));
      TEST_ASSERT_EQUAL(entryArrowRightPixel(x, y),
                        entryArrowRightPixel(x, 63 - y));
      if (left) ++left_count;
      if (entryArrowRightPixel(x, y)) ++right_count;
    }
  }
  TEST_ASSERT_EQUAL(left_count, right_count);
  // Shaft (130px) plus a ~150px head: a count outside this range is a
  // different shape, not a rounding choice.
  TEST_ASSERT_GREATER_THAN_INT(200, left_count);
  TEST_ASSERT_LESS_THAN_INT(360, left_count);
}

void test_heads_point_inward_with_a_gap_between(void) {
  // Nothing of either arrow between the tips (28 and 35).
  for (int16_t x = 29; x <= 34; ++x) {
    for (int16_t y = 17; y <= 46; ++y) {
      TEST_ASSERT_FALSE(entryArrowLeftPixel(x, y));
      TEST_ASSERT_FALSE(entryArrowRightPixel(x, y));
    }
  }
  // Tips face each other: left tip right of its base, right tip left of its.
  TEST_ASSERT_TRUE(entryArrowLeftPixel(28, 31));
  TEST_ASSERT_TRUE(entryArrowRightPixel(35, 31));
}

int main(int argc, char** argv) {
  UNITY_BEGIN();
  RUN_TEST(test_palette_matches_the_spec_hex);
  RUN_TEST(test_ui_mode_parses_all_five_modes);
  RUN_TEST(test_ui_mode_rejects_the_unknown);
  RUN_TEST(test_name_entry_state_parses_typing_and_submit);
  RUN_TEST(test_name_entry_state_rejects_and_leaves_state_untouched);
  RUN_TEST(test_stream_state_parses_and_rejects);
  RUN_TEST(test_arrow_state_parses_and_rejects);
  RUN_TEST(test_header_and_prompt_center_on_the_panel);
  RUN_TEST(test_slots_center_on_10_31_and_52);
  RUN_TEST(test_active_slot_is_cyan_rest_amber);
  RUN_TEST(test_chevron_grows_from_a_2px_tip);
  RUN_TEST(test_submit_glyphs_cover_submit_only);
  RUN_TEST(test_cylinder_centers_and_clamps);
  RUN_TEST(test_stream_letters_wrap_the_alphabet);
  RUN_TEST(test_center_glyph_is_big_rest_small);
  RUN_TEST(test_stream_origins_clamp_off_panel_edges);
  RUN_TEST(test_every_drum_position_lands_its_ink_on_the_panel);
  RUN_TEST(test_spotlight_endpoints);
  RUN_TEST(test_spotlight_midpoints_move_toward_their_targets);
  RUN_TEST(test_pointers_widen_from_a_2px_tip);
  RUN_TEST(test_ok_border_is_a_2px_chamfered_frame);
  RUN_TEST(test_left_arrow_shaft_and_head);
  RUN_TEST(test_arrows_mirror_each_other);
  RUN_TEST(test_heads_point_inward_with_a_gap_between);
  return UNITY_END();
}
