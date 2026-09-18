/* The complete host-renderer battery, kept out of the process entry point. */
#include "gpu_selftests.h"

int gpu_host_selftest(void) {
  int result = gpu_present_selftest();
  if (result)
    return result;

  result = gpu_device_selftest();
  if (result)
    return result;

  result = gpu_upload_reuse_selftest();
  if (result)
    return result;

  result = gpu_upload_order_selftest();
  if (result)
    return result;

  result = gpu_frame_init_selftest();
  if (result && result != 77)
    return result;

  result = gpu_midframe_clear_selftest();
  if (result && result != 77)
    return result;

  result = gpu_cube_texgen_selftest();
  if (result && result != 77)
    return result;

  result = gpu_tfactor_selftest();
  if (result && result != 77)
    return result;

  result = gpu_bc1_texture_selftest();
  if (result && result != 77)
    return result;

  result = gpu_multistage_selftest();
  if (result && result != 77)
    return result;

  result = gpu_shadow_selftest();
  if (result && result != 77)
    return result;

  result = gpu_prompt_glyphs_selftest();
  if (result && result != 77)
    return result;

  /* Presenting a frame and drawing into one are different claims. */
  result = gpu_draw_selftest();
  if (result)
    return result;

  /* Issue #152: the D3DFVF_XYZ + lighting branch, which the test above never
     reaches (it uses D3DFVF_XYZRHW). */
  return gpu_lit_mvp_selftest();
}
