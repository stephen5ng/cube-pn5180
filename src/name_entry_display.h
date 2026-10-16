#pragma once
#include <math.h>
#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

// Three-cube high score name entry: geometry, palette, and wire parsing.
//
// Pure: no Arduino, no panel, no millis. DisplayManager (main.cpp) renders
// from these; the native suite pins every number the approved assets in
// docs/assets/high-score-entry/ fix. Angles are in float; the panel is 64x64.

// ---- palette (docs/high-score-three-cube-entry-spec.md section 8) --------

#define ENTRY_RGB565(r, g, b) \
  (uint16_t)((((r) & 0xF8) << 8) | (((g) & 0xFC) << 3) | ((b) >> 3))

static const uint16_t ENTRY_YELLOW = ENTRY_RGB565(255, 230, 0);    // 0xFF20
static const uint16_t ENTRY_WHITE = ENTRY_RGB565(245, 245, 250);   // 0xF7BF
static const uint16_t ENTRY_CYAN = ENTRY_RGB565(0, 255, 255);      // 0x07FF
static const uint16_t ENTRY_AMBER = ENTRY_RGB565(255, 186, 99);    // 0xFDCC
static const uint16_t ENTRY_STREAM_AMBER = ENTRY_RGB565(210, 130, 40);  // 0xD405
static const uint16_t ENTRY_STREAM_DIM = ENTRY_RGB565(85, 50, 18);      // 0x5182
static const uint16_t ENTRY_DARK_SLATE = ENTRY_RGB565(42, 48, 56);      // 0x2987
static const uint16_t ENTRY_RAIL_DOT = ENTRY_RGB565(30, 45, 50);        // 0x1966
static const uint16_t ENTRY_TIME_GREEN = ENTRY_RGB565(0, 255, 0);       // 0x07E0
static const uint16_t ENTRY_TIME_RED = ENTRY_RGB565(255, 0, 0);         // 0xF800

// ---- UI modes and state (MQTT section 5.2) --------------------------------

typedef enum {
  UI_MODE_GAME_NORMAL = 0,
  UI_MODE_NAME_ENTRY,
  UI_MODE_LETTER_STREAM,
  UI_MODE_OK_BOX,
  UI_MODE_ARROW_BIDIRECTIONAL
} UiMode;

typedef enum {
  STREAM_STOP = 0,
  STREAM_FORWARD,
  STREAM_BACKWARD
} StreamDirection;

typedef enum {
  STREAM_SLOW = 0,
  STREAM_FAST
} StreamSpeed;

typedef enum {
  ARROW_NONE = 0,
  ARROW_LEFT,
  ARROW_RIGHT
} ArrowDirection;

typedef struct {
  char initials[4];  // three A-Z plus NUL
  int8_t active_slot;  // 0..2, or -1 while SUBMIT is focused
  bool submit_focused;
  int8_t time_pct;  // 0..100 unspent, or -1 when the server omits the bar
} NameEntryState;

typedef struct {
  char center_letter;  // A-Z
  StreamDirection direction;
  StreamSpeed speed;
} StreamState;

typedef struct {
  bool active;
  ArrowDirection direction;
} ArrowState;

// ---- tiny JSON readers (no ArduinoJson on this firmware) ------------------
//
// Fixed-shape payloads only: find "key", skip to ':', read one value.
// Anything unexpected is false and the caller's state is untouched.

// Reads "key" : "value" into out (NUL-terminated, truncated to cap).
inline bool entryJsonQuoted(const char* json, const char* key,
                            char* out, size_t cap) {
  char pattern[32];
  pattern[0] = '"';
  size_t i = 0;
  while (key[i] && i + 3 < sizeof(pattern)) {
    pattern[1 + i] = key[i];
    ++i;
  }
  pattern[1 + i] = '"';
  pattern[2 + i] = '\0';
  const char* at = strstr(json, pattern);
  if (!at) return false;
  at += strlen(pattern);
  while (*at == ' ' || *at == '\t') ++at;
  if (*at != ':') return false;
  ++at;
  while (*at == ' ' || *at == '\t') ++at;
  if (*at != '"') return false;
  ++at;
  size_t n = 0;
  while (*at && *at != '"' && n + 1 < cap) out[n++] = *at++;
  out[n] = '\0';
  return *at == '"';
}

