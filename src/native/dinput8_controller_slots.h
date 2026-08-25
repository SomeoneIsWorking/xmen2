#ifndef X2_DINPUT8_CONTROLLER_SLOTS_H
#define X2_DINPUT8_CONTROLLER_SLOTS_H

#include <stdint.h>

/* The IDirectInput8 owner publishes the retail input-manager instance when
   the game's controller enumeration reaches it. */
void dinput8_controller_slots_set_manager(uint32_t manager);

/* The retail table's fixed size; both translators iterate it. */
#define DINPUT8_CONTROLLER_SLOTS 10

/* Translate between SDL-host inventory pads and XMen2's ten-slot DirectInput
   table. The two index spaces are independent and can reorder after hotswap.
   Both directions return -1 while the device is not attached. */
int dinput8_controller_slot_for_host_pad(int host_pad);
int dinput8_controller_host_pad_for_slot(int controller_slot);

#endif /* X2_DINPUT8_CONTROLLER_SLOTS_H */
