#pragma once

#include <stdint.h>

// Border colors for the four panel edges; 0 means the edge is off.
struct BorderSides {
  uint16_t top = 0, bottom = 0, left = 0, right = 0;

  bool operator==(const BorderSides& o) const {
    return top == o.top && bottom == o.bottom && left == o.left && right == o.right;
  }
  bool operator!=(const BorderSides& o) const { return !(*this == o); }
};

// Which border is drawn, and the animation from the previous one.
//
// A border change animates for duration_ms. A target that arrives mid-
// animation is queued as `pending` and becomes the next animation's target,
// except within replace_window_ms of the start, where it replaces the target
// outright (MQTT can deliver a batch of topology updates before one frame).
struct BorderTransition {
  BorderSides from, to, pending;
  unsigned long start_ms = 0;
  bool active = false;
  bool has_pending = false;

  void set(const BorderSides& target, unsigned long now, unsigned long replace_window_ms) {
    // MQTT retained deliveries and idle border refreshes can repeat a target.
    // A repeated target must not start another redraw or replace an
    // animation that is already headed to that exact frame. It is still the
    // latest request, so anything queued behind it is stale and is dropped;
    // keeping it left a cube showing a border the server had since cleared.
    if (target == to) {
      has_pending = false;
      return;
    }
    if (active && has_pending && target == pending) {
      return;
    }
    if (active) {
      if (now - start_ms <= replace_window_ms) {
        to = target;
      } else {
        pending = target;
        has_pending = true;
      }
      return;
    }
    from = to;
    to = target;
    start_ms = now;
    active = true;
  }

  // Advance the animation. Returns true when the frame needs redrawing.
  bool tick(unsigned long now, unsigned long duration_ms) {
    if (!active) return false;
    if (now - start_ms >= duration_ms) {
      if (has_pending) {
        from = to;
        to = pending;
        has_pending = false;
        start_ms = now;
      } else {
        // Draw a settled static frame after the final eased frame so neither
        // DMA buffer can retain a rounded-short segment of the animation.
        active = false;
      }
    }
    return true;
  }
};
