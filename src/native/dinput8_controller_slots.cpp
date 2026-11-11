#include "dinput8_controller_slots.h"

#include "dinput_pad.h"
#include "guest_memory.h"

#include <stdint.h>
#include <stdio.h>
#include <string.h>

namespace x2::native {

#define RETAIL_CONTROLLER_SLOTS 10
#define RETAIL_ATTACHED_OFFSET 0x4e4u
#define RETAIL_INSTANCE_OFFSET 0x27e8u

/* The POLL side, read out of FUN_006285c0 rather than assumed. That update
   walks ten slots and its two facts live at fixed offsets from the manager:

     0x6287e6  LEA EBX,[manager+0xc]        the device-interface array
     0x6287f0  MOV EAX,[EBX] / TEST/JZ      a NULL slot is skipped entirely
     0x628848  OR [manager+0x129cc],1<<slot set when that slot's
                                            GetDeviceState(0x110) succeeded
     0x628870  CMP EAX,0xa                  ten slots, no deserialized count

   So a slot the game never reads is a slot whose interface pointer is NULL
   (or whose GetDeviceState fails); nothing else in that loop can gate it.
   That is the whole question issue #117 asks, and these two offsets answer
   it directly instead of inferring it from a button counter. */
#define RETAIL_DEVICE_ARRAY_OFFSET 0x0cu
#define RETAIL_POLLED_MASK_OFFSET 0x129ccu

/* XMen2.exe's input manager: FUN_0061bae0 creates it, FUN_0061c3b0 polls it
   each frame. The scan thread FUN_00618c90 enumerates into a scratch manager
   of its own, so an EnumDevices pvRef does not name this one. */
inline constexpr uint32_t kInputManager = 0x00a6abfcu;

uint32_t dinput8_input_manager(void) {
  return *guest_memory_as<const uint32_t>(kInputManager);
}

static const unsigned char *slot_guid(uint32_t manager, int controller_slot) {
  return guest_memory_as<const unsigned char>(manager + RETAIL_INSTANCE_OFFSET +
                                              (uint32_t)controller_slot * 16u);
}

static int slot_is_attached(uint32_t manager, int controller_slot) {
  const unsigned char *attached =
      guest_memory_as<const unsigned char>(manager + RETAIL_ATTACHED_OFFSET);
  return attached[controller_slot] != 0;
}

int dinput8_controller_slot_for_host_pad(int host_pad) {
  unsigned char guid[16];
  int controller_slot;
  const uint32_t manager = dinput8_input_manager();

  if (!manager || !dinput_pad_instance_guid(host_pad, guid))
    return -1;
  for (controller_slot = 0; controller_slot < RETAIL_CONTROLLER_SLOTS;
       controller_slot++)
    if (slot_is_attached(manager, controller_slot) &&
        memcmp(slot_guid(manager, controller_slot), guid, sizeof guid) == 0)
      return controller_slot;
  return -1;
}

int dinput8_controller_host_pad_for_slot(int controller_slot) {
  const uint32_t manager = dinput8_input_manager();
  if (!manager || controller_slot < 0 ||
      controller_slot >= RETAIL_CONTROLLER_SLOTS ||
      !slot_is_attached(manager, controller_slot))
    return -1;
  return dinput_pad_for_guid(slot_guid(manager, controller_slot));
}

size_t dinput8_controller_slots_probe(char *out, size_t size) {
  const uint32_t *devices;
  uint32_t mask;
  int slot, live = 0, attached = 0, read = 0;
  size_t at = 0;
  int wrote;
  const uint32_t manager = dinput8_input_manager();

  if (!out || !size)
    return 0;
  /* Refuse rather than return an empty report: "no slots" and "the manager
     was never published" are different answers and must not print alike. */
  if (!manager) {
    wrote = snprintf(out, size,
                     "  dinput8 poll side: the game has not created its "
                     "input manager yet, so NOTHING below is a statement "
                     "about the poll side.\n");
    return wrote > 0 ? (size_t)wrote : 0;
  }

  devices =
      guest_memory_as<const uint32_t>(manager + RETAIL_DEVICE_ARRAY_OFFSET);
  mask = *guest_memory_as<const uint32_t>(manager + RETAIL_POLLED_MASK_OFFSET);

  wrote =
      snprintf(out + at, size - at,
               "  dinput8 poll side (manager 0x%08x, FUN_006285c0's own "
               "array at +0x%x and mask at +0x%x):\n",
               manager, RETAIL_DEVICE_ARRAY_OFFSET, RETAIL_POLLED_MASK_OFFSET);
  if (wrote > 0)
    at += (size_t)wrote;

  for (slot = 0; slot < RETAIL_CONTROLLER_SLOTS; slot++) {
    int host = dinput8_controller_host_pad_for_slot(slot);
    int polled = (mask >> slot) & 1u;
    if (devices[slot])
      live++;
    if (host >= 0)
      attached++;
    if (polled)
      read++;
    if (at >= size)
      break;
    wrote = snprintf(out + at, size - at,
                     "    slot %d  device 0x%08x  %s  attached-table %s  "
                     "last frame %s\n",
                     slot, devices[slot],
                     devices[slot] ? "        "
                                   : "NULL -- SKIPPED by the "
                                     "poll loop",
                     host >= 0 ? "yes" : "no ", polled ? "READ" : "not read");
    if (wrote > 0)
      at += (size_t)wrote;
  }
  if (at < size) {
    wrote = snprintf(out + at, size - at,
                     "    %d of %d slot(s) hold a device interface, %d "
                     "named by the attached table, %d read last frame "
                     "(mask 0x%08x). %d host pad(s) are connected.\n",
                     live, RETAIL_CONTROLLER_SLOTS, attached, read, mask,
                     dinput_pad_count());
    if (wrote > 0)
      at += (size_t)wrote;
  }
  return at;
}

} // namespace x2::native
