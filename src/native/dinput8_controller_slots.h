#pragma once

#include <cstddef>
#include <cstdint>

namespace x2::native {

/* The game's input manager, the one its frame update polls; 0 before the
   game creates it. */
uint32_t dinput8_input_manager(void);

/* The retail table's fixed size; both translators iterate it. */
inline constexpr int DINPUT8_CONTROLLER_SLOTS = 10;

/* Translate between SDL-host inventory pads and XMen2's ten-slot DirectInput
   table. The two index spaces are independent and can reorder after hotswap.
   Both directions return -1 while the device is not attached. */
int dinput8_controller_slot_for_host_pad(int host_pad);
int dinput8_controller_host_pad_for_slot(int controller_slot);

/* The POLL side of the manager, as FUN_006285c0 itself sees it: every one of
 * the ten device-interface pointers it walks, which of them the attached
 * table names, and which it actually read last frame. Every slot prints,
 * including the empty ones -- issue #117 is exactly the case where the
 * interesting fact is an absence. */
size_t dinput8_controller_slots_probe(char *out, size_t size);

} // namespace x2::native
