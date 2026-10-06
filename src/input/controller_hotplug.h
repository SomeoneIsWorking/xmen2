#ifndef X2_CONTROLLER_HOTPLUG_H
#define X2_CONTROLLER_HOTPLUG_H

#include <stdint.h>

/*
 * Admission is keyed by inventory generation. This deliberately remembers no
 * controller GUIDs: disconnected instances are dead, and an unbounded process
 * may see arbitrarily many new GUIDs while still having only eight live pads.
 */
typedef struct {
  uint64_t processed_generation;
  unsigned long admissions;
  int initialized;
  int connected;
  int last_reported;
} X2ControllerHotplug;

int x2_controller_hotplug_needs_admission(X2ControllerHotplug *state,
                                          uint64_t generation);
void x2_controller_hotplug_enumerated(X2ControllerHotplug *state,
                                      uint64_t generation, int connected,
                                      int reported);
void x2_controller_hotplug_admitted(X2ControllerHotplug *state);

/* Force the next pump to re-admit the live inventory through the game's own
   enumeration, even though no SDL generation changed. */
void x2_controller_hotplug_invalidate(X2ControllerHotplug *state);

#endif /* X2_CONTROLLER_HOTPLUG_H */
