#include "controller_instance.h"

#include "dinput_pad.h"

#include <string.h>

void x2_controller_instance_bind(X2ControllerInstance *instance,
                                 const unsigned char guid[16]) {
  if (!instance || !guid)
    return;
  memcpy(instance->guid, guid, sizeof instance->guid);
}

int x2_controller_instance_matches(const X2ControllerInstance *instance,
                                   const unsigned char guid[16]) {
  return instance && guid &&
         memcmp(instance->guid, guid, sizeof instance->guid) == 0;
}

int x2_controller_instance_resolve(const X2ControllerInstance *instance) {
  return instance ? dinput_pad_for_guid(instance->guid) : -1;
}
