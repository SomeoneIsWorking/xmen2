/*
 * A self-test for the host side of the renderer.
 *
 * ## Why this exists
 *
 * gpu_device.c implements a frame -- acquire a swapchain image, open a
 * render pass clearing to a colour, submit -- and at the time it was written
 * the game had never reached a frame, so not one line of it had ever run. The
 * engine stops earlier, in the exe. Untested code that looks finished is the
 * thing this project's rules exist to prevent, and "it will be exercised when
 * the game gets there" is exactly the promise that never gets kept.
 *
 * gpu_device.c takes no guest state on purpose -- that is what makes this
 * possible. The frame path can be driven from host code with no engine, no
 * ARK, and no recompiled bodies involved at all.
 *
 * ## What this DOES prove
 *
 * That a GPU device can be created, a swapchain claimed on a real window, and
 * frames acquired, cleared and presented, on this machine.
 *
 * ## What it does NOT prove, and must not be read as proving
 *
 * That the ENGINE's frame maps onto this correctly. The engine drives these
 * calls in its own order through slots 174/177/186/175, and whether that
 * order produces the right picture is a question only a real run answers.
 * This is a check on the host half, and the report says so in those words.
 */
#include "gpu_device.h"
#include "gpu_draw.h"

#include <stdio.h>
#include <string.h>

#ifdef X2_WITH_SDL
#include <SDL3/SDL.h>
#endif

#define FRAMES 3

int gpu_device_selftest(void)
{
#ifndef X2_WITH_SDL
    printf("gpu selftest: SKIPPED -- built without SDL, so there is no "
           "device to test. This is not a pass.\n");
    return 77;
#else
    SDL_Window *w = NULL;
    int i, presented_ok = 0, began = 0;

    printf("\n=== gpu selftest: the host frame path, with no engine "
           "involved ===\n");

    if (!gpu_device_create()) {
        printf("gpu selftest: FAILED -- no GPU device. Nothing below could "
               "have run.\n");
        return 1;
    }

    /*
     * Our own window. The guest has not made one at this point, and borrowing
     * a window this test did not create would leave it claimed afterwards.
     *
     * SHOWN, not hidden, and that is not cosmetic: the first version passed
     * SDL_WINDOW_HIDDEN so as not to disturb a headless harness, and every
     * frame then came back with no swapchain texture -- 3 of 3 skipped, 0
     * presented. A hidden window has nothing to present to, so SDL has no
     * image to hand out. The test caught it on its first run, which is the
     * argument for the test existing. It is on screen for well under a
     * second.
     */
    w = SDL_CreateWindow("igvk selftest", 320, 240, 0);
    if (!w) {
        /*
         * SKIP, not FAIL: no display is a property of where this ran, not of
         * the renderer. Distinct from the no-GPU case above, which stays a
         * FAILURE -- a machine with no Vulkan cannot run this port at all,
         * and skipping would hide exactly the thing worth knowing.
         */
        printf("gpu selftest: SKIPPED -- no window could be created (%s), so "
               "there is no surface to present to. The GPU device WAS created, "
               "so this is the environment, not the renderer. Nothing below "
               "was checked.\n", SDL_GetError());
        gpu_device_destroy();
        return 77;
    }
    if (!gpu_device_attach_window(w)) {
        printf("gpu selftest: FAILED -- the swapchain could not be claimed "
               "on a window that was created successfully.\n");
        SDL_DestroyWindow(w);
        gpu_device_destroy();
        return 1;
    }

    for (i = 0; i < FRAMES; i++) {
        if (!gpu_frame_begin()) continue;      /* counted by the device */
        began++;
        /* A different colour each frame, so a frame that is silently reused
           rather than re-cleared would be visible to a capture. */
        gpu_frame_clear(1u, i == 0 ? 1.0f : 0.0f, i == 1 ? 1.0f : 0.0f,
                         i == 2 ? 1.0f : 0.0f, 1.0f, 1.0f, 0u);
        gpu_frame_viewport(0, 0, 320, 240, 0.0f, 1.0f);
        if (!gpu_frame_in_progress()) {
            printf("gpu selftest: FAILED -- frame %d reported no frame in "
                   "progress between begin and end.\n", i);
            break;
        }
        gpu_frame_end();
        if (gpu_frame_in_progress()) {
            printf("gpu selftest: FAILED -- frame %d was still open after "
                   "gpu_frame_end.\n", i);
            break;
        }
        presented_ok++;
    }

    gpu_device_report();
    gpu_device_attach_window(NULL);
    SDL_DestroyWindow(w);
    gpu_device_destroy();

    if (presented_ok != FRAMES) {
        printf("gpu selftest: FAILED -- %d of %d frames completed (%d were "
               "begun). A frame path that cannot present in isolation will "
               "not present under the engine either.\n",
               presented_ok, FRAMES, began);
        return 1;
    }
    printf("gpu selftest: PASSED -- %d frames acquired, cleared and "
           "presented.\n"
           "  This proves the HOST half only. Whether the engine's own order "
           "of\n"
           "  beginDraw / clearRenderDestination / setViewport / endDraw "
           "produces the\n"
           "  right picture is not tested here and cannot be until a real run "
           "reaches a frame.\n", presented_ok);
    return 0;
#endif
}