// Positions at the unquoted value of "key" : value. False when absent.
inline bool entryJsonValue(const char* json, const char* key, const char** value) {
  char pattern[32];
  pattern[0] = '"';
  size_t i = 0;
  while (key[i] && i + 3 < sizeof(pattern)) {
    pattern[1 + i] = key[i];
    ++i;
  }
  pattern[1 + i] = '"';
  pattern[2 + i] = '\0';
  const char* at = strstr(json, pattern);
  if (!at) return false;
  at += strlen(pattern);
  while (*at == ' ' || *at == '\t') ++at;
  if (*at != ':') return false;
  ++at;
  while (*at == ' ' || *at == '\t') ++at;
  *value = at;
  return true;
}

inline bool parseUiMode(const char* json, UiMode* out) {
  char value[24];
  if (!entryJsonQuoted(json, "mode", value, sizeof(value))) return false;
  if (!strcmp(value, "GAME_NORMAL")) *out = UI_MODE_GAME_NORMAL;
  else if (!strcmp(value, "NAME_ENTRY")) *out = UI_MODE_NAME_ENTRY;
  else if (!strcmp(value, "LETTER_STREAM")) *out = UI_MODE_LETTER_STREAM;
  else if (!strcmp(value, "OK_BOX")) *out = UI_MODE_OK_BOX;
  else if (!strcmp(value, "ARROW_BIDIRECTIONAL")) *out = UI_MODE_ARROW_BIDIRECTIONAL;
  else return false;
  return true;
}

inline bool parseNameEntryState(const char* json, NameEntryState* out) {
  // Parsed into locals and committed once, so a half-valid payload leaves
  // the live state untouched.
  char initials[8];
  const char* slot_text;
  const char* focused_text;
  if (!entryJsonQuoted(json, "initials", initials, sizeof(initials))) return false;
  if (!entryJsonValue(json, "active_slot", &slot_text)) return false;
  if (!entryJsonValue(json, "submit_focused", &focused_text)) return false;
  if (strlen(initials) != 3) return false;
  for (uint8_t i = 0; i < 3; ++i) {
    if (initials[i] < 'A' || initials[i] > 'Z') return false;
  }
  // -1 is SUBMIT; strtol reads the sign, atoi would too, but neither says
  // where the number ends -- the NUL/','/'}' check is what rejects "2x".
  char* end = NULL;
  long slot = strtol(slot_text, &end, 10);
  if (end == slot_text || (*end != ',' && *end != '}' && *end != ' ' && *end != '\0')) {
    return false;
  }
  if (slot < -1 || slot > 2) return false;
  bool focused;
  if (!strncmp(focused_text, "true", 4)) focused = true;
  else if (!strncmp(focused_text, "false", 5)) focused = false;
  else return false;
  // The bar is optional: old servers omit it and the screen simply shows no
  // bar. Present but out of range is a corrupt payload like any other.
  int8_t pct = -1;
  const char* pct_text;
  if (entryJsonValue(json, "time_left_pct", &pct_text)) {
    char* pct_end = NULL;
    long pct_long = strtol(pct_text, &pct_end, 10);
    if (pct_end == pct_text ||
        (*pct_end != ',' && *pct_end != '}' && *pct_end != ' ' && *pct_end != '\0')) {
      return false;
    }
    if (pct_long < 0 || pct_long > 100) return false;
    pct = (int8_t)pct_long;
  }
  for (uint8_t i = 0; i < 3; ++i) out->initials[i] = initials[i];
  out->initials[3] = '\0';
  out->active_slot = (int8_t)slot;
  out->submit_focused = focused;
  out->time_pct = pct;
  return true;
}

inline bool parseStreamState(const char* json, StreamState* out) {
  char center[8];
  char direction[16];
  char speed[8];
  if (!entryJsonQuoted(json, "center_letter", center, sizeof(center))) return false;
  if (!entryJsonQuoted(json, "direction", direction, sizeof(direction))) return false;
  if (!entryJsonQuoted(json, "speed", speed, sizeof(speed))) return false;
  if (strlen(center) != 1 || center[0] < 'A' || center[0] > 'Z') return false;
  StreamDirection dir;
  if (!strcmp(direction, "STOP")) dir = STREAM_STOP;
  else if (!strcmp(direction, "FORWARD")) dir = STREAM_FORWARD;
  else if (!strcmp(direction, "BACKWARD")) dir = STREAM_BACKWARD;
  else return false;
  StreamSpeed spd;
  if (!strcmp(speed, "SLOW")) spd = STREAM_SLOW;
  else if (!strcmp(speed, "FAST")) spd = STREAM_FAST;
  else return false;
  out->center_letter = center[0];
  out->direction = dir;
  out->speed = spd;
  return true;
}

