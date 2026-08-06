/*
 * What the layer says about itself at shutdown, and what it proves about
 * itself on demand.
 *
 * Kept apart from the interfaces on purpose: the report is what a reader sees
 * when nothing was drawn, and it has to be able to distinguish "the engine
 * never reached DirectX" from "it reached it and this host did nothing" from
 * "it drew and the frame was empty". Those three look identical on a black
 * screen and are three completely different bugs.
 */
#include "d3d8_host.h"
#include "d3d8_com.h"
#include "d3d8_device.h"
#include "d3d8_caps.h"
#include "d3d8_types.h"
#include "d3d8_resource.h"
#include "d3d8_drawcall.h"
#include "d3d8_state.h"

#include "gpu_device.h"
#include "gpu_draw.h"
#include "x86rt.h"
#include "guest_heap.h"

#include <stdio.h>
#include <string.h>

void d3d8_host_report(void)
{
    if (!d3d8_host_enabled()) {
        printf("\nd3d8: the host Direct3D 8 was NOT enabled this run, so "
               "nothing here was exercised.\n");
        return;
    }
    printf("\n=== host Direct3D 8 ===\n");
    d3d8_device_report();
    d3d8_permissive_report();
    fflush(stdout);
}

/*
 * The caps block's own check.
 *
 * Not "does it look right" -- that cannot fail. It checks the two things that
 * would be silently wrong: that the struct this host writes is the size the
 * guest allocated for it (the game allocates 0xd4 and would be overrun by a
 * byte more), and that the fields land where a D3D8 caller reads them, tested
 * through the offsets rather than the field names.
 */
static int caps_selftest(void)
{
    D3DCAPS8 c;
    D3D8CapsLimits hw;
    const uint32_t *raw = (const uint32_t *)&c;
    int fails = 0;

    d3d8_caps_limits_default(&hw);
    memset(&c, 0xAA, sizeof c);
    d3d8_caps_fill(&c, 0, D3DDEVTYPE_HAL, &hw);

    if (sizeof c != 212) {
        fprintf(stderr, "  FAIL: D3DCAPS8 is %d bytes; the game allocates "
                        "212\n", (int)sizeof c);
        fails++;
    }
    /* DeviceType is dword 0 and MaxTextureWidth is dword 22; reading them
       positionally is what catches a field inserted in the wrong place. */
    if (raw[0] != D3DDEVTYPE_HAL) {
        fprintf(stderr, "  FAIL: dword 0 is 0x%08x, not the device type\n",
                raw[0]);
        fails++;
    }
    if (raw[22] != hw.max_texture_dim) {
        fprintf(stderr, "  FAIL: dword 22 is 0x%08x, not MaxTextureWidth "
                        "(%u)\n", raw[22], hw.max_texture_dim);
        fails++;
    }
    if (raw[49] != 0xFFFE0101u) {
        fprintf(stderr, "  FAIL: dword 49 is 0x%08x, not VertexShaderVersion "
                        "1.1\n", raw[49]);
        fails++;
    }
    /* Nothing may be left as the 0xAA fill: a field the filler forgot is a
       field the engine reads as garbage. memset(0) first would have hidden
       exactly that, which is why the fill is 0xAA. */
    {
        int i, untouched = 0;
        for (i = 0; i < (int)(sizeof c / 4); i++)
            if (raw[i] == 0xAAAAAAAAu) untouched++;
        if (untouched) {
            fprintf(stderr, "  FAIL: %d caps dword(s) were never written by "
                            "d3d8_caps_fill\n", untouched);
            fails++;
        }
    }
    printf("d3d8: caps self-test %s\n", fails ? "FAILED" : "passed");
    return fails;
}

/*
 * The D3D8 half of the draw path, driven THROUGH THE COM VTABLES.
 *
 * Not by calling the C functions directly: the guest reaches this layer by
 * dispatching through a vtable slot, and that is the path that has to work.
 * So the buffer is created, locked, written and unlocked exactly as libIGGfx
 * would, and only then is a draw built from the device state and rasterised
 * into an off-screen target and read back.
 *
 * Its negative is designed first: the target is cleared GREEN and the triangle
 * is RED, so "the triangle drew" cannot be confused with "the clear drew", and
 * a corner outside the triangle is checked so a shader that filled everything
 * would fail.
 */
static uint32_t call_method(D3D8Object *o, int slot, const uint32_t *args,
                            int nargs)
{
    CPU C;
    static uint32_t stack;
    uint32_t vt = d3d8_iface_vtable(d3d8_object_iface(o));
    int i;

    if (!stack) stack = guest_malloc(4096) + 2048;
    memset(&C, 0, sizeof C);
    C.esp = stack - (uint32_t)(nargs + 2) * 4u;
    WR32(C.esp, 0xD3D80000u);                       /* return address */
    WR32(C.esp + 4u, d3d8_object_guest(o));         /* this */
    for (i = 0; i < nargs; i++) WR32(C.esp + 8u + (uint32_t)i * 4u, args[i]);
    x86_dispatch(&C, RD32(vt + (uint32_t)slot * 4u));
    return C.eax;
}

#define TW 64
#define TH 64

