#include "directinput_controller_sample.h"

#include "dinput_pad.h"

#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

uint32_t dinput_pad_device_id(int pad) { return pad == 2 ? 77u : 0u; }

int32_t dinput_pad_axis(int pad, int axis, int32_t lo, int32_t hi) {
  (void)lo;
  (void)hi;
  assert(pad == 2);
  return -900 + axis * 300;
}

uint32_t dinput_pad_pov(int pad) {
  assert(pad == 2);
  return 4500u;
}

int dinput_pad_button_count(int pad) {
  assert(pad == 2);
  return X2_DIRECTINPUT_BUTTON_COUNT;
}

int dinput_pad_button(int pad, int button) {
  assert(pad == 2);
  return (button % 2) == 0;
}

float dinput_pad_trigger_pressure(int pad, int trigger) {
  assert(pad == 2);
  return trigger == 0 ? 0.25F : 0.75F;
}

static uint32_t read_u32(const unsigned char *bytes, uint32_t offset) {
  uint32_t value;
  memcpy(&value, bytes + offset, sizeof value);
  return value;
}

int main(void) {
  X2DirectInputControllerSample sample;
  unsigned char state[176];

  assert(!x2_directinput_controller_capture(1, -1000, 1000, &sample));
  assert(x2_directinput_controller_capture(2, -1000, 1000, &sample));
  assert(sample.device_id == 77u);
  for (int axis = 0; axis < X2_DIRECTINPUT_AXIS_COUNT; ++axis) {
    assert(sample.axes[axis] == -900 + axis * 300);
  }
  assert(sample.pov == 4500u);
  assert(sample.buttons == 0x0155u);
  assert(sample.left_trigger == 0.25F);
  assert(sample.right_trigger == 0.75F);

  memset(state, 0, sizeof state);
  x2_directinput_controller_write(&sample, state, sizeof state);
  for (uint32_t axis = 0; axis < X2_DIRECTINPUT_AXIS_COUNT; ++axis) {
    assert((int32_t)read_u32(state, axis * 4u) == -900 + (int32_t)axis * 300);
  }
  assert(read_u32(state, 32u) == 4500u);
  for (uint32_t button = 0; button < X2_DIRECTINPUT_BUTTON_COUNT; ++button) {
    assert(state[48u + button] == ((button % 2u) == 0u ? 0x80u : 0u));
  }

  puts("DirectInput sample: retained capture and DIJOYSTATE2 serialization "
       "passed");
  return 0;
}
