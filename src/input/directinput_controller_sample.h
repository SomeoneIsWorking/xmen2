#ifndef X2_DIRECTINPUT_CONTROLLER_SAMPLE_H
#define X2_DIRECTINPUT_CONTROLLER_SAMPLE_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

enum {
  X2_DIRECTINPUT_AXIS_COUNT = 6,
  X2_DIRECTINPUT_BUTTON_COUNT = 10,
};

/* One latched host-controller snapshot before either the retained DirectInput
 * writer or the shared Alchemy adapter interprets it. Keeping this as a value
 * makes the A/B comparison use identical input rather than two device polls. */
typedef struct X2DirectInputControllerSample {
  uint32_t device_id;
  int32_t axes[X2_DIRECTINPUT_AXIS_COUNT];
  uint32_t pov;
  uint16_t buttons;
  float left_trigger;
  float right_trigger;
} X2DirectInputControllerSample;

int x2_directinput_controller_capture(int pad, int32_t axis_lo, int32_t axis_hi,
                                      X2DirectInputControllerSample *out);

/* Serialize the retained DIJOYSTATE2 fields. `out_size` is validated by the
 * caller's neutral-state owner before this is called. */
void x2_directinput_controller_write(
    const X2DirectInputControllerSample *sample, unsigned char *out,
    uint32_t out_size);

#ifdef __cplusplus
}
#endif

#endif
