#pragma once

#include "directinput_controller_sample.h"

#include <cstdint>

namespace x2::input {

/* Feed the shared Alchemy controller owner from the same latched value the
 * retained DirectInput writer consumes, then perform the configured A/B
 * comparison. */
void alchemy_controller_observe(int host_slot,
                                const DirectInputControllerSample *sample,
                                int32_t axis_lo, int32_t axis_hi);

/* Reconcile removals even though DirectInput stops polling a detached device.
 */
void alchemy_controller_sync_inventory(void);

void alchemy_controller_report(void);

} // namespace x2::input