static int d3d8_draw_selftest(void)
{
    /* D3DFVF_XYZRHW | D3DFVF_DIFFUSE, and D3DCOLOR is 0xAARRGGBB. */
    struct { float x, y, z, rhw; uint32_t color; } tri[3] = {
        { TW * 0.5f, 2.0f,       0.0f, 1.0f, 0xFFFF0000u },
        { TW - 2.0f, TH - 2.0f,  0.0f, 1.0f, 0xFFFF0000u },
        { 2.0f,      TH - 2.0f,  0.0f, 1.0f, 0xFFFF0000u }
    };
    static uint32_t img[TW * TH];
    D3D8Object *vb;
    D3D8State st;
    D3D8DrawRequest req;
    GpuDraw gd;
    uint32_t args[3], locked = 0, centre, corner;
    int fails = 0;

    printf("\n=== d3d8 draw selftest: through the COM vtables ===\n");
    if (!gpu_device_create()) {
        printf("d3d8 draw selftest: FAILED -- no GPU device.\n");
        return 1;
    }
    d3d8_resource_install();
    vb = d3d8_vertexbuffer_new(sizeof tri, 0, 0x0044u /* XYZRHW|DIFFUSE */, 0);
    if (!vb) {
        printf("d3d8 draw selftest: FAILED -- no vertex buffer.\n");
        gpu_device_destroy();
        return 1;
    }
    d3d8_resource_attach_destructor(vb);

    /* Lock(offset, size, &pointer, flags) -- slot 11 of IDirect3DVertexBuffer8. */
    args[0] = 0; args[1] = 0;
    args[2] = guest_malloc(4);
    call_method(vb, 11, args, 4);
    locked = RD32(args[2]);
    if (!locked) {
        printf("d3d8 draw selftest: FAILED -- Lock returned no pointer.\n");
        gpu_device_destroy();
        return 1;
    }
    memcpy((void *)(uintptr_t)locked, tri, sizeof tri);
    call_method(vb, 12, NULL, 0);                   /* Unlock */

    /* The device state the engine would have set. */
    d3d8_state_reset(&st);
    st.vertex_shader = 0x0044u;
    st.stream[0].guest_ptr = d3d8_object_guest(vb);
    st.stream[0].stride = sizeof tri[0];
    /* Deliberately left at D3D's DEFAULT cull mode (D3DCULL_CCW) rather than
       disabled: the triangle is wound the way the engine winds a front face,
       so this test is also what catches the front-face convention being
       inverted -- which it was. */

    memset(&req, 0, sizeof req);
    req.vertex_buffer = d3d8_resource_buffer(vb);
    req.stride = sizeof tri[0];
    req.primitive_type = 4;                          /* D3DPT_TRIANGLELIST */
    req.primitive_count = 1;
    if (!d3d8_build_draw(&st, &req, &gd)) {
        printf("d3d8 draw selftest: FAILED -- the draw could not be built "
               "from the device state.\n");
        gpu_device_destroy();
        return 1;
    }
    if (!gd.pretransformed || gd.color_offset != 16 || gd.pos_offset != 0) {
        printf("d3d8 draw selftest: FAILED -- FVF 0x0044 decoded to "
               "pos=%d colour=%d pretransformed=%d; expected 0, 16, 1.\n",
               gd.pos_offset, gd.color_offset, gd.pretransformed);
        fails++;
    }
    if (!gpu_offscreen_begin(TW, TH, 0.0f, 1.0f, 0.0f, 1.0f)) {
        printf("d3d8 draw selftest: FAILED -- no off-screen target.\n");
        gpu_device_destroy();
        return 1;
    }
    if (!gpu_draw(&gd)) {
        printf("d3d8 draw selftest: FAILED -- the draw was refused.\n");
        gpu_offscreen_end();
        gpu_device_destroy();
        return 1;
    }
    if (!gpu_offscreen_read(img, sizeof img)) {
        printf("d3d8 draw selftest: FAILED -- nothing could be read back, so "
               "nothing about the pixels is known.\n");
        gpu_offscreen_end();
        gpu_device_destroy();
        return 1;
    }
    gpu_offscreen_end();

    centre = img[(TH / 2) * TW + TW / 2];
    corner = img[1 * TW + 1];
    if (centre != 0xFFFF0000u) {
        printf("d3d8 draw selftest: FAILED -- the centre is 0x%08x, not the "
               "red triangle (0xffff0000)\n", centre);
        fails++;
    }
    if (corner != 0xFF00FF00u) {
        printf("d3d8 draw selftest: FAILED -- the corner is 0x%08x, not the "
               "green clear (0xff00ff00); something filled the whole "
               "target\n", corner);
        fails++;
    }
    gpu_device_destroy();
    printf("d3d8 draw selftest: %s\n", fails ? "FAILED"
           : "PASSED -- a vertex buffer locked and filled through the COM "
             "vtables was rasterised");
    return fails;
}

int d3d8_host_selftest(void)
{
    int fails = 0;
    fails += d3d8_com_selftest();
    fails += caps_selftest();
    fails += d3d8_draw_selftest();
    printf("d3d8: SELF-TEST %s -- %d failure(s)\n",
           fails ? "FAILED" : "PASSED", fails);
    fflush(stdout);
    return fails;
}
