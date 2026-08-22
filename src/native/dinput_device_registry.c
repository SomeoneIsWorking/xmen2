#include "dinput_device_registry.h"

#include <stdlib.h>

/* Methods retain a Device pointer across guest re-entry. Store each object in
   its own allocation so growing the pointer index cannot invalidate it. */
static DInputDevice **devices;
static size_t count;
static size_t capacity;

DInputDevice *dinput_device_registry_find(uint32_t guest)
{
    size_t i;
    for (i = 0; i < count; i++)
        if (devices[i]->guest == guest) return devices[i];
    return NULL;
}

DInputDevice *dinput_device_registry_find_system(DInputDeviceKind kind)
{
    size_t i;
    for (i = 0; i < count; i++)
        if (devices[i]->kind == kind && kind != DINPUT_DEV_JOYSTICK)
            return devices[i];
    return NULL;
}

DInputDevice *dinput_device_registry_find_controller(
    const unsigned char guid[16])
{
    size_t i;
    for (i = 0; i < count; i++)
        if (devices[i]->kind == DINPUT_DEV_JOYSTICK &&
            x2_controller_instance_matches(&devices[i]->controller, guid))
            return devices[i];
    return NULL;
}

DInputDevice *dinput_device_registry_append(void)
{
    DInputDevice **grown;
    DInputDevice *device;
    size_t next;
    if (count == capacity) {
        next = capacity ? capacity * 2 : 8;
        grown = (DInputDevice **)realloc(devices, next * sizeof *devices);
        if (!grown) return NULL;
        devices = grown;
        capacity = next;
    }
    device = (DInputDevice *)calloc(1, sizeof *device);
    if (!device) return NULL;
    devices[count++] = device;
    return device;
}

size_t dinput_device_registry_count(void) { return count; }

DInputDevice *dinput_device_registry_at(size_t index)
{
    return index < count ? devices[index] : NULL;
}