inline bool parseArrowState(const char* json, ArrowState* out) {
  const char* active_text;
  char direction[8];
  if (!entryJsonValue(json, "active", &active_text)) return false;
  if (!entryJsonQuoted(json, "active_direction", direction, sizeof(direction))) {
    return false;
  }
  bool active;
  if (!strncmp(active_text, "true", 4)) active = true;
  else if (!strncmp(active_text, "false", 5)) active = false;
  else return false;
  ArrowDirection dir;
  if (!strcmp(direction, "NONE")) dir = ARROW_NONE;
  else if (!strcmp(direction, "LEFT")) dir = ARROW_LEFT;
  else if (!strcmp(direction, "RIGHT")) dir = ARROW_RIGHT;
  else return false;
  out->active = active;
  out->direction = dir;
  return true;
}

// ---- name display (Main cube, section 4.1) --------------------------------

#define ENTRY_HEADER_Y 4
#define ENTRY_PROMPT_Y 15
#define ENTRY_SLOT_Y 31        // 2x glyphs: rows 31..44
#define ENTRY_SLOT_SIZE 2
#define ENTRY_CHEVRON_ABOVE_Y 26  // 4 rows, tip (2px) at row 29
#define ENTRY_CHEVRON_BELOW_Y 47  // 4 rows, tip (2px) at row 47
#define ENTRY_UNDERLINE_Y 49      // 2 rows, inactive slots only
#define ENTRY_SUBMIT_X 17
#define ENTRY_SUBMIT_Y 54
#define ENTRY_SUBMIT_W 30  // x = 17..46
#define ENTRY_SUBMIT_H 9   // y = 54..62

// Built-in font: 5x7 ink on a 6x8 advance. A run of n glyphs is n*6-1 wide;
// centering the ink (not the advance) is what keeps odd lengths honest.
inline int16_t entryCenteredX(uint8_t glyphs) {
  return (int16_t)((64 - (glyphs * 6 - 1)) / 2);
}

// Slot ink (10px at 2x) centered on the slot center.
inline int16_t entrySlotCenterX(uint8_t slot) {
  static const int16_t centers[3] = {10, 31, 52};
  if (slot > 2) slot = 2;
  return centers[slot];
}

inline int16_t entrySlotTextX(uint8_t slot) {
  return entrySlotCenterX(slot) - 5;
}

inline uint16_t entrySlotColor(uint8_t slot, int8_t active_slot) {
  return slot == active_slot ? ENTRY_CYAN : ENTRY_AMBER;
}

// Chevron half-width, counted from the 2px tip toward the 8px mouth.
inline uint8_t entryChevronHalfWidth(uint8_t row_from_tip) {
  return row_from_tip + 1;
}

// SUBMIT in a compact 3x5 face: 6 glyphs x (3px + 1px gap) = 24px, centered
// in the 30px button at x = 20, y = 56. Rows are 3-bit top-to-bottom.
static const uint8_t ENTRY_SUBMIT_GLYPH_W = 3;
static const int16_t ENTRY_SUBMIT_TEXT_X = 20;
static const int16_t ENTRY_SUBMIT_TEXT_Y = 56;

inline const uint8_t* entrySubmitGlyph(char c) {
  static const uint8_t glyph_s[5] = {0x7, 0x4, 0x7, 0x1, 0x7};
  static const uint8_t glyph_u[5] = {0x5, 0x5, 0x5, 0x5, 0x7};
  static const uint8_t glyph_b[5] = {0x6, 0x5, 0x6, 0x5, 0x6};
  static const uint8_t glyph_m[5] = {0x5, 0x7, 0x7, 0x5, 0x5};
  static const uint8_t glyph_i[5] = {0x7, 0x2, 0x2, 0x2, 0x7};
  static const uint8_t glyph_t[5] = {0x7, 0x2, 0x2, 0x2, 0x2};
  switch (c) {
    case 'S': return glyph_s;
    case 'U': return glyph_u;
    case 'B': return glyph_b;
    case 'M': return glyph_m;
    case 'I': return glyph_i;
    case 'T': return glyph_t;
    default: return NULL;
  }
}

// ---- time bar (Main cube top) ----------------------------------------------
//
// Five-minute limit as a 2px bar along y = 0..1, anchored left, shrinking
// rightward. Green past 20%, yellow to 10%, red at and below it.
#define ENTRY_TIME_BAR_X 0
#define ENTRY_TIME_BAR_Y 0
#define ENTRY_TIME_BAR_H 2

