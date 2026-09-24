/* frame_limiter_wait.c -- see frame_limiter_wait.h. */
#include "frame_limiter_wait.h"

uint32_t frame_limiter_sleep_us(float min_frame_s, float frame_start_s,
                                float last_read_s) {
  double remaining =
      (double)min_frame_s - ((double)last_read_s - (double)frame_start_s);
  /* A clock read from before the frame began (a wrapped or reset timer) says
     more than a whole frame is left; a frame is the most there can be. */
  if (remaining > (double)min_frame_s)
    remaining = (double)min_frame_s;
  double us = remaining * 1e6 - (double)FRAME_LIMITER_SPIN_US;
  /* Written so a NaN anywhere falls out here. */
  if (!(us >= (double)FRAME_LIMITER_MIN_SLEEP_US))
    return 0u;
  if (us >= (double)UINT32_MAX)
    return UINT32_MAX;
  return (uint32_t)us;
}
