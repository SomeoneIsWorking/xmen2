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
#include "d3d8_surface.h"
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

/*
 * The pixel-shader handle, driven through the real device vtable.
 *
 * The engine's first call is `SetPixelShader(0)` -- MEASURED, not assumed: the
 * unimplemented-method report prints the pushed arguments, and this is what it
 * printed. Handle 0 is D3D8 for "no pixel shader, use the fixed-function
 * pipeline", which this backend implements, so answering it is faithful.
 *
 * A NON-ZERO handle is the case that must not quietly pass. Nothing here can
 * create one -- CreatePixelShader is unimplemented, so a handle cannot exist --
 * and if a shader ever were bound, the fixed-function draw path would silently
 * render something the engine did not ask for. The test that matters is
 * therefore the refusal, and that the refusal leaves the state alone.
 *
 * Needs no GPU and no game: it is entirely about the handle and the stack.
 */
static int pixel_shader_selftest(void)
{
    D3D8Object *dev;
    uint32_t args[1], out, hr;
    int fails = 0;

    printf("\n=== d3d8 pixel-shader selftest: through the device vtable ===\n");
    d3d8_device_install();
    dev = d3d8_object_new(D3D8_IF_IDirect3DDevice8, NULL);
    if (!dev) {
        printf("d3d8 ps selftest: FAILED -- no device object.\n");
        return 1;
    }
    out = guest_malloc(4);

    args[0] = 0;
    hr = call_method(dev, 88, args, 1);                  /* SetPixelShader(0) */
    if (hr != D3D_OK) {
        printf("d3d8 ps selftest: FAILED -- SetPixelShader(0) returned "
               "0x%08x, not D3D_OK. Handle 0 IS the fixed-function "
               "pipeline.\n", hr);
        fails++;
    }

    WR32(out, 0xA5A5A5A5u);
    args[0] = out;
    hr = call_method(dev, 89, args, 1);                  /* GetPixelShader */
    if (hr != D3D_OK || RD32(out) != 0) {
        printf("d3d8 ps selftest: FAILED -- GetPixelShader returned 0x%08x "
               "and wrote 0x%08x; expected D3D_OK and 0.\n", hr, RD32(out));
        fails++;
    }

    args[0] = 0xDEADBEEFu;
    hr = call_method(dev, 88, args, 1);                  /* a handle nobody made */
    if (hr != D3DERR_INVALIDCALL) {
        printf("d3d8 ps selftest: FAILED -- SetPixelShader(0xdeadbeef) "
               "returned 0x%08x. A handle this host never created must be "
               "REFUSED, or the fixed-function path would draw in place of a "
               "shader nobody would see was missing.\n", hr);
        fails++;
    }

    WR32(out, 0xA5A5A5A5u);
    args[0] = out;
    call_method(dev, 89, args, 1);
    if (RD32(out) != 0) {
        printf("d3d8 ps selftest: FAILED -- the refused handle 0x%08x was "
               "recorded anyway.\n", RD32(out));
        fails++;
    }

    args[0] = 0;
    hr = call_method(dev, 89, args, 1);                  /* GetPixelShader(NULL) */
    if (hr != D3DERR_INVALIDCALL) {
        printf("d3d8 ps selftest: FAILED -- GetPixelShader(NULL) returned "
               "0x%08x, not D3DERR_INVALIDCALL.\n", hr);
        fails++;
    }

    printf("d3d8 ps selftest: %s\n", fails ? "FAILED"
           : "PASSED -- handle 0 selects the fixed-function pipeline, and a "
             "handle this host never created is refused");
    return fails;
}

