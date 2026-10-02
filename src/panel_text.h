#pragma once
#include <stdint.h>

// Where the words of a multi-glyph panel message sit while it descends.
//
// One letter is drawn by DisplayManager::drawLetter in the 78pt font and needs
// no layout. Anything longer -- "GAME OVER" -- is drawn a word per line in
// GFX's built-in font, centered as one block, and descends on the same
// percent-complete the letter animation runs on: 0 is entirely above the
// panel, 100 is centered, 200 is entirely below it.

// GFX's built-in font advances 6x8 px per glyph before setTextSize scaling.
// The 6th column and the 8th row are the inter-glyph gap, so a run of n glyphs
// is n*6*size wide with one gap of `size` hanging off its right edge.
#define PANEL_TEXT_GLYPH_W 6
#define PANEL_TEXT_GLYPH_H 8

struct PanelTextLine {
  const char* begin;  // into the caller's string; NOT null-terminated here
  uint8_t length;
  int16_t x;
  int16_t y;  // top row of the glyph cell, which is where GFX's built-in
              // font puts the cursor
};

// Splits `text` on spaces, one word per line, and places the block. Returns
// the number of lines written to `out`; words past `max_lines` are dropped,
// since a message too tall for the panel cannot be shown however it is cut.
inline uint8_t layoutPanelText(const char* text, uint8_t panel_w, uint8_t panel_h,
                               uint8_t text_size, uint16_t vertical_position,
                               PanelTextLine* out, uint8_t max_lines) {
  uint8_t count = 0;
  for (const char* p = text; *p && count < max_lines; ) {
    if (*p == ' ') { ++p; continue; }
    const char* start = p;
    while (*p && *p != ' ') ++p;
    out[count].begin = start;
    out[count].length = (uint8_t)(p - start);
    ++count;
  }
  if (!count) return 0;

  const int16_t line_h = PANEL_TEXT_GLYPH_H * text_size;
  const int16_t block_h = line_h * count;
  const int16_t centered_top = (panel_h - block_h) / 2;
  // Linear in `vertical_position` so the block tracks the letter animation's
  // bounce curve, which is already baked into the percentage.
  const int16_t top = centered_top +
      (int16_t)(((int32_t)vertical_position - 100) * (centered_top + block_h) / 100);

  for (uint8_t i = 0; i < count; ++i) {
    const int16_t width = out[i].length * PANEL_TEXT_GLYPH_W * text_size - text_size;
    out[i].x = (panel_w - width) / 2;
    if (out[i].x < 0) out[i].x = 0;
    out[i].y = top + i * line_h;
  }
  return count;
}
