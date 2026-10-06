#include "directinput_controller_sample.h"

#include "dinput_pad.h"

#include <string.h>

int x2_directinput_controller_capture(int pad, int32_t axis_lo, int32_t axis_hi,
                                      X2DirectInputControllerSample *out) {
  int button;

  if (out == NULL) {
    return 0;
  }
  memset(out, 0, sizeof *out);
  out->device_id = dinput_pad_device_id(pad);
  if (out->device_id == 0) {
    return 0;
  }
  for (int axis = 0; axis < X2_DIRECTINPUT_AXIS_COUNT; ++axis) {
    out->axes[axis] = dinput_pad_axis(pad, axis, axis_lo, axis_hi);
  }
  out->pov = dinput_pad_pov(pad);
  for (button = 0; button < dinput_pad_button_count(pad) &&
                   button < X2_DIRECTINPUT_BUTTON_COUNT;
       ++button) {
    if (dinput_pad_button(pad, button)) {
      out->buttons |= (uint16_t)(1u << (unsigned)button);
    }
  }
  out->left_trigger = dinput_pad_trigger_pressure(pad, 0);
  out->right_trigger = dinput_pad_trigger_pressure(pad, 1);
  return 1;
}

static void write_u32(unsigned char *out, uint32_t offset, uint32_t value) {
  memcpy(out + offset, &value, sizeof value);
}

void x2_directinput_controller_write(
    const X2DirectInputControllerSample *sample, unsigned char *out,
    uint32_t out_size) {
  int button;

  if (sample == NULL || out == NULL) {
    return;
  }
  for (uint32_t axis = 0; axis < X2_DIRECTINPUT_AXIS_COUNT; ++axis) {
    write_u32(out, axis * 4u, (uint32_t)sample->axes[axis]);
  }
  write_u32(out, 32u, sample->pov);
  for (button = 0; button < X2_DIRECTINPUT_BUTTON_COUNT &&
                   48u + (uint32_t)button < out_size;
       ++button) {
    if ((sample->buttons & (uint16_t)(1u << (unsigned)button)) != 0) {
      out[48u + (uint32_t)button] = 0x80u;
    }
  }
}
