#pragma once

#include <math.h>
#include <stdint.h>

// Presence detection for the DRV5055 analog hall sensor. No Arduino dependencies, so it
// unit-tests natively.
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

// A baseline is only worth writing to flash when it was taken with nothing
// magnetic nearby, because an NVS seed outlives the mistake: a poisoned RTC
// value costs one wake, a poisoned seed costs every cold boot until something
// overwrites it. An id_mask of 0 is the physical proof -- no ID magnet is in
// range, so no neighbour is docked and its presence magnet cannot be skewing
// the reading, whether or not the tracker has asserted.
//
// The move threshold is what keeps this off the flash: the baseline wanders a
// count or two a second, and NVS erases a sector per write.
static constexpr int PRESENCE_BASELINE_SAVE_DELTA = 16;

inline bool shouldSavePresenceBaseline(uint8_t id_mask, bool active, int baseline,
                                       int stored) {
  if (id_mask != 0 || active) return false;
  const int move = baseline > stored ? baseline - stored : stored - baseline;
  return move > PRESENCE_BASELINE_SAVE_DELTA;
}

// delta is a field strength, which falls off as the cube of distance, so it is a
// badly misleading gauge of how close a neighbour is: the 176 -> 148 drop
// measured while re-seating slot 1 looks like losing a sixth of the signal and
// is under 6% of extra gap. These convert it to a distance, in units where 100
// is the gap at which presence latches on.
//
// Calibration-free by construction: only the ratio to on_delta matters, so no
// bench measurement of a real gap is needed. The cost is that the number is
// relative, not millimetres, and the inverse-cube law holds in the far field --
// across a gap comparable to the magnet the true exponent is smaller, so a
// distance change is somewhat larger than reported.
static constexpr int PRESENCE_DISTANCE_REFERENCE = 100;
static constexpr int PRESENCE_DISTANCE_OUT_OF_RANGE = 999;

inline int hallPresenceDistance(int delta, int on_delta) {
  if (delta < 1) return PRESENCE_DISTANCE_OUT_OF_RANGE;  // at or behind the baseline
  return (int)lroundf(PRESENCE_DISTANCE_REFERENCE *
                      cbrtf((float)on_delta / (float)delta));
}

// 0..100 for driving an animation: 0 is nothing in range, 100 is as close as a
// docked neighbour gets. Rising with proximity so brightness, size or speed can
// use it directly, and clamped at both ends so a consumer never has to know
// about the out-of-range sentinel.
//
// The endpoints are where the number stops being informative rather than where
// the thresholds sit, and both are stated in the delta counts they were
// measured in rather than in derived distance: the far end is the ADC noise
// floor, beyond which nothing is distinguishable from an idle sensor, and the
// near end is a docked reading. Presence latching and releasing both fall
// inside this span, which is deliberate -- most of the visible travel happens
// before a cube is close enough to latch.
static constexpr int PRESENCE_CLOSENESS_NOISE_DELTA = 35;    // measured on slot 1
static constexpr int PRESENCE_CLOSENESS_DOCKED_DELTA = 127;  // measured on slot 1

inline int hallPresenceCloseness(int delta, int on_delta) {
  const int far = hallPresenceDistance(PRESENCE_CLOSENESS_NOISE_DELTA, on_delta);
  const int near_by = hallPresenceDistance(PRESENCE_CLOSENESS_DOCKED_DELTA, on_delta);
  const int distance = hallPresenceDistance(delta, on_delta);
  if (distance >= far) return 0;
  if (distance <= near_by) return 100;
  return 100 * (far - distance) / (far - near_by);
}

// Whether a decoded neighbour ID may be believed, given the ID already seated during
// this presence latch (0 if none yet).
//
// A cube offset against its neighbour shows the ID sensors a real, stable pattern that
// belongs to another cube: slid one column, cube 11's P5+P6 magnets sit over P4+P5,
// which is cube 4, and a one-row offset turns 14 into 15. At Maker Faire 2026-09, 94
// of 105 out-of-set IDs were exactly that "4". Presence does not stop it: the
// hysteresis that keeps a docked cube latched down to off_delta keeps it latched
// through the slide too.
//
// So a *change* to a different ID is only believed from a neighbour that reads docked,
// while everything else passes as before:
//  - no ID, so that "no neighbour" is never held back;
//  - the seated ID coming back after a dropout, so a loosely seated cube that
//    glitches is not stranded at "no neighbour" until someone pushes it in;
//  - the first ID of a latch, because latching already required on_delta.
// Swaps within one latch are rare: 5 direct ID-to-ID changes against 6709 re-links
// through "no neighbour" over the same weekend.
inline uint8_t gateHallNeighborChange(uint8_t id, uint8_t seated_id, int delta,
                                      int docked_delta) {
  if (id == 0 || seated_id == 0 || id == seated_id) return id;
  return delta >= docked_delta ? id : 0;
}

// The neighbour ID a hall cube settles on, one decoded sample at a time.
//
// A dock or undock is reported after debounce_reads identical samples. A change
// from the seated cube to a different one waits settle_reads instead, because
// the strength check reads the tracker's filtered delta, and that lags a slide by
// about 2^fast_shift samples: a cube slid off its neighbour still reads docked
// for the first few milliseconds, long enough for a plain debounce to report the
// alias. settle_reads has to span several of those time constants.
struct HallNeighborDebounce {
  HallNeighborDebounce(uint16_t debounce_reads, uint16_t settle_reads, int docked_delta)
      : debounce_reads(debounce_reads), settle_reads(settle_reads),
        docked_delta(docked_delta) {}