/*
 * The gamma ramp, driven through the real device vtable.
 *
 * The engine calls SetGammaRamp(0, <ramp>) during renderer init -- measured,
 * from the unimplemented-method report. A D3DGAMMARAMP is three arrays of 256
 * 16-bit entries, one per channel.
 *
 * This backend cannot programme a hardware gamma ramp: it presents through a
 * Vulkan swapchain and there is no ramp to set. So what matters is not that
 * the call returns, but that the layer knows and SAYS whether the ramp it was
 * given would have changed anything -- an IDENTITY ramp costs nothing to
 * ignore, a curved one is a visible difference from the original game. The
 * test therefore checks the round trip AND the identity verdict, in both
 * directions: a discriminator only trusted after it has been run against both
 * classes has been run against neither.
 */
static void write_ramp(uint32_t base, int curved)
{
    int ch, i;
    for (ch = 0; ch < 3; ch++)
        for (i = 0; i < 256; i++) {
            unsigned v = (unsigned)i * 257u;         /* the identity ramp */
            if (curved && i == 128) v = 0u;
            WR16(base + (uint32_t)(ch * 256 + i) * 2u, (uint16_t)v);
        }
}

static int gamma_selftest(void)
{
    D3D8Object *dev;
    uint32_t args[2], ramp, back;
    int fails = 0, i;

    printf("\n=== d3d8 gamma selftest: through the device vtable ===\n");
    d3d8_device_install();
    dev = d3d8_object_new(D3D8_IF_IDirect3DDevice8, NULL);
    ramp = guest_malloc(3 * 256 * 2);
    back = guest_malloc(3 * 256 * 2);

    write_ramp(ramp, 0);
    args[0] = 0; args[1] = ramp;
    call_method(dev, 18, args, 2);                       /* SetGammaRamp */
    if (d3d8_device_gamma_curved()) {
        printf("d3d8 gamma selftest: FAILED -- an IDENTITY ramp was reported "
               "as curved, so the warning fires on every run and means "
               "nothing.\n");
        fails++;
    }

    memset((void *)(uintptr_t)back, 0xA5, 3 * 256 * 2);
    args[0] = back;
    call_method(dev, 19, args, 1);                       /* GetGammaRamp */
    for (i = 0; i < 3 * 256; i++)
        if (RD16(back + (uint32_t)i * 2u) != RD16(ramp + (uint32_t)i * 2u)) {
            printf("d3d8 gamma selftest: FAILED -- entry %d came back as "
                   "0x%04x, not 0x%04x.\n", i,
                   RD16(back + (uint32_t)i * 2u), RD16(ramp + (uint32_t)i * 2u));
            fails++;
            break;
        }

    write_ramp(ramp, 1);                                 /* one entry bent */
    args[0] = 0; args[1] = ramp;
    call_method(dev, 18, args, 2);
    if (!d3d8_device_gamma_curved()) {
        printf("d3d8 gamma selftest: FAILED -- a ramp with a bent entry was "
               "called identity. Then a game that darkens the screen through "
               "gamma would do it silently and nothing would say so.\n");
        fails++;
    }

    args[0] = 0; args[1] = 0;
    call_method(dev, 18, args, 2);                       /* a NULL ramp */
    printf("d3d8 gamma selftest: %s\n", fails ? "FAILED"
           : "PASSED -- the ramp round-trips, and identity is told apart from "
             "curved in both directions");
    return fails;
}

/*
 * IDirect3DDevice8::GetDirect3D -- hand the engine back the object that made
 * this device, with a reference of its own.
 *
 * It was refused, on the grounds that the AddRef could not be balanced. That
 * was wrong twice over: the object layer refcounts already (Direct3DCreate8
 * AddRefs the singleton on every repeat call, exactly as the real one does),
 * and REFUSING is not free -- the engine writes the returned pointer and uses
 * it, so a failed HRESULT with a NULL out-pointer became a SIGSEGV at (nil)
 * one call later.
 *
 * The two cases are both real and are both checked, because they are answered
 * differently: with no IDirect3D8 in existence there is nothing to hand back
 * and INVALIDCALL with a NULL out-pointer is the truth; with one, D3D_OK and a
 * counted reference.
 */