/* ---- the draw path, proved by reading the pixels back ------------------ */

/*
 * A frame path that presents and a frame path that DRAWS are different
 * claims, and the first has been true here for a while without the second.
 * So this does not check that gpu_draw returned 1 -- that is a mechanism
 * check. It renders into an off-screen target, copies it back, and looks at
 * the pixels.
 *
 * Designed around its NEGATIVE: the target is cleared to a colour that is not
 * the triangle's, so "the triangle is there" and "the clear is there" cannot
 * be confused, and the test fails if the centre is still the clear colour.
 * Two corners are checked too -- a shader that filled the whole target would
 * otherwise pass.
 */
#define OFF_W 64
#define OFF_H 64

#ifdef X2_WITH_SDL
static int px_is(const uint32_t *img, int x, int y, uint32_t bgra,
                 const char *what)
{
    uint32_t got = img[(size_t)y * OFF_W + x];
    if (got == bgra) return 1;
    printf("gpu selftest: FAILED -- pixel (%d,%d) is 0x%08x, expected 0x%08x "
           "(%s)\n", x, y, got, bgra, what);
    return 0;
}
#endif

/*
 * A clear that arrives AFTER drawing has begun.
 *
 * SDL_GPU clears on pass entry only, so this used to be counted and dropped --
 * 3,833 times in one gameplay run. The pass is now ended and reopened with the
 * clear as its load op, and the interesting claim is not "the clear happened"
 * but "the clear happened AND what was already drawn survived it". A reopen
 * that got the load ops wrong would erase the frame and still pass a test that
 * only checked the cleared thing.
 *
 * So: draw red, clear DEPTH ONLY, and require the red to still be there; then
 * clear COLOUR and require it to be gone. Either check alone is passed by a
 * wrong implementation -- the first by dropping the clear entirely (the old
 * behaviour), the second by restarting the frame.
 */
