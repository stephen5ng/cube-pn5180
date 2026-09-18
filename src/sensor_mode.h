#pragma once

#include <stddef.h>
#include <stdint.h>

// Which neighbour sensor a board carries. The two share the PN5180 connector,
// so exactly one is fitted, and which one is chosen when the board is flashed.
enum SensorMode {
  SENSOR_MODE_NFC = 1,
  SENSOR_MODE_MAGNETS = 2,
};
