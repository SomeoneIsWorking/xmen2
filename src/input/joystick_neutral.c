#include "joystick_neutral.h"

#include <string.h>

int x2_joystick_write_neutral(void *state, size_t size, int32_t axis_lo,
                              int32_t axis_hi) {
  unsigned char *bytes = state;
  int32_t midpoint = axis_lo + (axis_hi - axis_lo) / 2;
  unsigned i;

  if (!state)
    return 0;
  memset(state, 0, size);
  if (size < 176u)
    return 0;
  for (i = 0; i < 8u; i++)
    memcpy(bytes + i * sizeof midpoint, &midpoint, sizeof midpoint);
  for (i = 0; i < 4u; i++) {
    uint32_t centred = UINT32_MAX;
    memcpy(bytes + 32u + i * sizeof centred, &centred, sizeof centred);
  }
  return 1;
}
