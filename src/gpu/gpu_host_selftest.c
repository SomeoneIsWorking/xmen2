/* The complete host-renderer battery, kept out of the process entry point. */

int gpu_device_selftest(void);
int gpu_draw_selftest(void);
int gpu_midframe_clear_selftest(void);
int gpu_cube_texgen_selftest(void);
int gpu_tfactor_selftest(void);
int gpu_upload_reuse_selftest(void);

int gpu_host_selftest(void)
{
    int result = gpu_device_selftest();
    if (result) return result;

    result = gpu_upload_reuse_selftest();
    if (result) return result;

    result = gpu_midframe_clear_selftest();
    if (result && result != 77) return result;

    result = gpu_cube_texgen_selftest();
    if (result && result != 77) return result;

    result = gpu_tfactor_selftest();
    if (result && result != 77) return result;

    /* Presenting a frame and drawing into one are different claims. */
    return gpu_draw_selftest();
}
