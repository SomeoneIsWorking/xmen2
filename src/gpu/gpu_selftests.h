#ifndef GPU_SELFTESTS_H
#define GPU_SELFTESTS_H

/*
 * The host-renderer self-test entry points, declared once.
 *
 * Every one of these lives in a different translation unit and was, before
 * this header, forward-declared by hand in gpu_host_selftest.c. That block of
 * loose declarations was the only thing tying a definition to its caller, so
 * nothing checked that the two agreed: a signature change in one file would
 * have compiled and linked against a stale declaration in the other.
 *
 * The contract every one of them shares: 0 means the test ran and passed,
 * 77 means it could not run at all on this build (no SDL), and anything else
 * is a failure whose detail has already been printed. The battery treats 77
 * as "keep going" for the tests that can legitimately be skipped, so a
 * skipped test must return 77 and must never return 0.
 */

int gpu_device_selftest(void);
int gpu_present_selftest(void);
int gpu_draw_selftest(void);
int gpu_frame_draw_selftest(void);
int gpu_lit_mvp_selftest(void);
int gpu_frame_init_selftest(void);
int gpu_midframe_clear_selftest(void);
int gpu_cube_texgen_selftest(void);
int gpu_tfactor_selftest(void);
int gpu_bc1_texture_selftest(void);
int gpu_multistage_selftest(void);
int gpu_upload_reuse_selftest(void);
int gpu_upload_order_selftest(void);
int gpu_index_upload_order_selftest(void);
int gpu_shadow_selftest(void);
int gpu_prompt_glyphs_selftest(void);
int gpu_host_selftest(void);

#endif /* GPU_SELFTESTS_H */
