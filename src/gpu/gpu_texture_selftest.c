/*
 * Texture and combiner self-tests: what the sampler and the fixed-function
 * stage actually return, read back pixel by pixel.
 *
 * Separate from gpu_selftest.c, which checks the FRAME path -- create a
 * device, clear, draw, present. These three ask a different question: given a
 * frame that works, does the stage sample the right texel and combine it with
 * the right argument. Each is built around the wrong answer it has to be able
 * to see: a cube direction that does not vary with the normal, a combiner
 * argument that silently falls back to the diffuse colour, and a BC1 texture
 * that decodes to black.
 */
#include "gpu_device.h"
#include "gpu_draw.h"
#include "gpu_selftest_pixels.h"

#include <stdio.h>
#include <string.h>

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

/*
 * D3DTA_TFACTOR as a combiner argument.
 *
 * 12,632 draws a run set COLORARG2 to the texture factor, and this stage used
 * to assume D3D8's default of D3DTA_CURRENT and hand the shader the diffuse
 * colour instead. That is not a missing feature -- it is a wrong colour with
 * nothing to show for it, which is the hardest kind to find from a screenshot.
 *
 * So the test makes the two readings give DIFFERENT answers by construction:
 * an untextured green vertex colour modulated by a HALF-RED texture factor.
 * Reading the factor gives black (green * red = 0); the old behaviour, reading
 * the diffuse for both arguments, gives green squared -- still green. A test
 * whose two readings agreed would prove nothing, so the control draw with
 * GPU_TA_DEFAULT is run too and is required to differ.
 */
int gpu_tfactor_selftest(void)
{
#ifndef X2_WITH_SDL
    printf("gpu tfactor selftest: SKIPPED -- built without SDL. This is not a "
           "pass.\n");
    return 77;
#else
    struct Vtx { float x, y, z, rhw; uint32_t color; };
    static struct Vtx quad[6];
    static uint32_t img[OFF_W * OFF_H];
    GpuBuffer vb;
    GpuTexture white;
    GpuDraw d;
    uint32_t with_factor, with_default;
    int fails = 0, k;

    printf("\n=== gpu tfactor selftest: the factor must reach the "
           "combiner ===\n");
    if (!gpu_device_create()) {
        printf("gpu tfactor selftest: FAILED -- no GPU device, so NOTHING "
               "about the texture factor was checked.\n");
        return 1;
    }
    {
        static const float X[6] = { 0.0f, (float)OFF_W, 0.0f,
                                    0.0f, (float)OFF_W, (float)OFF_W };
        static const float Y[6] = { 0.0f, 0.0f, (float)OFF_H,
                                    (float)OFF_H, 0.0f, (float)OFF_H };
        for (k = 0; k < 6; k++) {
            quad[k].x = X[k]; quad[k].y = Y[k];
            quad[k].z = 0.5f; quad[k].rhw = 1.0f;
            quad[k].color = 0xFF00FF00u;               /* opaque GREEN */
        }
    }
    vb = gpu_buffer_create(GPU_BUF_VERTEX, sizeof quad);
    if (!vb || !gpu_buffer_upload(vb, 0, quad, sizeof quad)) {
        printf("gpu tfactor selftest: FAILED -- no vertex buffer.\n");
        gpu_device_destroy();
        return 1;
    }

    /* A 1x1 WHITE texture, so the texture argument is the identity for a
       modulate and the only thing that can change the pixel is the factor. */
    {
        static const uint32_t px = 0xFFFFFFFFu;
        white = gpu_texture_create(1, 1, GPU_FMT_BGRA8, 1);
        if (!white || !gpu_texture_upload(white, 0, &px, sizeof px)) {
            printf("gpu tfactor selftest: FAILED -- no white texture.\n");
            gpu_device_destroy();
            return 1;
        }
    }

    memset(&d, 0, sizeof d);
    d.vertices = vb;
    d.vertex_stride = sizeof quad[0];
    d.prim = GPU_PRIM_TRIANGLELIST;
    d.prim_count = 2;
    d.pos_offset = 0;
    d.pretransformed = 1;
    d.color_offset = 16;
    d.uv_offset = -1;
    d.normal_offset = -1;
    d.texture = white;
    d.texop = GPU_TEXOP_MODULATE;
    d.cull = GPU_CULL_NONE;
    d.depth_func = GPU_CMP_ALWAYS;
    d.texture_factor[0] = 1.0f;        /* RED, with no green in it */
    d.texture_factor[1] = 0.0f;
    d.texture_factor[2] = 0.0f;
    d.texture_factor[3] = 1.0f;

    for (k = 0; k < 2; k++) {
        /* k=0: ARG1 diffuse, ARG2 the FACTOR. k=1: the defaults, which is
           what this stage did before the factor was read at all. */
        d.color_arg1 = k ? GPU_TA_DEFAULT : GPU_TA_DIFFUSE;
        d.color_arg2 = k ? GPU_TA_DEFAULT : GPU_TA_TFACTOR;
        if (!gpu_offscreen_begin(OFF_W, OFF_H, 0.0f, 0.0f, 1.0f, 1.0f)
            || !gpu_draw(&d)
            || !gpu_offscreen_read(img, sizeof img)) {
            printf("gpu tfactor selftest: FAILED -- draw %d did not happen.\n",
                   k);
            gpu_offscreen_end();
            gpu_device_destroy();
            return 1;
        }
        gpu_offscreen_end();
        if (k) with_default = img[(OFF_H / 2) * OFF_W + OFF_W / 2];
        else   with_factor  = img[(OFF_H / 2) * OFF_W + OFF_W / 2];
    }

    /* Green modulated by a factor with no green in it is BLACK. */
    if (with_factor != 0xFF000000u) {
        printf("gpu tfactor selftest: FAILED -- green modulated by a red "
               "texture factor came back 0x%08x, not black. The factor is not "
               "reaching the combiner.\n", with_factor);
        fails++;
    }
    if (with_factor == with_default) {
        printf("gpu tfactor selftest: FAILED -- naming the texture factor and "
               "leaving the arguments at their defaults give the SAME pixel "
               "(0x%08x), so this test cannot tell the two apart and the "
               "check above proves nothing.\n", with_factor);
        fails++;
    }

    gpu_device_destroy();
    printf("gpu tfactor selftest: %s\n", fails ? "FAILED"
           : "PASSED -- the texture factor reaches the combiner, and it "
             "differs from the default arguments");
    return fails ? 1 : 0;
#endif
}

