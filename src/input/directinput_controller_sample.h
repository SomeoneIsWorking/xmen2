#pragma once

#include <cstdint>

namespace x2::input {

inline constexpr int kDirectInputAxisCount = 6;
inline constexpr int kDirectInputButtonCount = 10;

/* One latched host-controller snapshot before either the retained DirectInput
 * writer or the shared Alchemy adapter interprets it. Keeping this as a value
 * makes the A/B comparison use identical input rather than two device polls. */
struct DirectInputControllerSample {
  uint32_t device_id;
  int32_t axes[kDirectInputAxisCount];
  uint32_t pov;
  uint16_t buttons;
  float left_trigger;
  float right_trigger;
};

int directinput_controller_capture(int pad, int32_t axis_lo, int32_t axis_hi,
                                   DirectInputControllerSample *out);

/* Serialize the retained DIJOYSTATE2 fields. `out_size` is validated by the
 * caller's neutral-state owner before this is called. */
void directinput_controller_write(const DirectInputControllerSample *sample,
                                  unsigned char *out, uint32_t out_size);

} // namespace x2::input
