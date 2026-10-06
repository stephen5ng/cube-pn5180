#pragma once
#include <stdint.h>

// A letter waking from a sleeping pill starts like a fluorescent tube, from
// `cube/{id}/wake`: a faint starter glow, a strike that fails, another glow,
// then on with one last dip. The steps never ramp -- a tube strikes or it
// does not -- and never go darker than the sleeping colour.
//
// THE SAME KEYFRAMES AS THE SCREEN'S RACK, cubes `src/rendering/wake_flicker.py`,
// so the cube and the screen flicker together. Change both or neither.
//
// One message, not one per step: the server sends the sleeping colour once
// and the cube plays the table. Its retained `letter_color` still carries the
// awake colour, so a lost wake only loses the flicker, never the letter.

struct WakeStep {
  uint16_t at_ms;
  uint8_t percent;  // 0 = the sleeping colour, 100 = the resting colour
};

static const WakeStep WAKE_FLICKER[] = {
  {0, 0}, {100, 25}, {170, 0}, {300, 100}, {340, 0},
  {470, 30}, {520, 0}, {620, 100}, {660, 20}, {720, 100},
};

static const uint16_t WAKE_FLICKER_MS = 720;

// Percent awake `elapsed_ms` into a wake: steps, held until the next.
inline uint8_t wakePercent(unsigned long elapsed_ms) {
  uint8_t percent = 100;
  for (const WakeStep& step : WAKE_FLICKER) {
    if (elapsed_ms < step.at_ms) break;
    percent = step.percent;
  }
  return percent;
}

// `percent` of the way from `asleep` to `awake`, channel by channel in RGB565.
inline uint16_t blendRgb565(uint16_t asleep, uint16_t awake, uint8_t percent) {
  auto mix = [percent](int from, int to) {
    return from + (to - from) * percent / 100;
  };
  int r = mix(asleep >> 11, awake >> 11);
  int g = mix((asleep >> 5) & 0x3F, (awake >> 5) & 0x3F);
  int b = mix(asleep & 0x1F, awake & 0x1F);
  return (uint16_t)((r << 11) | (g << 5) | b);
}

// The letter's colour `elapsed_ms` into a wake from `asleep` to `awake`.
inline uint16_t wakeFlickerColor(unsigned long elapsed_ms, uint16_t asleep,
                                 uint16_t awake) {
  return blendRgb565(asleep, awake, wakePercent(elapsed_ms));
}
