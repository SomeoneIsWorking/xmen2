#pragma once

#include <cstdint>

namespace x2::input {

/*
 * A DirectInput device belongs to one connected controller instance, not to
 * the inventory slot that happened to contain it when it was created. Slots
 * are reusable; the live instance GUID is not.
 */
struct ControllerInstance {
  unsigned char guid[16];
};

void controller_instance_bind(ControllerInstance *instance,
                              const unsigned char guid[16]);
int controller_instance_matches(const ControllerInstance *instance,
                                const unsigned char guid[16]);
int controller_instance_resolve(const ControllerInstance *instance);

} // namespace x2::input