int gpu_midframe_clear_selftest(void)
{
#ifndef X2_WITH_SDL
    printf("gpu mid-frame clear selftest: SKIPPED -- built without SDL. This "
           "is not a pass.\n");
    return 77;
#else
    struct { float x, y, z, rhw; uint32_t color; } tri[3] = {
        { OFF_W * 0.5f,  2.0f,          0.5f, 1.0f, 0xFFFF0000u },
        { OFF_W - 2.0f,  OFF_H - 2.0f,  0.5f, 1.0f, 0xFFFF0000u },
        { 2.0f,          OFF_H - 2.0f,  0.5f, 1.0f, 0xFFFF0000u }
    };
    static uint32_t img[OFF_W * OFF_H];
    GpuBuffer vb;
    GpuDraw d;
    int fails = 0;

    printf("\n=== gpu mid-frame clear selftest: the clear happens and the "
           "frame survives it ===\n");
    if (!gpu_device_create()) {
        printf("gpu mid-frame clear selftest: FAILED -- no GPU device.\n");
        return 1;
    }
    vb = gpu_buffer_create(GPU_BUF_VERTEX, sizeof tri);
    if (!vb || !gpu_buffer_upload(vb, 0, tri, sizeof tri)) {
        printf("gpu mid-frame clear selftest: FAILED -- no vertex buffer.\n");
        gpu_device_destroy();
        return 1;
    }
    memset(&d, 0, sizeof d);
    d.vertices = vb;
    d.vertex_stride = sizeof tri[0];
    d.prim = GPU_PRIM_TRIANGLELIST;
    d.prim_count = 1;
    d.pos_offset = 0;
    d.pretransformed = 1;
    d.color_offset = 16;
    d.uv_offset = -1;
    d.texop = GPU_TEXOP_NONE;
    d.cull = GPU_CULL_NONE;
    d.depth_func = GPU_CMP_ALWAYS;

    /* --- depth-only clear mid-frame: the red must survive --- */
    if (!gpu_offscreen_begin(OFF_W, OFF_H, 0.0f, 0.0f, 1.0f, 1.0f)
        || !gpu_draw(&d)) {
        printf("gpu mid-frame clear selftest: FAILED -- the first draw did "
               "not happen, so nothing was compared.\n");
        gpu_offscreen_end();
        gpu_device_destroy();
        return 1;
    }
    gpu_frame_clear(2u, 0.0f, 1.0f, 0.0f, 1.0f, 1.0f, 0u);   /* depth only */
    if (!gpu_offscreen_read(img, sizeof img)) {
        printf("gpu mid-frame clear selftest: FAILED -- no read back.\n");
        gpu_offscreen_end();
        gpu_device_destroy();
        return 1;
    }
    gpu_offscreen_end();
    fails += !px_is(img, OFF_W / 2, OFF_H / 2, 0xFFFF0000u,
                    "the triangle, which a DEPTH-only clear must not erase");
    fails += !px_is(img, 1, 1, 0xFF0000FFu,
                    "the background, which a depth-only clear must also "
                    "leave alone");

    /* --- colour clear mid-frame: the red must be gone --- */
    if (!gpu_offscreen_begin(OFF_W, OFF_H, 0.0f, 0.0f, 1.0f, 1.0f)
        || !gpu_draw(&d)) {
        printf("gpu mid-frame clear selftest: FAILED -- the second draw did "
               "not happen.\n");
        gpu_offscreen_end();
        gpu_device_destroy();
        return 1;
    }
    gpu_frame_clear(1u, 0.0f, 1.0f, 0.0f, 1.0f, 1.0f, 0u);   /* colour green */
    if (!gpu_offscreen_read(img, sizeof img)) {
        printf("gpu mid-frame clear selftest: FAILED -- no read back.\n");
        gpu_offscreen_end();
        gpu_device_destroy();
        return 1;
    }
    gpu_offscreen_end();
    fails += !px_is(img, OFF_W / 2, OFF_H / 2, 0xFF00FF00u,
                    "the middle after a mid-frame COLOUR clear to green -- "
                    "still red here means the clear was dropped");

    gpu_device_destroy();
    printf("gpu mid-frame clear selftest: %s\n", fails ? "FAILED"
           : "PASSED -- a depth-only clear leaves the picture alone and a "
             "colour clear replaces it");
    return fails ? 1 : 0;
#endif
}

/*
 * CUBE SAMPLING BY A GENERATED DIRECTION.
 *
 * The engine's environment-mapped characters hand this stage an FVF of
 * position and normal with NO texture coordinates at all, and a cube map. The
 * direction is generated from the normal (or the reflection about it) in
 * camera space, so the claim being tested is not "a cube can be sampled" but
 * "the direction VARIES with the normal".
 *
 * Hence two draws whose only difference is the normal, which must come back as
 * two DIFFERENT faces. A shader that sampled a constant direction, or face 0,
 * or read the position's bytes as a coordinate, gives the same answer twice
 * and fails -- where a single-draw test would pass all three.
 *
 * The third check is the refusal: with no generator, the same cube must be
 * REFUSED rather than sampled with whatever the UVs hold.
 */
