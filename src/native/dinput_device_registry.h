#pragma once

#include "dinput_device_internal.h"

#include <cstddef>

namespace x2::native {

DInputDevice *dinput_device_registry_find(uint32_t guest);
DInputDevice *dinput_device_registry_find_system(DInputDeviceKind kind);
DInputDevice *
dinput_device_registry_find_controller(const unsigned char guid[16]);
DInputDevice *dinput_device_registry_append(void);
size_t dinput_device_registry_count(void);
DInputDevice *dinput_device_registry_at(size_t index);

} // namespace x2::native
