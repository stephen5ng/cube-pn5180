#pragma once

#include <stddef.h>
#include <stdint.h>

// Which neighbour sensor a cube carries. The two boards share the PN5180
// connector, so exactly one is present and the cube works out which.
enum SensorMode {
  SENSOR_MODE_UNKNOWN = 0,
  SENSOR_MODE_NFC = 1,
  SENSOR_MODE_MAGNETS = 2,
};

// Stage 1. Bits 0..3 are P1..P4 (GPIO 32/17/23/18) read under INPUT_PULLDOWN,
// set when the line read HIGH. The hall board pulls every ID line up through
// 10k, which beats the ~45k internal pulldown; a PN5180 leaves all four
// floating, so the pulldown wins and nothing reads high.
//
// One high line is enough. The encoding is 2-of-6, so at most two sensors are
// ever active and at least two of P1-P4 stay high whatever is docked.
inline bool hallBoardPresent(uint8_t driven_high_mask) {
  return (driven_high_mask & 0x0F) != 0;
}

// The P1-P4 pulldown reading produced by an id_mask, for tests: a magnet pulls
// its sensor low, every other line stays pulled up.
inline uint8_t hallDrivenHighMask(uint8_t active_mask) {
  return (uint8_t)(~active_mask) & 0x0F;
}

// Measured on slot 1, 2026-08-10.
static constexpr uint8_t PN5180_PRODUCT_VERSION_0 = 0x00;
static constexpr uint8_t PN5180_PRODUCT_VERSION_1 = 0x04;

// Stage 2, reached only when stage 1 found no hall board.
//
// readEEprom's return value is deliberately not a parameter. An empty
// connector returns success for every read: floating BUSY satisfies both
// busy-wait edges and floating MISO clocks in zeros. Only the bytes carry
// information.
//
// The version alone puts one byte between a live reader and a floating bus, so
// the die identifier is required too: 16 per-chip bytes that read as all zeros
// with nothing attached.
inline bool pn5180ReaderPresent(const uint8_t* version, const uint8_t* die,
                                size_t die_len) {
  if (version[0] != PN5180_PRODUCT_VERSION_0 ||
      version[1] != PN5180_PRODUCT_VERSION_1) {
    return false;
  }
  uint8_t any_set = 0x00;
  uint8_t all_set = 0xFF;
  for (size_t i = 0; i < die_len; i++) {
    any_set |= die[i];
    all_set &= die[i];
  }
  return any_set != 0x00 && all_set != 0xFF;
}

// Stage 2 drives NSS/RST/MOSI/SCK, which are open-drain sensors on the hall
// board, so it must be unreachable whenever one is fitted. Named and tested
// rather than left implicit in the order of statements at the call site.
inline bool shouldRunActiveProbe(uint8_t driven_high_mask) {
  return !hallBoardPresent(driven_high_mask);
}