  uint16_t debounce_reads;
  uint16_t settle_reads;
  int docked_delta;
  uint8_t candidate = 0;
  uint16_t count = 0;
  uint8_t seated = 0;  // last ID reported during this presence latch

  // True once `candidate` has held long enough to be reported.
  bool update(uint8_t decoded, bool presence_active, int delta) {
    if (!presence_active) seated = 0;
    const uint8_t id = gateHallNeighborChange(decoded, seated, delta, docked_delta);
    if (id == candidate) {
      if (count < UINT16_MAX) count++;
    } else {
      candidate = id;
      count = 1;
    }
    const bool change = candidate != 0 && seated != 0 && candidate != seated;
    return count >= (change ? settle_reads : debounce_reads);
  }

  // Call once `candidate` has actually been reported.
  void reported() {
    if (candidate != 0) seated = candidate;
  }
};

// The shared-edge animation is an approach hint, not a second representation of
// a confirmed neighbour. A real dock can settle below the calibrated 100% point,
// so the presence latch, rather than closeness alone, ends the preview.
inline bool shouldPreviewHallCandidate(int proximity, bool presence_active) {
  return !presence_active && proximity > 0 && proximity < 100;
}

class HallPresenceTracker {
 public:
  // saved_baseline carries a baseline across a wake. Priming from the first sample
  // is blind to a magnet that is already there: the magnet gets subtracted into the
  // baseline and the neighbour is never seen. 0 means nothing was saved -- a cold
  // boot re-initialises RTC memory -- and the first sample primes as before.
  void begin(const HallPresenceConfig& cfg, int saved_baseline = 0) {
    cfg_ = cfg;
    primed_ = false;
    settle_started_ = false;
    active_ = false;
    delta_ = 0;
    base_primed_ = saved_baseline > 0;
    // Set either way. Leaving the old value in place would let baseline() keep
    // reporting a reference that begin() has just discarded, and anything that
    // copies it out -- the RTC cache, the diagnostics -- would carry it forward.
    base_ = (int32_t)(base_primed_ ? saved_baseline : 0) << cfg_.base_shift;
  }

  // id_mask is what the ID sensors read: non-zero means a neighbour is physically
  // there. Those sensors are digital and independent of this reading, so they can
  // say so while the analog signal is still below the trip point -- which is the
  // whole range the baseline must not adapt through.
  bool update(int raw, uint32_t now_ms, uint8_t id_mask) {
    if (!primed_) {
      fast_ = (int32_t)raw << cfg_.fast_shift;
      last_base_ms_ = now_ms;
      primed_ = true;
    } else {
      fast_ += raw - (fast_ >> cfg_.fast_shift);
    }

    const int f = (int)(fast_ >> cfg_.fast_shift);

    // Priming from a sample taken with a neighbour present subtracts the magnet
    // into the baseline and it is never seen again. Cubes are powered up in
    // whatever arrangement they were left in, so that is a coin flip on every
    // cold boot; wait for a clean sample instead. Until one arrives there is no
    // reference, and nothing can be reported from it.
    if (!base_primed_) {
      if (id_mask != 0) {
        settle_started_ = false;
        delta_ = 0;
        active_ = false;
        return false;
      }
      // A cold boot reaches here before the sensor output has settled. Slot 16
      // primed from a first sample of 1702 against a true idle near 1850, latched
      // on the difference, and had persisted the bad value to NVS before the
      // activation closed the save gate -- so a power cycle restored it.
      if (!settle_started_) {
        settle_started_ = true;
        settle_start_ms_ = now_ms;
        // The fast filter carries whatever it tracked through the blind period,
        // which may include a magnet that has since gone.
        fast_ = (int32_t)raw << cfg_.fast_shift;
      }
      if ((uint32_t)(now_ms - settle_start_ms_) < PRIME_SETTLE_MS) {
        delta_ = 0;
        active_ = false;
        return false;
      }
      // From the filtered value rather than this one sample: it has tracked only
      // magnet-free samples since settling began, so it is the same reading with
      // the noise taken out.
      base_ = (int32_t)f << cfg_.base_shift;
      base_primed_ = true;
      last_base_ms_ = now_ms;
      delta_ = 0;
      active_ = false;
      return false;
    }

    delta_ = cfg_.direction * (f - baseline());

    if (!active_) {
      if (delta_ >= cfg_.on_delta) active_ = true;
    } else {
      if (delta_ < cfg_.off_delta) active_ = false;
    }

    // Frozen while a neighbour is there, latched or not: otherwise the baseline
    // creeps up to the magnet and the cube forgets the neighbour is there.
    if (id_mask == 0 && !active_ &&
        (uint32_t)(now_ms - last_base_ms_) >= cfg_.base_interval_ms) {
      base_ += f - (base_ >> cfg_.base_shift);
      last_base_ms_ = now_ms;
    }
    return active_;
  }

  bool active()   const { return active_; }
  // False until a sample arrived with no neighbour to prime from. While it is
  // false nothing is reported, which from outside looks the same as an absent
  // neighbour -- the diagnostics publish it so the two can be told apart.
  bool primed()   const { return base_primed_; }
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
  // How long the reading must be magnet-free before it is taken as the reference.
  static const uint32_t PRIME_SETTLE_MS = 500;
  bool     settle_started_ = false;
  uint32_t settle_start_ms_ = 0;
  bool     primed_ = false;
  bool     base_primed_ = false;
};