static int getdirect3d_selftest(void)
{
    D3D8Object *dev;
    uint32_t args[1], out, hr, before;
    int fails = 0;

    printf("\n=== d3d8 GetDirect3D selftest: through the device vtable ===\n");
    d3d8_device_install();
    dev = d3d8_object_new(D3D8_IF_IDirect3DDevice8, NULL);
    out = guest_malloc(4);

    /* No Direct3DCreate8 has run in this process. */
    WR32(out, 0xA5A5A5A5u);
    args[0] = out;
    hr = call_method(dev, 6, args, 1);
    if (d3d8_the_direct3d8() == 0) {
        if (hr != D3DERR_INVALIDCALL || RD32(out) != 0) {
            printf("d3d8 GetDirect3D selftest: FAILED -- with no IDirect3D8 in "
                   "existence it returned 0x%08x and wrote 0x%08x; expected "
                   "INVALIDCALL and a NULL out-pointer.\n", hr, RD32(out));
            fails++;
        }
    }

    /* And with one. Created directly rather than through the import, so this
       needs no guest and no CPU state. */
    before = d3d8_the_direct3d8_refs();
    d3d8_the_direct3d8_ensure();
    WR32(out, 0xA5A5A5A5u);
    args[0] = out;
    hr = call_method(dev, 6, args, 1);
    if (hr != D3D_OK || RD32(out) != d3d8_the_direct3d8()) {
        printf("d3d8 GetDirect3D selftest: FAILED -- returned 0x%08x and wrote "
               "0x%08x; expected D3D_OK and the IDirect3D8 at 0x%08x.\n",
               hr, RD32(out), d3d8_the_direct3d8());
        fails++;
    }
    if (d3d8_the_direct3d8_refs() <= before) {
        printf("d3d8 GetDirect3D selftest: FAILED -- the reference count did "
               "not rise (%u then %u). The engine will Release what it was "
               "given, and an unbalanced count makes teardown fail with "
               "nothing to explain it.\n", before, d3d8_the_direct3d8_refs());
        fails++;
    }
    printf("d3d8 GetDirect3D selftest: %s\n", fails ? "FAILED"
           : "PASSED -- the IDirect3D8 is handed back with a reference of its "
             "own, and refused honestly when there is none");
    return fails;
}

/*
 * IDirect3DTexture8::GetSurfaceLevel, driven through the real texture vtable.
 *
 * The thing that must not pass is a surface that LOOKS right and owns its own
 * pixels: every check below would still be satisfiable by one -- it has a size,
 * a pitch, an HRESULT -- except the two that matter. So those are the ones
 * designed first:
 *
 *   the level surface's Lock pointer IS the texture's own staging pointer for
 *   that level (a private buffer would give a different address), and
 *
 *   unlocking the surface UPLOADS that level (a counter, incremented only when
 *   the backend accepted the copy -- "returned D3D_OK" and "the texture
 *   changed" are otherwise indistinguishable, and this backend cannot read a
 *   texture back to check the pixels).
 *
 * The level is deliberately 1, not 0: a surface that describes and uploads
 * level 0 whatever it was asked for is the natural way to get this wrong, and
 * level 0 would hide it. 64x32 with 3 levels makes level 1 32x16, so the
 * dimensions are wrong in both axes if the level is ignored.
 */
