/*
 * Pixel assertion for the D3DFVF_XYZRHW screen-space path.
 *
 * The draw orchestration remains in d3d8_report.c. This module owns the one
 * asymmetric assertion needed to tell a correctly oriented D3D triangle from
 * its vertical mirror; centre/corner coverage cannot make that distinction.
 */
#include "d3d8_screen_space_test.h"
#include "../native/x2_log.h"

#include <stdio.h>

int d3d8_screen_space_pixels_check(const uint32_t *pixels, int width,
                                   int height) {
  uint32_t centre, corner, upper_left, lower_left;
  int fails = 0;

  if (!pixels || width < 55 || height < 55) {
    x2_log_info("d3d8 draw selftest: FAILED -- XYZRHW orientation check needs "
                "at least a 55x55 pixel target.\n");
    return 1;
  }

  centre = pixels[(height / 2) * width + width / 2];
  corner = pixels[width + 1];
  if (centre != 0xFFFF0000u) {
    x2_log_info("d3d8 draw selftest: FAILED -- the centre is 0x%08x, not the "
                "red triangle (0xffff0000).\n",
                centre);
    fails++;
  }
  if (corner != 0xFF00FF00u) {
    x2_log_info("d3d8 draw selftest: FAILED -- the corner is 0x%08x, not the "
                "green clear (0xff00ff00); something filled the whole "
                "target.\n",
                corner);
    fails++;
  }

  /* The test triangle has a narrow apex at the top and a wide base at the
     bottom. A vertical mirror swaps these two deliberately unequal pixels. */
  upper_left = pixels[10 * width + 10];
  lower_left = pixels[54 * width + 10];
  if (upper_left == 0xFF00FF00u && lower_left == 0xFFFF0000u)
    return fails;

  x2_log_info("d3d8 draw selftest: FAILED -- XYZRHW Y orientation is mirrored: "
              "upper-left 0x%08x (expected green), lower-left 0x%08x "
              "(expected red).\n",
              upper_left, lower_left);
  return fails + 1;
}