int gpu_cube_texgen_selftest(void)
{
#ifndef X2_WITH_SDL
    printf("gpu cube texgen selftest: SKIPPED -- built without SDL. This is "
           "not a pass.\n");
    return 77;
#else
    struct Vtx { float x, y, z, rhw, nx, ny, nz; };
    static struct Vtx quad[6];
    static uint32_t img[OFF_W * OFF_H];
    /* One texel per face, in D3D8's face order: +X, -X, +Y, -Y, +Z, -Z. */
    static const uint32_t FACE[6] = {
        0xFFFF0000u,   /* +X red    */
        0xFF00FF00u,   /* -X green  */
        0xFF0000FFu,   /* +Y blue   */
        0xFFFFFF00u,   /* -Y yellow */
        0xFFFF00FFu,   /* +Z magenta*/
        0xFF00FFFFu    /* -Z cyan   */
    };
    static const float NRM[2][3] = { { 1.0f, 0.0f, 0.0f },
                                     { 0.0f, 0.0f, -1.0f } };
    static const uint32_t WANT[2] = { 0xFFFF0000u, 0xFF00FFFFu };  /* +X, -Z */
    static const float IDENT[16] = {1,0,0,0, 0,1,0,0, 0,0,1,0, 0,0,0,1};
    GpuBuffer vb;
    GpuTexture cube;
    GpuDraw d;
    uint32_t got[2];
    int fails = 0, i, k;

    printf("\n=== gpu cube texgen selftest: the direction must follow the "
           "normal ===\n");
    if (!gpu_device_create()) {
        printf("gpu cube texgen selftest: FAILED -- no GPU device, so NOTHING "
               "about cube sampling was checked.\n");
        return 1;
    }
    cube = gpu_texture_create_cube(1, GPU_FMT_BGRA8, 1);
    if (!cube) {
        printf("gpu cube texgen selftest: FAILED -- no cube texture.\n");
        gpu_device_destroy();
        return 1;
    }
    for (i = 0; i < 6; i++)
        if (!gpu_texture_upload_face(cube, (uint32_t)i, 0, &FACE[i], 4)) {
            printf("gpu cube texgen selftest: FAILED -- face %d would not "
                   "upload.\n", i);
            gpu_device_destroy();
            return 1;
        }
    vb = gpu_buffer_create(GPU_BUF_VERTEX, sizeof quad);
    if (!vb) {
        printf("gpu cube texgen selftest: FAILED -- no vertex buffer.\n");
        gpu_device_destroy();
        return 1;
    }

    for (i = 0; i < 2; i++) {
        static const float X[6] = { 0.0f, (float)OFF_W, 0.0f,
                                    0.0f, (float)OFF_W, (float)OFF_W };
        static const float Y[6] = { 0.0f, 0.0f, (float)OFF_H,
                                    (float)OFF_H, 0.0f, (float)OFF_H };
        for (k = 0; k < 6; k++) {
            quad[k].x = X[k]; quad[k].y = Y[k];
            quad[k].z = 0.5f; quad[k].rhw = 1.0f;
            quad[k].nx = NRM[i][0];
            quad[k].ny = NRM[i][1];
            quad[k].nz = NRM[i][2];
        }
        if (!gpu_buffer_upload(vb, 0, quad, sizeof quad)) {
            printf("gpu cube texgen selftest: FAILED -- upload.\n");
            gpu_device_destroy();
            return 1;
        }
        memset(&d, 0, sizeof d);
        d.vertices = vb;
        d.vertex_stride = sizeof quad[0];
        d.prim = GPU_PRIM_TRIANGLELIST;
        d.prim_count = 2;
        d.pos_offset = 0;
        d.pretransformed = 1;
        d.color_offset = -1;
        d.uv_offset = -1;
        d.normal_offset = 16;
        d.texture = cube;
        d.texop = GPU_TEXOP_SELECT_TEXTURE;
        d.texgen = GPU_TEXGEN_CAMERA_NORMAL;
        memcpy(d.worldview, IDENT, sizeof d.worldview);
        d.cull = GPU_CULL_NONE;
        d.depth_func = GPU_CMP_ALWAYS;

        if (!gpu_offscreen_begin(OFF_W, OFF_H, 0.0f, 0.0f, 0.0f, 1.0f)
            || !gpu_draw(&d)
            || !gpu_offscreen_read(img, sizeof img)) {
            printf("gpu cube texgen selftest: FAILED -- draw %d did not "
                   "happen, so nothing was compared.\n", i);
            gpu_offscreen_end();
            gpu_device_destroy();
            return 1;
        }
        gpu_offscreen_end();
        got[i] = img[(OFF_H / 2) * OFF_W + OFF_W / 2];
        if (got[i] != WANT[i]) {
            printf("gpu cube texgen selftest: FAILED -- a normal of "
                   "(%g,%g,%g) sampled 0x%08x; the face pointing that way is "
                   "0x%08x.\n", NRM[i][0], NRM[i][1], NRM[i][2], got[i],
                   WANT[i]);
            fails++;
        }
    }
    if (got[0] == got[1]) {
        printf("gpu cube texgen selftest: FAILED -- both normals sampled the "
               "same texel (0x%08x). The direction does not follow the "
               "normal, so a constant direction, face 0, or the position's "
               "bytes read as a coordinate would all pass the checks "
               "above.\n", got[0]);
        fails++;
    }

    /* The refusal: no generator means no direction, and face 0 is not an
       answer. */
    d.texgen = GPU_TEXGEN_NONE;
    if (gpu_offscreen_begin(OFF_W, OFF_H, 0.0f, 0.0f, 0.0f, 1.0f)) {
        if (gpu_draw(&d)) {
            printf("gpu cube texgen selftest: FAILED -- a cube with NO "
                   "texture-coordinate generator was DRAWN. There is no "
                   "direction to sample it with; it must be refused.\n");
            fails++;
        }
        gpu_offscreen_end();
    }

    gpu_device_destroy();
    printf("gpu cube texgen selftest: %s\n", fails ? "FAILED"
           : "PASSED -- two normals sample two different faces, and a cube "
             "with no generator is refused");
    return fails ? 1 : 0;
#endif
}

