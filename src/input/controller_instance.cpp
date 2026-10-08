#include "controller_instance.h"

#include "dinput_pad.h"

#include <string.h>

namespace x2::input {

void controller_instance_bind(x2::input::ControllerInstance *instance,
                              const unsigned char guid[16]) {
  if (!instance || !guid)
    return;
  memcpy(instance->guid, guid, sizeof instance->guid);
}

int controller_instance_matches(const x2::input::ControllerInstance *instance,
                                const unsigned char guid[16]) {
  return instance && guid &&
         memcmp(instance->guid, guid, sizeof instance->guid) == 0;
}

int controller_instance_resolve(const x2::input::ControllerInstance *instance) {
  return instance ? dinput_pad_for_guid(instance->guid) : -1;
}

} // namespace x2::input
