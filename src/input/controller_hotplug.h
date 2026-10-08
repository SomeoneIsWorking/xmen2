#pragma once

#include <cstdint>

namespace x2::input {

/*
 * Admission is keyed by inventory generation. This deliberately remembers no
 * controller GUIDs: disconnected instances are dead, and an unbounded process
 * may see arbitrarily many new GUIDs while still having only eight live pads.
 */
struct ControllerHotplug {
  uint64_t processed_generation;
  unsigned long admissions;
  int initialized;
  int connected;
  int last_reported;
};

int controller_hotplug_needs_admission(ControllerHotplug *state,
                                       uint64_t generation);
void controller_hotplug_enumerated(ControllerHotplug *state,
                                   uint64_t generation, int connected,
                                   int reported);
void controller_hotplug_admitted(ControllerHotplug *state);

/* Force the next pump to re-admit the live inventory through the game's own
   enumeration, even though no SDL generation changed. */
void controller_hotplug_invalidate(ControllerHotplug *state);

} // namespace x2::input
