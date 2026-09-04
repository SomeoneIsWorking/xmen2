#ifndef X2_ALCHEMY_CONTROLLER_BRIDGE_H
#define X2_ALCHEMY_CONTROLLER_BRIDGE_H

#include "directinput_controller_sample.h"

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Feed the shared Alchemy controller owner from the same latched value the
 * retained DirectInput writer consumes, then perform the configured A/B
 * comparison. */
void x2_alchemy_controller_observe(int host_slot,
                                   const X2DirectInputControllerSample *sample,
                                   int32_t axis_lo, int32_t axis_hi);

/* Reconcile removals even though DirectInput stops polling a detached device.
 */
void x2_alchemy_controller_sync_inventory(void);

void x2_alchemy_controller_report(void);

#ifdef __cplusplus
}
#endif

#endif
