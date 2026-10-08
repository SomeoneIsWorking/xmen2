#ifndef X2_ALCHEMY_CONTROLLER_BRIDGE_H
#define X2_ALCHEMY_CONTROLLER_BRIDGE_H

#include "directinput_controller_sample.h"

#include <stdint.h>

/* Feed the shared Alchemy controller owner from the same latched value the
 * retained DirectInput writer consumes, then perform the configured A/B
 * comparison. */
void x2_alchemy_controller_observe(
    int host_slot, const x2::input::DirectInputControllerSample *sample,
    int32_t axis_lo, int32_t axis_hi);

/* Reconcile removals even though DirectInput stops polling a detached device.
 */
void x2_alchemy_controller_sync_inventory(void);

void x2_alchemy_controller_report(void);

#endif
