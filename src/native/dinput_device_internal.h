#ifndef X2_DINPUT_DEVICE_INTERNAL_H
#define X2_DINPUT_DEVICE_INTERNAL_H

#include "controller_instance.h"
#include "dinput_device.h"
#include "x86rt.h"

typedef struct {
  DInputDeviceKind kind;
  uint32_t guest;
  uint32_t refs;
  uint32_t data_size;
  uint32_t coop;
  int acquired;
  unsigned long polls;
  unsigned long n_poll, n_acquire, n_acquire_fail;
  X2ControllerInstance controller;
  int32_t axis_lo, axis_hi;
  int range_set;
} DInputDevice;

static inline int dinput_device_pad(const DInputDevice *device) {
  return device && device->kind == DINPUT_DEV_JOYSTICK
             ? x2_controller_instance_resolve(&device->controller)
             : -1;
}

static inline void dinput_device_return(CPU *cpu, uint32_t result, int nargs) {
  cpu->eax = result;
  cpu->esp += 4u + (uint32_t)(nargs + 1) * 4u;
}

void dinput_device_get_state(CPU *cpu, DInputDevice *device);

#endif /* X2_DINPUT_DEVICE_INTERNAL_H */