static int texture_level_selftest(void)
{
    D3D8Object *tex;
    D3D8Object *s0, *s1;
    uint32_t args[2], out, desc, lr, hr, g0, g0b, g1, texptr, before;
    unsigned long uploads;
    int fails = 0;

    printf("\n=== d3d8 texture-level selftest: through the texture vtable ===\n");
    if (!gpu_device_create()) {
        printf("d3d8 texlevel selftest: FAILED -- no GPU device, so NOTHING "
               "about GetSurfaceLevel was checked.\n");
        return 1;
    }
    d3d8_resource_install();
    d3d8_surface_install();
    tex = d3d8_texture_new(64, 32, 3, 0, D3DFMT_A8R8G8B8, 0);
    if (!tex) {
        printf("d3d8 texlevel selftest: FAILED -- no texture.\n");
        gpu_device_destroy();
        return 1;
    }
    d3d8_resource_attach_destructor(tex);
    out = guest_malloc(4);
    desc = guest_malloc(32);
    lr = guest_malloc(8);

    /* GetSurfaceLevel is slot 15; (level, ppSurfaceLevel). */
    before = (uint32_t)d3d8_object_refs(tex);
    args[0] = 0; args[1] = out;
    WR32(out, 0xA5A5A5A5u);
    hr = call_method(tex, 15, args, 2);
    g0 = RD32(out);
    if (hr != D3D_OK || !g0 || !d3d8_object_from_guest(g0)) {
        printf("d3d8 texlevel selftest: FAILED -- GetSurfaceLevel(0) returned "
               "0x%08x and wrote 0x%08x, which is not a COM object of this "
               "host's.\n", hr, g0);
        gpu_device_destroy();
        return fails + 1;
    }
    s0 = d3d8_object_from_guest(g0);
    if (d3d8_object_iface(s0) != D3D8_IF_IDirect3DSurface8) {
        printf("d3d8 texlevel selftest: FAILED -- what came back is a %s, not "
               "an IDirect3DSurface8.\n",
               d3d8_iface_name(d3d8_object_iface(s0)));
        fails++;
    }
    if ((uint32_t)d3d8_object_refs(tex) <= before) {
        printf("d3d8 texlevel selftest: FAILED -- the texture's reference count "
               "did not rise (%u then %u). The engine Releases the surface it "
               "was given, and an unbalanced count destroys the texture early "
               "or never.\n", before, (uint32_t)d3d8_object_refs(tex));
        fails++;
    }

    /* The same level twice is the same surface. */
    args[0] = 0; args[1] = out;
    call_method(tex, 15, args, 2);
    g0b = RD32(out);
    if (g0b != g0) {
        printf("d3d8 texlevel selftest: FAILED -- asking for level 0 twice gave "
               "0x%08x then 0x%08x. D3D8's level surfaces are persistent "
               "children, and code that compares them would see two.\n",
               g0, g0b);
        fails++;
    }
    /* and Releasing it gives the texture's count back. */
    call_method(s0, 2, NULL, 0);

    args[0] = 1; args[1] = out;
    call_method(tex, 15, args, 2);
    g1 = RD32(out);
    if (!g1 || g1 == g0) {
        printf("d3d8 texlevel selftest: FAILED -- level 1 came back as 0x%08x, "
               "the same object as level 0.\n", g1);
        gpu_device_destroy();
        return fails + 1;
    }
    s1 = d3d8_object_from_guest(g1);

    /* Out of range, and nowhere to write. */
    WR32(out, 0xA5A5A5A5u);
    args[0] = 3; args[1] = out;
    hr = call_method(tex, 15, args, 2);
    if (hr != D3DERR_INVALIDCALL || RD32(out) != 0) {
        printf("d3d8 texlevel selftest: FAILED -- level 3 of a 3-level texture "
               "returned 0x%08x and wrote 0x%08x; expected INVALIDCALL and a "
               "NULL out-pointer.\n", hr, RD32(out));
        fails++;
    }
    args[0] = 0; args[1] = 0;
    hr = call_method(tex, 15, args, 2);
    if (hr != D3DERR_INVALIDCALL) {
        printf("d3d8 texlevel selftest: FAILED -- GetSurfaceLevel with a NULL "
               "out-pointer returned 0x%08x.\n", hr);
        fails++;
    }

    /* GetDesc (slot 8) must describe LEVEL 1, not the texture. */
    memset((void *)(uintptr_t)desc, 0xA5, 32);
    args[0] = desc;
    call_method(s1, 8, args, 1);
    if (RD32(desc + 24u) != 32u || RD32(desc + 28u) != 16u) {
        printf("d3d8 texlevel selftest: FAILED -- level 1 of a 64x32 texture "
               "describes itself as %ux%u, not 32x16.\n",
               RD32(desc + 24u), RD32(desc + 28u));
        fails++;
    }
    if (RD32(desc + 16u) != 32u * 16u * 4u) {
        printf("d3d8 texlevel selftest: FAILED -- level 1 says it is %u bytes, "
               "not %u.\n", RD32(desc + 16u), 32u * 16u * 4u);
        fails++;
    }
    if (RD32(desc + 0u) != (uint32_t)D3DFMT_A8R8G8B8) {
        printf("d3d8 texlevel selftest: FAILED -- level 1 reports format %u, "
               "not the texture's.\n", RD32(desc + 0u));
        fails++;
    }

    /* THE CHECK: the surface's bytes are the texture's bytes. LockRect is
       slot 9 on the surface (pLockedRect, pRect) and slot 16 on the texture
       (level, pLockedRect, pRect). */
    WR32(lr, 0); WR32(lr + 4u, 0);
    args[0] = lr; args[1] = 0;
    call_method(s1, 9, args, 2);
    if (RD32(lr) != 32u * 4u) {
        printf("d3d8 texlevel selftest: FAILED -- level 1 locked with a pitch "
               "of %u; 32 pixels of BGRA8 is %u.\n", RD32(lr), 32u * 4u);
        fails++;
    }
    {
        uint32_t surfptr = RD32(lr + 4u);
        uint32_t targs[3];
        targs[0] = 1; targs[1] = lr; targs[2] = 0;
        WR32(lr, 0); WR32(lr + 4u, 0);
        call_method(tex, 16, targs, 3);
        texptr = RD32(lr + 4u);
        call_method(tex, 17, targs, 1);              /* texture UnlockRect(1) */
        if (!surfptr || surfptr != texptr) {
            printf("d3d8 texlevel selftest: FAILED -- the level 1 surface locks "
                   "at 0x%08x and the texture's own level 1 at 0x%08x. The "
                   "surface has storage of its own, so everything the engine "
                   "writes through it is lost.\n", surfptr, texptr);
            fails++;
        }
        if (surfptr) {
            /* Something recognisable, so the upload has real bytes to move. */
            memset((void *)(uintptr_t)surfptr, 0x5A, 32u * 16u * 4u);
        }
    }

    /* And unlocking the SURFACE uploads that level of the texture. */
    uploads = d3d8_texture_uploads(tex);
    call_method(s1, 10, NULL, 0);                    /* surface UnlockRect */
    if (d3d8_texture_uploads(tex) != uploads + 1u) {
        printf("d3d8 texlevel selftest: FAILED -- unlocking the level 1 surface "
               "uploaded nothing (%lu uploads before and after). The engine "
               "fills textures through these surfaces, so the texture would "
               "stay empty.\n", uploads);
        fails++;
    } else if (d3d8_texture_last_upload_level(tex) != 1u) {
        printf("d3d8 texlevel selftest: FAILED -- unlocking the level 1 surface "
               "uploaded level %u instead.\n",
               d3d8_texture_last_upload_level(tex));
        fails++;
    }

    (void)s0;
    gpu_device_destroy();
    printf("d3d8 texlevel selftest: %s\n", fails ? "FAILED"
           : "PASSED -- a level surface is a VIEW on the texture's own bytes, "
             "describes its own level, and uploads that level on unlock");
    return fails;
}

int d3d8_host_selftest(void)
{
    int fails = 0;
    fails += d3d8_com_selftest();
    fails += caps_selftest();
    fails += pixel_shader_selftest();
    fails += gamma_selftest();
    fails += getdirect3d_selftest();
    fails += texture_level_selftest();
    fails += d3d8_draw_selftest();
    printf("d3d8: SELF-TEST %s -- %d failure(s)\n",
           fails ? "FAILED" : "PASSED", fails);
    fflush(stdout);
    return fails;
}
