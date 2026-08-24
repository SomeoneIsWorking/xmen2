#include "dinput8_controller_slots.h"

#include "dinput_pad.h"

#include <stdint.h>
#include <string.h>

#define RETAIL_CONTROLLER_SLOTS 10
#define RETAIL_ATTACHED_OFFSET  0x4e4u
#define RETAIL_INSTANCE_OFFSET  0x27e8u

static uint32_t g_manager;

static const unsigned char *slot_guid(int controller_slot)
{
    return (const unsigned char *)(uintptr_t)
        (g_manager + RETAIL_INSTANCE_OFFSET + (uint32_t)controller_slot * 16u);
}

static int slot_is_attached(int controller_slot)
{
    const unsigned char *attached = (const unsigned char *)(uintptr_t)
        (g_manager + RETAIL_ATTACHED_OFFSET);
    return attached[controller_slot] != 0;
}

void dinput8_controller_slots_set_manager(uint32_t manager)
{
    g_manager = manager;
}

int dinput8_controller_slot_for_host_pad(int host_pad)
{
    unsigned char guid[16];
    int controller_slot;

    if (!g_manager || !dinput_pad_instance_guid(host_pad, guid)) return -1;
    for (controller_slot = 0;
         controller_slot < RETAIL_CONTROLLER_SLOTS;
         controller_slot++)
        if (slot_is_attached(controller_slot) &&
            memcmp(slot_guid(controller_slot), guid, sizeof guid) == 0)
            return controller_slot;
    return -1;
}

int dinput8_controller_host_pad_for_slot(int controller_slot)
{
    if (!g_manager || controller_slot < 0 ||
        controller_slot >= RETAIL_CONTROLLER_SLOTS ||
        !slot_is_attached(controller_slot))
        return -1;
    return dinput_pad_for_guid(slot_guid(controller_slot));
}