/*
 * BC1 is the game's DXT1 source format. A device advertising the format but
 * returning a black texel is not usable by this port, so sample the texture
 * through the shipping fixed-function shader and read the resulting pixel.
 */
int gpu_bc1_texture_selftest(void)
{
#ifndef X2_WITH_SDL
    printf("gpu BC1 texture selftest: SKIPPED -- built without SDL. This is "
           "not a pass.\n");
    return 77;
#else
    struct Vertex { float x, y, z, rhw, u, v; };
    static const struct Vertex quad[6] = {
        {0, 0, 0.5f, 1, .5f, .5f}, {OFF_W, 0, 0.5f, 1, .5f, .5f},
        {0, OFF_H, 0.5f, 1, .5f, .5f},
        {0, OFF_H, 0.5f, 1, .5f, .5f}, {OFF_W, 0, 0.5f, 1, .5f, .5f},
        {OFF_W, OFF_H, 0.5f, 1, .5f, .5f}
    };
    /* One opaque-red DXT1 block: c0 = RGB565 red, every index selects c0. */
    static const uint8_t red_block[8] = {0x00, 0xf8, 0, 0, 0, 0, 0, 0};
    static uint32_t image[OFF_W * OFF_H];
    GpuBuffer vertices;
    GpuTexture texture;
    GpuDraw draw;
    uint32_t centre;

    printf("\n=== gpu BC1 texture selftest: compressed texel through the "
           "shipping shader ===\n");
    if (!gpu_device_create()) {
        printf("gpu BC1 texture selftest: FAILED -- no GPU device.\n");
        return 1;
    }
    vertices = gpu_buffer_create(GPU_BUF_VERTEX, sizeof quad);
    texture = gpu_texture_create(4, 4, GPU_FMT_BC1, 1);
    if (!vertices || !texture
        || !gpu_buffer_upload(vertices, 0, quad, sizeof quad)
        || !gpu_texture_upload(texture, 0, red_block, sizeof red_block)) {
        printf("gpu BC1 texture selftest: FAILED -- could not create and "
               "upload the DXT1 control block.\n");
        if (texture) gpu_texture_destroy(texture);
        if (vertices) gpu_buffer_destroy(vertices);
        gpu_device_destroy();
        return 1;
    }
    memset(&draw, 0, sizeof draw);
    draw.vertices = vertices;
    draw.vertex_stride = sizeof quad[0];
    draw.prim = GPU_PRIM_TRIANGLELIST;
    draw.prim_count = 2;
    draw.pos_offset = 0;
    draw.pretransformed = 1;
    draw.color_offset = -1;
    draw.specular_offset = -1;
    draw.uv_offset = 16;
    draw.normal_offset = -1;
    draw.texture = texture;
    draw.texop = GPU_TEXOP_SELECT_TEXTURE;
    draw.cull = GPU_CULL_NONE;
    draw.depth_func = GPU_CMP_ALWAYS;
    if (!gpu_offscreen_begin(OFF_W, OFF_H, 0.0f, 0.0f, 1.0f, 1.0f)
        || !gpu_draw(&draw)
        || !gpu_offscreen_read(image, sizeof image)) {
        printf("gpu BC1 texture selftest: FAILED -- draw/readback did not "
               "complete.\n");
        gpu_offscreen_end();
        gpu_texture_destroy(texture);
        gpu_buffer_destroy(vertices);
        gpu_device_destroy();
        return 1;
    }
    gpu_offscreen_end();
    centre = image[(OFF_H / 2) * OFF_W + OFF_W / 2];
    gpu_texture_destroy(texture);
    gpu_buffer_destroy(vertices);
    gpu_device_destroy();
    if (centre != 0xFFFF0000u) {
        printf("gpu BC1 texture selftest: FAILED -- centre is 0x%08x, "
               "expected opaque red. The device advertised BC1 but the "
               "shipping texture path did not produce its texel.\n", centre);
        return 1;
    }
    printf("gpu BC1 texture selftest: PASSED -- uploaded DXT1 sampled as "
           "opaque red.\n");
    return 0;
#endif
}