inline int16_t entryTimeBarWidth(uint8_t pct) {
  if (pct > 100) pct = 100;
  return (int16_t)(pct * 64 + 50) / 100;
}

inline uint16_t entryTimeBarColor(uint8_t pct) {
  if (pct <= 10) return ENTRY_TIME_RED;
  if (pct <= 20) return ENTRY_YELLOW;
  return ENTRY_TIME_GREEN;
}

// ---- letter stream (Stream cube, section 4.2) -----------------------------

#define ENTRY_PTR_COLS 2  // tip columns [31, 32] across x = 31.5
#define ENTRY_PTR_TOP_Y0 7
#define ENTRY_PTR_TOP_TIP 11
#define ENTRY_PTR_BOT_TIP 52
#define ENTRY_PTR_BOT_Y1 56
#define ENTRY_RAIL_TOP_Y 2
#define ENTRY_RAIL_BOT_Y 61

// One pointer row: false outside rows 7..11 (top) / 52..56 (bottom),
// otherwise the x span, widening from the 2px tip to a 10px mouth.
inline bool streamPointerRow(bool top, uint8_t row, int16_t* x0, uint8_t* w) {
  uint8_t from_tip;
  if (top) {
    if (row < ENTRY_PTR_TOP_Y0 || row > ENTRY_PTR_TOP_TIP) return false;
    from_tip = ENTRY_PTR_TOP_TIP - row;
  } else {
    if (row < ENTRY_PTR_BOT_TIP || row > ENTRY_PTR_BOT_Y1) return false;
    from_tip = row - ENTRY_PTR_BOT_TIP;
  }
  *w = (uint8_t)(from_tip * 2 + ENTRY_PTR_COLS);
  *x0 = (int16_t)(32 - *w / 2);
  return true;
}

// Cylinder projection: the letter delta steps from the center maps onto the
// panel through a sine, so the edges compress like a turning drum.
inline float streamLetterX(float delta) {
  float u = delta / 2.5f;
  if (u > 1.0f) u = 1.0f;
  else if (u < -1.0f) u = -1.0f;
  return 31.5f + 32.0f * sinf(u * (3.14159265f / 2.0f));
}

// The letter `offset` steps from the center, wrapping A-Z.
inline char streamLetterAt(char center, int8_t offset) {
  int idx = (center - 'A' + offset) % 26;
  if (idx < 0) idx += 26;
  return (char)('A' + idx);
}

// Center glyph reads at 2x; everything else travels at 1x.
inline uint8_t streamGlyphSize(float delta) {
  float mag = delta < 0 ? -delta : delta;
  return mag < 0.5f ? 2 : 1;
}

// Pin a stream letter's text origin so its 5px ink lands fully on the panel.
// The drum's rest extremes overhang (x0 = -1 / 60) and the glide pushes to
// -2 / 62, while the HUB75 driver draws unclipped -- off-panel ink aliases
// into visible pixels instead of vanishing.
inline int16_t streamClampedX0(int16_t x0, uint8_t text_size) {
  int16_t max_x0 = (int16_t)(64 - 5 * text_size);
  if (x0 < 0) return 0;
  if (x0 > max_x0) return max_x0;
  return x0;
}

// 565 channel mix in 8-bit room, so the midpoints land between the endpoints
// instead of on whatever the 5/6-bit truncation felt like.
inline uint16_t lerp565(uint16_t a, uint16_t b, float t) {
  if (t < 0.0f) t = 0.0f;
  else if (t > 1.0f) t = 1.0f;
  uint8_t ar = (a >> 11) & 0x1F, ag = (a >> 5) & 0x3F, ab = a & 0x1F;
  uint8_t br = (b >> 11) & 0x1F, bg = (b >> 5) & 0x3F, bb = b & 0x1F;
  uint8_t r8 = (uint8_t)(((ar << 3) | (ar >> 2)) * (1.0f - t) +
                         (((br << 3) | (br >> 2))) * t + 0.5f);
  uint8_t g8 = (uint8_t)(((ag << 2) | (ag >> 4)) * (1.0f - t) +
                         (((bg << 2) | (bg >> 4))) * t + 0.5f);
  uint8_t b8 = (uint8_t)(((ab << 3) | (ab >> 2)) * (1.0f - t) +
                         (((bb << 3) | (bb >> 2))) * t + 0.5f);
  return ENTRY_RGB565(r8, g8, b8);
}

