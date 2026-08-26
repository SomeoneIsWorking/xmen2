/* The complete host-renderer battery, kept out of the process entry point. */

int gpu_device_selftest(void);
int gpu_present_selftest(void);
int gpu_draw_selftest(void);
int gpu_frame_init_selftest(void);
int gpu_midframe_clear_selftest(void);
int gpu_cube_texgen_selftest(void);
int gpu_tfactor_selftest(void);
int gpu_upload_reuse_selftest(void);
int gpu_upload_order_selftest(void);
int gpu_shadow_selftest(void);
int gpu_prompt_glyphs_selftest(void);

int gpu_host_selftest(void)
{
    int result = gpu_present_selftest();
    if (result) return result;

    result = gpu_device_selftest();
    if (result) return result;

    result = gpu_upload_reuse_selftest();
    if (result) return result;

    result = gpu_upload_order_selftest();
    if (result) return result;

    result = gpu_frame_init_selftest();
    if (result && result != 77) return result;

    result = gpu_midframe_clear_selftest();
    if (result && result != 77) return result;

    result = gpu_cube_texgen_selftest();
    if (result && result != 77) return result;

    result = gpu_tfactor_selftest();
    if (result && result != 77) return result;

    result = gpu_shadow_selftest();
    if (result && result != 77) return result;

    result = gpu_prompt_glyphs_selftest();
    if (result && result != 77) return result;

    /* Presenting a frame and drawing into one are different claims. */
    return gpu_draw_selftest();
}