int gpu_draw_selftest(void)
{
#ifndef X2_WITH_SDL
    printf("gpu draw selftest: SKIPPED -- built without SDL. This is not a "
           "pass.\n");
    return 77;
#else
    /* A big clockwise triangle covering the middle, pre-transformed so no
       matrix is involved: this is testing the draw path, not the maths. */
    struct { float x, y, z, rhw; uint32_t color; } tri[3] = {
        { OFF_W * 0.5f,  2.0f,          0.0f, 1.0f, 0xFFFF0000u },
        { OFF_W - 2.0f,  OFF_H - 2.0f,  0.0f, 1.0f, 0xFFFF0000u },
        { 2.0f,          OFF_H - 2.0f,  0.0f, 1.0f, 0xFFFF0000u }
    };
    static uint32_t img[OFF_W * OFF_H];
    GpuBuffer vb;
    GpuDraw d;
    int fails = 0;

    printf("\n=== gpu draw selftest: geometry, with no engine involved ===\n");
    if (!gpu_device_create()) {
        printf("gpu draw selftest: FAILED -- no GPU device.\n");
        return 1;
    }
    vb = gpu_buffer_create(GPU_BUF_VERTEX, sizeof tri);
    if (!vb || !gpu_buffer_upload(vb, 0, tri, sizeof tri)) {
        printf("gpu draw selftest: FAILED -- the vertex buffer could not be "
               "made or filled.\n");
        gpu_device_destroy();
        return 1;
    }
    /* Cleared to opaque BLUE; the triangle is opaque RED. Neither can be
       mistaken for the other, or for uninitialised memory. */
    if (!gpu_offscreen_begin(OFF_W, OFF_H, 0.0f, 0.0f, 1.0f, 1.0f)) {
        printf("gpu draw selftest: FAILED -- no off-screen target.\n");
        gpu_device_destroy();
        return 1;
    }

    memset(&d, 0, sizeof d);
    d.vertices = vb;
    d.vertex_stride = sizeof tri[0];
    d.prim = GPU_PRIM_TRIANGLELIST;
    d.prim_count = 1;
    d.pos_offset = 0;
    d.pretransformed = 1;
    d.color_offset = 16;
    d.uv_offset = -1;
    d.texop = GPU_TEXOP_NONE;
    d.cull = GPU_CULL_NONE;
    d.depth_func = GPU_CMP_ALWAYS;
    if (!gpu_draw(&d)) {
        printf("gpu draw selftest: FAILED -- the draw was refused.\n");
        gpu_offscreen_end();
        gpu_device_destroy();
        return 1;
    }
    if (!gpu_offscreen_read(img, sizeof img)) {
        printf("gpu draw selftest: FAILED -- the target could not be read "
               "back, so nothing about the pixels is known.\n");
        gpu_offscreen_end();
        gpu_device_destroy();
        return 1;
    }
    gpu_offscreen_end();

    /* B8G8R8A8 in memory, read as a little-endian uint32 -> 0xAARRGGBB. */
    fails += !px_is(img, OFF_W / 2, OFF_H / 2, 0xFFFF0000u,
                    "the middle of the triangle");
    fails += !px_is(img, 1, 1, 0xFF0000FFu,
                    "a corner OUTSIDE the triangle, which must still be the "
                    "clear colour");
    fails += !px_is(img, OFF_W - 2, 1, 0xFF0000FFu,
                    "the other top corner, outside the triangle");

    gpu_draw_report();
    gpu_device_destroy();
    printf("gpu draw selftest: %s\n", fails ? "FAILED" : "PASSED -- a triangle "
           "was rasterised and read back");
    return fails ? 1 : 0;
#endif
}