// Spotlight curve: Hermite smoothstep from amber to full cyan under the
// pointers (|d| < 0.30), then a linear fade from amber to dim at the edges.
inline uint16_t streamLetterColor(float delta) {
  float mag = delta < 0 ? -delta : delta;
  if (mag < 0.30f) {
    float u = 1.0f - mag / 0.30f;
    float s = u * u * (3.0f - 2.0f * u);
    return lerp565(ENTRY_STREAM_AMBER, ENTRY_CYAN, s);
  }
  return lerp565(ENTRY_STREAM_AMBER, ENTRY_STREAM_DIM, (mag - 0.30f) / 1.70f);
}

// ---- OK box (Stream cube, submit phase, section 4.3) ----------------------

#define ENTRY_OK_X0 5
#define ENTRY_OK_X1 58
#define ENTRY_OK_Y0 12
#define ENTRY_OK_Y1 51
#define ENTRY_OK_TEXT_X 10  // 4x "OK": O at 10..29, K at 34..53
#define ENTRY_OK_TEXT_Y 18
#define ENTRY_OK_TEXT_SIZE 4

// 2px chamfered border: inside the 3px-cut outer rect, outside its 2px inset.
inline bool okBoxBorderPixel(int16_t x, int16_t y) {
  int16_t cx = x - ENTRY_OK_X0;
  int16_t ex = ENTRY_OK_X1 - x;
  int16_t cy = y - ENTRY_OK_Y0;
  int16_t ey = ENTRY_OK_Y1 - y;
  if (cx < 0 || ex < 0 || cy < 0 || ey < 0) return false;
  int16_t mx = cx < ex ? cx : ex;
  int16_t my = cy < ey ? cy : ey;
  if (mx < 3 && my < 3 && mx + my < 3) return false;  // chamfered corner
  if (mx < 2 || my < 2) return true;  // within 2 of the outer edge
  // The inset keeps the same chamfer, so the border reads a constant 2px
  // around the corners instead of pinching.
  int16_t ix = mx - 2;
  int16_t iy = my - 2;
  return ix < 3 && iy < 3 && ix + iy < 3;
}

// ---- bidirectional arrows (Arrow cube, section 4.4) -----------------------

#define ENTRY_ARROW_Y0 17  // head spans rows 17..46, center 31.5
#define ENTRY_ARROW_Y1 46
#define ENTRY_ARROW_SHAFT_Y0 27  // shaft rows 27..36
#define ENTRY_ARROW_SHAFT_Y1 36
#define ENTRY_ARROW_LEFT_X0 6    // shaft 6..18, head base 19, tip 28
#define ENTRY_ARROW_LEFT_TIP 28
#define ENTRY_ARROW_RIGHT_TIP 35  // tip 35, head base 44, shaft 45..57
#define ENTRY_ARROW_RIGHT_X1 57

// Left head: |2y - 63| <= 2 + 3*(28 - x) over x = 19..28. Base row (x = 19)
// spans 17..46; the tip column (x = 28) is rows 31..32, the honest center of
// an even-height head.
inline bool entryArrowLeftPixel(int16_t x, int16_t y) {
  if (x >= 6 && x <= 18 && y >= ENTRY_ARROW_SHAFT_Y0 && y <= ENTRY_ARROW_SHAFT_Y1) {
    return true;
  }
  if (x < 19 || x > ENTRY_ARROW_LEFT_TIP) return false;
  int16_t dy2 = 2 * y - 63;
  if (dy2 < 0) dy2 = -dy2;
  return dy2 <= 2 + 3 * (ENTRY_ARROW_LEFT_TIP - x);
}

// Right head mirrored: |2y - 63| <= 2 + 3*(x - 35) over x = 35..44.
inline bool entryArrowRightPixel(int16_t x, int16_t y) {
  if (x >= 45 && x <= ENTRY_ARROW_RIGHT_X1 &&
      y >= ENTRY_ARROW_SHAFT_Y0 && y <= ENTRY_ARROW_SHAFT_Y1) {
    return true;
  }
  if (x < ENTRY_ARROW_RIGHT_TIP || x > 44) return false;
  int16_t dy2 = 2 * y - 63;
  if (dy2 < 0) dy2 = -dy2;
  return dy2 <= 2 + 3 * (x - ENTRY_ARROW_RIGHT_TIP);
}
