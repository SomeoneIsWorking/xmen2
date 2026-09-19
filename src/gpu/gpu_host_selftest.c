/* The complete host-renderer battery, kept out of the process entry point. */
#include "../native/x2_log.h"
#include "gpu_selftests.h"

#include <stddef.h>

/*
 * EVERY check runs, and the battery says so with a denominator.
 *
 * This stopped at the first failure until issue #152 needed the answer to a
 * test that sits at the end of the list: a browser run reported
 * `gpu multistage selftest: FAILED` and returned there, so the two draw-path
 * checks below it -- the ones the issue was built to ask -- never ran and
 * their silence read exactly like a pass. A battery that reports one failure
 * and hides an unknown number of unrun checks is worth less than the checks
 * it contains.
 *
 * Each entry is independent: it acquires its own device, textures and buffers
 * and releases them, so a failure earlier in the list does not invalidate a
 * result later in it.
 *
 * 0 passed, 77 could not run on this build, anything else failed with its own
 * detail already printed.
 */
typedef struct {
  const char *name;
  int (*run)(void);
} GpuSelftestEntry;

static const GpuSelftestEntry kBattery[] = {
    {"present", gpu_present_selftest},
    {"device", gpu_device_selftest},
    {"upload reuse", gpu_upload_reuse_selftest},
    {"upload order", gpu_upload_order_selftest},
    {"frame init", gpu_frame_init_selftest},
    {"mid-frame clear", gpu_midframe_clear_selftest},
    {"cube texgen", gpu_cube_texgen_selftest},
    {"texture factor", gpu_tfactor_selftest},
    {"BC1 texture", gpu_bc1_texture_selftest},
    {"multistage", gpu_multistage_selftest},
    {"shadow", gpu_shadow_selftest},
    {"prompt glyphs", gpu_prompt_glyphs_selftest},
    /* Presenting a frame and drawing into one are different claims. */
    {"draw", gpu_draw_selftest},
    /* Issue #152: the D3DFVF_XYZ + lighting branch, which the draw check
       never reaches (it uses D3DFVF_XYZRHW). */
    {"lit/MVP draw", gpu_lit_mvp_selftest},
};

int gpu_host_selftest(void) {
  const size_t total = sizeof kBattery / sizeof kBattery[0];
  size_t passed = 0;
  size_t skipped = 0;
  size_t failed = 0;
  int first_failure = 0;
  size_t i;

  for (i = 0; i < total; i++) {
    const int result = kBattery[i].run();
    if (result == 0) {
      passed++;
    } else if (result == 77) {
      skipped++;
      x2_log_info("gpu selftests: %s could not run on this build.\n",
                  kBattery[i].name);
    } else {
      failed++;
      x2_log_info("gpu selftests: %s FAILED (%d).\n", kBattery[i].name, result);
      if (first_failure == 0) {
        first_failure = result;
      }
    }
  }

  x2_log_info("gpu selftests: %zu of %zu passed, %zu skipped, %zu failed.\n",
              passed, total, skipped, failed);
  if (first_failure != 0) {
    return first_failure;
  }
  /* A battery that ran nothing has proved nothing, and must not be read as a
     renderer that works. */
  if (passed == 0) {
    x2_log_info("gpu selftests: none of the %zu checks could run, so this "
                "build's renderer is unproven rather than working.\n",
                total);
    return 77;
  }
  return 0;
}
