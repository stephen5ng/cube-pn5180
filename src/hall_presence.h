#pragma once

#include <stdint.h>

// Presence detection for the DRV5055 analog hall sensor. Pure integer logic, no Arduino
// dependencies, so it unit-tests natively.
//
// Compares against a tracked baseline rather than a fixed midpoint, because the sensor
// centre is ratiometric to +3V3 while the ESP32 ADC references its own bandgap: rail
// movement shows up directly as a reading shift and is the same order as the signal.
struct HallPresenceConfig {
  int8_t   direction;         // +1 if the presence magnet drives the reading up
  int16_t  on_delta;          // counts from baseline to assert
  int16_t  off_delta;         // counts to release
  uint8_t  fast_shift;        // IIR shift on the raw reading
  uint8_t  base_shift;        // IIR shift on the baseline
  uint16_t base_interval_ms;  // how often the baseline may adapt
};

class HallPresenceTracker {
 public:
  void begin(const HallPresenceConfig& cfg) {
    cfg_ = cfg;
    primed_ = false;
    active_ = false;
    delta_ = 0;
  }

  bool update(int raw, uint32_t now_ms) {
    if (!primed_) {
      fast_ = (int32_t)raw << cfg_.fast_shift;
      base_ = (int32_t)raw << cfg_.base_shift;
      last_base_ms_ = now_ms;
      primed_ = true;
    } else {
      fast_ += raw - (fast_ >> cfg_.fast_shift);
    }

    const int f = (int)(fast_ >> cfg_.fast_shift);
    delta_ = cfg_.direction * (f - baseline());

    if (!active_) {
      if (delta_ >= cfg_.on_delta) active_ = true;
    } else {
      if (delta_ < cfg_.off_delta) active_ = false;
    }

    // Frozen while a neighbour is present: otherwise the baseline creeps up to the
    // magnet and the cube forgets the neighbour is there.
    if (!active_ && (uint32_t)(now_ms - last_base_ms_) >= cfg_.base_interval_ms) {
      base_ += f - (base_ >> cfg_.base_shift);
      last_base_ms_ = now_ms;
    }
    return active_;
  }

  bool active()   const { return active_; }
  int  baseline() const { return (int)(base_ >> cfg_.base_shift); }
  int  filtered() const { return (int)(fast_ >> cfg_.fast_shift); }
  int  delta()    const { return delta_; }  // signed by direction; proximity measure

 private:
  HallPresenceConfig cfg_ {};
  int32_t  fast_ = 0;
  int32_t  base_ = 0;
  uint32_t last_base_ms_ = 0;
  int      delta_ = 0;
  bool     active_ = false;
  bool     primed_ = false;
};
