/*
 * IDirect3DDevice8 on the host GPU.
 *
 * The shape of this file follows what the interface actually is: most of
 * IDirect3DDevice8 is a state machine, a handful of methods are the frame
 * boundary, and the rest create resources. So the state setters record into
 * the mirror (d3d8_state.c), the frame methods drive src/gpu, and everything
 * not yet written REPORTS ITSELF BY NAME rather than returning D3D_OK -- a
 * device that answers OK to a method it did not perform is a device whose
 * output cannot be attributed to anything.
 *
 * Note what is NOT here: any decision about how the game renders. Which states
 * it sets, in what order, with what values, is the engine's business and the
 * engine's code makes those decisions unmodified. That is the entire argument
 * for cutting at this boundary (see d3d8_host.h).
 */
#include "d3d8_device.h"
#include "d3d8_com.h"
#include "d3d8_caps.h"
#include "d3d8_state.h"
#include "d3d8_surface.h"
#include "d3d8_resource.h"
#include "d3d8_drawcall.h"
#include "d3d8_types.h"

#include "gpu_device.h"
#include "gpu_draw.h"
#include "win32_sdl.h"

#include "x86rt.h"

#include <stdio.h>
#include <string.h>

typedef struct {
    uint32_t              adapter, devtype, focus_window, behaviour;
    D3DPRESENT_PARAMETERS pp;
    D3D8CapsLimits        limits;
    D3D8State             state;

    uint16_t              gamma[3 * 256]; /* the ramp as D3D8 lays it out */
    int                   gamma_set, gamma_curved;
    unsigned              gamma_warned;

    unsigned long         scenes;         /* BeginScene calls */
    unsigned long         presents;
    unsigned long         clears;
    unsigned long         draws;
} Device;

static Device g_dev;
static D3D8Object *g_dev_obj;

static void *guest_ptr(uint32_t a, const char *what)
{
    if (!a) {
        fprintf(stderr, "d3d8: %s was given a NULL %s\n",
                d3d8_current_method(), what);
        return NULL;
    }
    return (void *)(uintptr_t)a;
}

/* ---- IUnknown ---------------------------------------------------------- */

static void dev_QueryInterface(D3D8Object *self, CPU *C)
{
    uint32_t ppv = d3d8_arg(C, 1);
    (void)self;
    if (ppv) WR32(ppv, 0);
    d3d8_ret(C, E_NOINTERFACE);
}

static void dev_AddRef(D3D8Object *self, CPU *C)
{
    d3d8_ret(C, (uint32_t)d3d8_object_addref(self));
}

static void device_destroyed(D3D8Object *o)
{
    (void)o;
    printf("d3d8: the engine released the device; tearing down the GPU device "
           "with it.\n");
    gpu_device_destroy();
}

static void dev_Release(D3D8Object *self, CPU *C)
{
    d3d8_ret(C, (uint32_t)d3d8_object_release(self));
}

/* ---- what the device is ------------------------------------------------ */

static void dev_TestCooperativeLevel(D3D8Object *self, CPU *C)
{
    /* This host never loses its device: there is no exclusive-mode desktop to
       be taken away. D3D_OK is the truth here, not a placeholder. */
    (void)self;
    d3d8_ret(C, D3D_OK);
}

static void dev_GetAvailableTextureMem(D3D8Object *self, CPU *C)
{
    /* The engine budgets against this. 256 MB is what the backend is willing
       to be held to; reporting the host GPU's real free memory would make the
       budget vary run to run and the same scene behave differently. */
    (void)self;
    d3d8_ret(C, 256u * 1024u * 1024u);
}

static void dev_ResourceManagerDiscardBytes(D3D8Object *self, CPU *C)
{
    /* Nothing is managed yet, so there is nothing to discard -- and that is
       the honest answer rather than a stub: D3D_OK means "done", and with no
       managed pool the work really is done. */
    (void)self;
    d3d8_ret(C, D3D_OK);
}

static void dev_GetDeviceCaps(D3D8Object *self, CPU *C)
{
    D3DCAPS8 *caps = (D3DCAPS8 *)guest_ptr(d3d8_arg(C, 0), "caps block");
    (void)self;
    if (!caps) { d3d8_ret(C, D3DERR_INVALIDCALL); return; }
    d3d8_caps_fill(caps, g_dev.adapter, g_dev.devtype, &g_dev.limits);
    d3d8_ret(C, D3D_OK);
}

static void dev_GetDisplayMode(D3D8Object *self, CPU *C)
{
    D3DDISPLAYMODE *m = (D3DDISPLAYMODE *)guest_ptr(d3d8_arg(C, 0), "mode");
    (void)self;
    if (!m) { d3d8_ret(C, D3DERR_INVALIDCALL); return; }
    m->Width = g_dev.pp.BackBufferWidth;
    m->Height = g_dev.pp.BackBufferHeight;
    m->RefreshRate = 60;
    m->Format = g_dev.pp.BackBufferFormat;
    d3d8_ret(C, D3D_OK);
}

static void dev_GetCreationParameters(D3D8Object *self, CPU *C)
{
    D3DDEVICE_CREATION_PARAMETERS *p =
        (D3DDEVICE_CREATION_PARAMETERS *)guest_ptr(d3d8_arg(C, 0),
                                                   "creation parameters");
    (void)self;
    if (!p) { d3d8_ret(C, D3DERR_INVALIDCALL); return; }
    p->AdapterOrdinal = g_dev.adapter;
    p->DeviceType = g_dev.devtype;
    p->hFocusWindow = g_dev.focus_window;
    p->BehaviorFlags = g_dev.behaviour;
    d3d8_ret(C, D3D_OK);
}

static void dev_GetDirect3D(D3D8Object *self, CPU *C)
{
    uint32_t out = d3d8_arg(C, 0);
    (void)self;
    if (!out) { d3d8_ret(C, D3DERR_INVALIDCALL); return; }
    /* Deliberately not implemented as "return the singleton and skip the
       AddRef": the caller will Release it, and an unbalanced count is exactly
       the class of bug that makes teardown fail in a way nothing explains. */
    fprintf(stderr, "d3d8: IDirect3DDevice8::GetDirect3D is not implemented; "
                    "handing back the IDirect3D8 needs an AddRef this device "
                    "cannot yet balance.\n");
    WR32(out, 0);
    d3d8_ret(C, D3DERR_INVALIDCALL);
}

/* ---- the cursor -------------------------------------------------------- */

/*
 * NO-OP, and each says why -- the distinction this project draws between "not
 * implemented" and "doing nothing IS the implementation".
 *
 * The host owns its window and its cursor through SDL. A D3D8 hardware cursor
 * is a thing the game may set and never look at again; ignoring it costs the
 * cursor image, not correctness, and the alternative -- reporting failure --
 * would have the engine fall back to drawing a software cursor into the scene
 * that the host cursor would then be drawn on top of.
 */
static void dev_SetCursorProperties(D3D8Object *self, CPU *C)
{
    (void)self;
    d3d8_ret(C, D3D_OK);
}
static void dev_SetCursorPosition(D3D8Object *self, CPU *C)
{
    (void)self;                              /* returns void in the ABI */
    d3d8_ret(C, 0);
}
static void dev_ShowCursor(D3D8Object *self, CPU *C)
{
    (void)self;
    d3d8_ret(C, 0);                          /* the previous visibility */
}

/* ---- render destinations ----------------------------------------------- */

/*
 * The back buffer and the depth/stencil are what igDxVisualContext calls its
 * render destinations: it fetches them once with GetBackBuffer and
 * GetDepthStencilSurface, keeps them, and hands them back to SetRenderTarget
 * whenever it changes where drawing goes. They exist for the life of the
 * device, so they are created with it rather than on demand -- which also
 * means GetBackBuffer is a lookup and cannot fail for a reason the caller has
 * to handle.
 *
 * Neither is lockable. They live on the GPU, and src/gpu does no readback; the
 * surface layer refuses a Lock on them by name rather than handing over a
 * buffer whose contents would be invented.
 */
static D3D8Object *g_backbuffer, *g_depth;
static D3D8Object *g_render_target, *g_render_depth;

static void dev_GetBackBuffer(D3D8Object *self, CPU *C)
{
    uint32_t index = d3d8_arg(C, 0), type = d3d8_arg(C, 1);
    uint32_t out = d3d8_arg(C, 2);

    (void)self;
    if (!out) { d3d8_ret(C, D3DERR_INVALIDCALL); return; }
    /* D3DBACKBUFFER_TYPE_MONO is 0; the stereo types belong to hardware this
       backend is not. One back buffer, so any other index is the engine
       asking for something that does not exist. */
    if (type != 0 || index != 0) {
        fprintf(stderr, "d3d8: GetBackBuffer(index=%u, type=%u) -- this device "
                        "presents one mono back buffer.\n", index, type);
        WR32(out, 0);
        d3d8_ret(C, D3DERR_INVALIDCALL);
        return;
    }
    d3d8_object_addref(g_backbuffer);
    WR32(out, d3d8_object_guest(g_backbuffer));
    d3d8_ret(C, D3D_OK);
}

static void dev_GetDepthStencilSurface(D3D8Object *self, CPU *C)
{
    uint32_t out = d3d8_arg(C, 0);
    (void)self;
    if (!out) { d3d8_ret(C, D3DERR_INVALIDCALL); return; }
    if (!g_depth) {
        /* The engine asked for a depth buffer it did not ask the device to
           create. Real D3D8 answers D3DERR_NOTFOUND here and the engine has a
           path for it; inventing one would be a depth buffer nothing renders
           into. */
        fprintf(stderr, "d3d8: GetDepthStencilSurface, but CreateDevice was "
                        "called with EnableAutoDepthStencil false, so there is "
                        "none.\n");
        WR32(out, 0);
        d3d8_ret(C, D3DERR_INVALIDCALL);
        return;
    }
    d3d8_object_addref(g_depth);
    WR32(out, d3d8_object_guest(g_depth));
    d3d8_ret(C, D3D_OK);
}

static void dev_GetRenderTarget(D3D8Object *self, CPU *C)
{
    uint32_t out = d3d8_arg(C, 0);
    (void)self;
    if (!out) { d3d8_ret(C, D3DERR_INVALIDCALL); return; }
    d3d8_object_addref(g_render_target);
    WR32(out, d3d8_object_guest(g_render_target));
    d3d8_ret(C, D3D_OK);
}

static void dev_SetRenderTarget(D3D8Object *self, CPU *C)
{
    uint32_t rt = d3d8_arg(C, 0), ds = d3d8_arg(C, 1);
    D3D8Object *rt_obj = rt ? d3d8_object_from_guest(rt) : NULL;
    D3D8Object *ds_obj = ds ? d3d8_object_from_guest(ds) : NULL;

    (void)self;
    if (rt && !rt_obj) {
        fprintf(stderr, "d3d8: SetRenderTarget was given 0x%08x, which is not "
                        "a surface this host made.\n", rt);
        d3d8_ret(C, D3DERR_INVALIDCALL);
        return;
    }
    /* NULL means "keep the current one" for the render target and "no depth
       buffer" for the stencil -- two different meanings for the same value,
       which is D3D8's rule and not one to normalise away. */
    if (rt_obj) g_render_target = rt_obj;
    g_render_depth = ds_obj;

    {
        D3D8Surface *s = d3d8_surface_of(g_render_target);
        /*
         * src/gpu has exactly one destination, the swapchain. Anything else is
         * an off-screen target it does not have, and drawing would silently go
         * to the wrong place -- so it is reported rather than ignored. This is
         * the same gap the --vk path reported as "render destination 3".
         */
        gpu_frame_bind_target(s->kind == D3D8_SURF_BACKBUFFER ? 0u : 1u);
    }
    d3d8_ret(C, D3D_OK);
}

static void dev_CreateImageSurface(D3D8Object *self, CPU *C)
{
    uint32_t w = d3d8_arg(C, 0), h = d3d8_arg(C, 1), fmt = d3d8_arg(C, 2);
    uint32_t out = d3d8_arg(C, 3);
    D3D8Object *s;

    (void)self;
    if (!out) { d3d8_ret(C, D3DERR_INVALIDCALL); return; }
    /* D3DPOOL_SYSTEMMEM: an image surface is host memory the guest fills and
       then copies from, which is exactly what this host can do honestly. */
    s = d3d8_surface_new(D3D8_SURF_SYSTEM, w, h, fmt, 0, 2);
    if (!s) { WR32(out, 0); d3d8_ret(C, E_OUTOFMEMORY); return; }
    WR32(out, d3d8_object_guest(s));
    d3d8_ret(C, D3D_OK);
}

/* ---- the frame --------------------------------------------------------- */

static void dev_BeginScene(D3D8Object *self, CPU *C)
{
    (void)self;
    g_dev.scenes++;
    d3d8_ret(C, gpu_frame_begin() ? D3D_OK : D3DERR_INVALIDCALL);
}

static void dev_EndScene(D3D8Object *self, CPU *C)
{
    (void)self;
    /* EndScene closes the scene but does NOT present -- Present does. Ending
       the GPU frame here would submit work the engine has not finished
       describing. */
    d3d8_ret(C, D3D_OK);
}

static void dev_Present(D3D8Object *self, CPU *C)
{
    (void)self;
    gpu_frame_end();
    g_dev.presents++;
    d3d8_ret(C, D3D_OK);
}

static void dev_Clear(D3D8Object *self, CPU *C)
{
    uint32_t flags   = d3d8_arg(C, 2);
    uint32_t colour  = d3d8_arg(C, 3);
    float    depth   = d3d8_argf(C, 4);
    uint32_t stencil = d3d8_arg(C, 5);
    unsigned mask = 0;

    (void)self;
    /* D3DCLEAR_TARGET 1, ZBUFFER 2, STENCIL 4 -- and src/gpu takes the same
       three bits in the same order, so the flag word passes through. */
    mask = (unsigned)(flags & 7u);
    gpu_frame_clear(mask,
                    (float)((colour >> 16) & 0xFF) / 255.0f,
                    (float)((colour >>  8) & 0xFF) / 255.0f,
                    (float)((colour      ) & 0xFF) / 255.0f,
                    (float)((colour >> 24) & 0xFF) / 255.0f,
                    depth, stencil);
    g_dev.clears++;
    d3d8_ret(C, D3D_OK);
}

/* ---- state ------------------------------------------------------------- */

static void dev_SetRenderState(D3D8Object *self, CPU *C)
{
    uint32_t which = d3d8_arg(C, 0), value = d3d8_arg(C, 1);
    (void)self;
    d3d8_ret(C, d3d8_state_set_render(&g_dev.state, which, value)
                ? D3D_OK : D3DERR_INVALIDCALL);
}

static void dev_GetRenderState(D3D8Object *self, CPU *C)
{
    uint32_t which = d3d8_arg(C, 0), *out;
    (void)self;
    out = (uint32_t *)guest_ptr(d3d8_arg(C, 1), "value");
    if (!out) { d3d8_ret(C, D3DERR_INVALIDCALL); return; }
    d3d8_ret(C, d3d8_state_get_render(&g_dev.state, which, out)
                ? D3D_OK : D3DERR_INVALIDCALL);
}

static void dev_SetTextureStageState(D3D8Object *self, CPU *C)
{
    uint32_t stage = d3d8_arg(C, 0), which = d3d8_arg(C, 1);
    uint32_t value = d3d8_arg(C, 2);
    (void)self;
    d3d8_ret(C, d3d8_state_set_stage(&g_dev.state, stage, which, value)
                ? D3D_OK : D3DERR_INVALIDCALL);
}

static void dev_GetTextureStageState(D3D8Object *self, CPU *C)
{
    uint32_t stage = d3d8_arg(C, 0), which = d3d8_arg(C, 1), *out;
    (void)self;
    out = (uint32_t *)guest_ptr(d3d8_arg(C, 2), "value");
    if (!out) { d3d8_ret(C, D3DERR_INVALIDCALL); return; }
    d3d8_ret(C, d3d8_state_get_stage(&g_dev.state, stage, which, out)
                ? D3D_OK : D3DERR_INVALIDCALL);
}

static void dev_SetTransform(D3D8Object *self, CPU *C)
{
    uint32_t which = d3d8_arg(C, 0);
    const float *m = (const float *)guest_ptr(d3d8_arg(C, 1), "matrix");
    (void)self;
    if (!m) { d3d8_ret(C, D3DERR_INVALIDCALL); return; }
    if (which >= D3D8_MAX_TRANSFORMS) { d3d8_ret(C, D3DERR_INVALIDCALL); return; }
    memcpy(g_dev.state.transform[which].m, m, sizeof(float) * 16);
    g_dev.state.transform_set[which] = 1;
    d3d8_ret(C, D3D_OK);
}

static void dev_GetTransform(D3D8Object *self, CPU *C)
{
    uint32_t which = d3d8_arg(C, 0);
    float *m = (float *)guest_ptr(d3d8_arg(C, 1), "matrix");
    (void)self;
    if (!m || which >= D3D8_MAX_TRANSFORMS) {
        d3d8_ret(C, D3DERR_INVALIDCALL);
        return;
    }
    memcpy(m, g_dev.state.transform[which].m, sizeof(float) * 16);
    d3d8_ret(C, D3D_OK);
}

static void dev_SetViewport(D3D8Object *self, CPU *C)
{
    const D3DVIEWPORT8 *v =
        (const D3DVIEWPORT8 *)guest_ptr(d3d8_arg(C, 0), "viewport");
    (void)self;
    if (!v) { d3d8_ret(C, D3DERR_INVALIDCALL); return; }
    g_dev.state.viewport_x = (int32_t)v->X;
    g_dev.state.viewport_y = (int32_t)v->Y;
    g_dev.state.viewport_w = (int32_t)v->Width;
    g_dev.state.viewport_h = (int32_t)v->Height;
    g_dev.state.viewport_minz = v->MinZ;
    g_dev.state.viewport_maxz = v->MaxZ;
    g_dev.state.viewport_set = 1;
    gpu_frame_viewport((int)v->X, (int)v->Y, (int)v->Width, (int)v->Height,
                       v->MinZ, v->MaxZ);
    d3d8_ret(C, D3D_OK);
}

static void dev_GetViewport(D3D8Object *self, CPU *C)
{
    D3DVIEWPORT8 *v = (D3DVIEWPORT8 *)guest_ptr(d3d8_arg(C, 0), "viewport");
    (void)self;
    if (!v) { d3d8_ret(C, D3DERR_INVALIDCALL); return; }
    v->X = (uint32_t)g_dev.state.viewport_x;
    v->Y = (uint32_t)g_dev.state.viewport_y;
    v->Width = (uint32_t)g_dev.state.viewport_w;
    v->Height = (uint32_t)g_dev.state.viewport_h;
    v->MinZ = g_dev.state.viewport_minz;
    v->MaxZ = g_dev.state.viewport_maxz;
    d3d8_ret(C, D3D_OK);
}

static void dev_SetMaterial(D3D8Object *self, CPU *C)
{
    const float *m = (const float *)guest_ptr(d3d8_arg(C, 0), "material");
    (void)self;
    if (!m) { d3d8_ret(C, D3DERR_INVALIDCALL); return; }
    memcpy(g_dev.state.material, m, sizeof g_dev.state.material);
    g_dev.state.material_set = 1;
    d3d8_ret(C, D3D_OK);
}

static void dev_SetLight(D3D8Object *self, CPU *C)
{
    uint32_t idx = d3d8_arg(C, 0);
    const float *l = (const float *)guest_ptr(d3d8_arg(C, 1), "light");
    (void)self;
    if (!l || idx >= D3D8_MAX_LIGHTS) { d3d8_ret(C, D3DERR_INVALIDCALL); return; }
    memcpy(g_dev.state.light[idx], l, sizeof g_dev.state.light[0]);
    g_dev.state.light_set[idx] = 1;
    d3d8_ret(C, D3D_OK);
}

static void dev_LightEnable(D3D8Object *self, CPU *C)
{
    uint32_t idx = d3d8_arg(C, 0), on = d3d8_arg(C, 1);
    (void)self;
    if (idx >= D3D8_MAX_LIGHTS) { d3d8_ret(C, D3DERR_INVALIDCALL); return; }
    g_dev.state.light_on[idx] = on != 0;
    d3d8_ret(C, D3D_OK);
}

static void dev_ValidateDevice(D3D8Object *self, CPU *C)
{
    uint32_t *passes = (uint32_t *)guest_ptr(d3d8_arg(C, 0), "pass count");
    (void)self;
    /* One pass: this device does not multi-pass a texture-stage setup it
       cannot do in one. When the draw path exists and finds a setup it cannot
       express, this is where that will be said -- not by silently claiming
       one pass then drawing something else. */
    if (passes) *passes = 1;
    d3d8_ret(C, D3D_OK);
}


/* ---- resources --------------------------------------------------------- */

/*
 * Create* hands the object straight back; the engine holds it and releases it.
 * Each gets the destructor that frees its GPU object, declared at the point of
 * creation so the two cannot drift apart.
 */
static void dev_CreateTexture(D3D8Object *self, CPU *C)
{
    uint32_t w = d3d8_arg(C, 0), h = d3d8_arg(C, 1), levels = d3d8_arg(C, 2);
    uint32_t usage = d3d8_arg(C, 3), fmt = d3d8_arg(C, 4), pool = d3d8_arg(C, 5);
    uint32_t out = d3d8_arg(C, 6);
    D3D8Object *t;

    (void)self;
    if (!out) { d3d8_ret(C, D3DERR_INVALIDCALL); return; }
    t = d3d8_texture_new(w, h, levels, usage, fmt, pool);
    if (!t) { WR32(out, 0); d3d8_ret(C, D3DERR_INVALIDCALL); return; }
    d3d8_resource_attach_destructor(t);
    WR32(out, d3d8_object_guest(t));
    d3d8_ret(C, D3D_OK);
}

static void dev_CreateVertexBuffer(D3D8Object *self, CPU *C)
{
    uint32_t len = d3d8_arg(C, 0), usage = d3d8_arg(C, 1);
    uint32_t fvf = d3d8_arg(C, 2), pool = d3d8_arg(C, 3), out = d3d8_arg(C, 4);
    D3D8Object *b;

    (void)self;
    if (!out) { d3d8_ret(C, D3DERR_INVALIDCALL); return; }
    b = d3d8_vertexbuffer_new(len, usage, fvf, pool);
    if (!b) { WR32(out, 0); d3d8_ret(C, D3DERR_INVALIDCALL); return; }
    d3d8_resource_attach_destructor(b);
    WR32(out, d3d8_object_guest(b));
    d3d8_ret(C, D3D_OK);
}

static void dev_CreateIndexBuffer(D3D8Object *self, CPU *C)
{
    uint32_t len = d3d8_arg(C, 0), usage = d3d8_arg(C, 1);
    uint32_t fmt = d3d8_arg(C, 2), pool = d3d8_arg(C, 3), out = d3d8_arg(C, 4);
    D3D8Object *b;

    (void)self;
    if (!out) { d3d8_ret(C, D3DERR_INVALIDCALL); return; }
    b = d3d8_indexbuffer_new(len, usage, fmt, pool);
    if (!b) { WR32(out, 0); d3d8_ret(C, D3DERR_INVALIDCALL); return; }
    d3d8_resource_attach_destructor(b);
    WR32(out, d3d8_object_guest(b));
    d3d8_ret(C, D3D_OK);
}

/* ---- what is bound ----------------------------------------------------- */

static void dev_SetTexture(D3D8Object *self, CPU *C)
{
    uint32_t stage = d3d8_arg(C, 0), tex = d3d8_arg(C, 1);
    (void)self;
    if (stage >= D3D8_MAX_STAGES) { d3d8_ret(C, D3DERR_INVALIDCALL); return; }
    if (tex && !d3d8_object_from_guest(tex)) {
        fprintf(stderr, "d3d8: SetTexture(%u, 0x%08x) -- that is not a texture "
                        "this host made.\n", stage, tex);
        d3d8_ret(C, D3DERR_INVALIDCALL);
        return;
    }
    g_dev.state.texture[stage] = tex;
    d3d8_ret(C, D3D_OK);
}

static void dev_SetStreamSource(D3D8Object *self, CPU *C)
{
    uint32_t stream = d3d8_arg(C, 0), buf = d3d8_arg(C, 1);
    uint32_t stride = d3d8_arg(C, 2);
    (void)self;
    if (stream >= D3D8_MAX_STREAMS) { d3d8_ret(C, D3DERR_INVALIDCALL); return; }
    if (buf && !d3d8_object_from_guest(buf)) {
        fprintf(stderr, "d3d8: SetStreamSource was given 0x%08x, which is not "
                        "a buffer this host made.\n", buf);
        d3d8_ret(C, D3DERR_INVALIDCALL);
        return;
    }
    g_dev.state.stream[stream].guest_ptr = buf;
    g_dev.state.stream[stream].stride = stride;
    d3d8_ret(C, D3D_OK);
}

static void dev_SetIndices(D3D8Object *self, CPU *C)
{
    uint32_t buf = d3d8_arg(C, 0), base = d3d8_arg(C, 1);
    (void)self;
    if (buf && !d3d8_object_from_guest(buf)) {
        fprintf(stderr, "d3d8: SetIndices was given 0x%08x, which is not a "
                        "buffer this host made.\n", buf);
        d3d8_ret(C, D3DERR_INVALIDCALL);
        return;
    }
    g_dev.state.indices = buf;
    g_dev.state.base_vertex_index = base;
    d3d8_ret(C, D3D_OK);
}

static void dev_SetVertexShader(D3D8Object *self, CPU *C)
{
    /* Below 0x10000 this is an FVF code, not a handle -- see d3d8_build_draw,
       which is where the distinction is acted on. */
    (void)self;
    g_dev.state.vertex_shader = d3d8_arg(C, 0);
    d3d8_ret(C, D3D_OK);
}

static void dev_GetVertexShader(D3D8Object *self, CPU *C)
{
    uint32_t *out = (uint32_t *)guest_ptr(d3d8_arg(C, 0), "handle");
    (void)self;
    if (!out) { d3d8_ret(C, D3DERR_INVALIDCALL); return; }
    *out = g_dev.state.vertex_shader;
    d3d8_ret(C, D3D_OK);
}

/*
 * The pixel-shader handle. Unlike SetVertexShader there is no overloading
 * here: a pixel-shader handle is only ever a handle, and 0 means "none --
 * texture-stage cascade, i.e. the fixed-function pipeline".
 *
 * What the engine actually does, measured rather than assumed: its first call
 * is SetPixelShader(0), which is it selecting fixed function. This backend
 * implements exactly that, so answering D3D_OK is faithful and the draw path
 * built from the state mirror is what the engine asked for.
 *
 * A NON-ZERO handle is refused, and the refusal is the important half. No
 * handle can exist -- CreatePixelShader is not implemented, and it will report
 * itself by name if the engine ever calls it. So a non-zero handle arriving
 * here means either a shader was created by something this host does not know
 * about, or the argument is wrong. Recording it and carrying on would leave
 * the fixed-function path drawing in place of a shader, which is the failure
 * this project keeps writing down: output that cannot be attributed, because
 * nothing said it was missing.
 */
static void dev_SetPixelShader(D3D8Object *self, CPU *C)
{
    uint32_t handle = d3d8_arg(C, 0);
    (void)self;
    if (handle) {
        fprintf(stderr,
                "d3d8: SetPixelShader(0x%08x) -- this host has never created a "
                "pixel shader, so that handle cannot be one of its.\n"
                "  There is no ps.1.x translator here; CreatePixelShader "
                "reports itself by name and is the work item.\n"
                "  Refusing rather than binding nothing, which would draw the "
                "fixed-function result in a shader's place.\n", handle);
        d3d8_ret(C, D3DERR_INVALIDCALL);
        return;
    }
    g_dev.state.pixel_shader = 0;
    d3d8_ret(C, D3D_OK);
}

/* ---- the gamma ramp ---------------------------------------------------- */

/*
 * D3D8's gamma ramp is three arrays of 256 16-bit entries, applied by the
 * display hardware between the back buffer and the monitor. This backend
 * presents through a Vulkan swapchain and has no such control, so the ramp is
 * RECORDED and not programmed.
 *
 * That is only acceptable because of what is checked here. An IDENTITY ramp --
 * entry i = i * 257, spanning 0..0xffff -- changes nothing, so ignoring it
 * costs nothing and there is no debt to record. A CURVED one is a visible
 * difference from the original game: the scene will be brighter or darker than
 * the engine intended and no pixel comparison would explain why. So the curved
 * case says so, once, by name.
 *
 * Applying it for real belongs in the presentation pass (src/gpu), as a lookup
 * on the way to the swapchain image; nothing here fakes that.
 */
static int ramp_is_identity(const uint16_t *r)
{
    int i;
    for (i = 0; i < 3 * 256; i++)
        if (r[i] != (uint16_t)((i % 256) * 257))
            return 0;
    return 1;
}

static void dev_SetGammaRamp(D3D8Object *self, CPU *C)
{
    const uint16_t *ramp =
        (const uint16_t *)guest_ptr(d3d8_arg(C, 1), "gamma ramp");
    (void)self;
    if (!ramp) { d3d8_ret(C, 0); return; }         /* SetGammaRamp returns void */

    memcpy(g_dev.gamma, ramp, sizeof g_dev.gamma);
    g_dev.gamma_set = 1;
    g_dev.gamma_curved = !ramp_is_identity(ramp);
    if (g_dev.gamma_curved && !g_dev.gamma_warned++)
        fprintf(stderr,
                "d3d8: SetGammaRamp was given a CURVED ramp, and this backend "
                "cannot programme one.\n"
                "  It presents through a Vulkan swapchain; there is no "
                "hardware ramp to set, and applying it belongs in the "
                "presentation pass.\n"
                "  The ramp is recorded and readable, but the picture will be "
                "brighter or darker than the game intends until it is.\n");
    d3d8_ret(C, 0);
}

static void dev_GetGammaRamp(D3D8Object *self, CPU *C)
{
    uint16_t *out = (uint16_t *)guest_ptr(d3d8_arg(C, 0), "gamma ramp");
    (void)self;
    if (!out) { d3d8_ret(C, 0); return; }
    /* Never set: D3D hands back the identity ramp, which is what the hardware
       would be doing. Inventing zeroes here would read as "the screen is
       black" to anything that asks. */
    if (!g_dev.gamma_set) {
        int i;
        for (i = 0; i < 3 * 256; i++) out[i] = (uint16_t)((i % 256) * 257);
    } else {
        memcpy(out, g_dev.gamma, sizeof g_dev.gamma);
    }
    d3d8_ret(C, 0);
}

int d3d8_device_gamma_curved(void) { return g_dev.gamma_curved; }

static void dev_GetPixelShader(D3D8Object *self, CPU *C)
{
    uint32_t *out = (uint32_t *)guest_ptr(d3d8_arg(C, 0), "handle");
    (void)self;
    if (!out) { d3d8_ret(C, D3DERR_INVALIDCALL); return; }
    *out = g_dev.state.pixel_shader;
    d3d8_ret(C, D3D_OK);
}

/* ---- the draws --------------------------------------------------------- */

static int fill_request(D3D8DrawRequest *req, uint32_t prim, uint32_t count)
{
    D3D8Object *vb = g_dev.state.stream[0].guest_ptr
                         ? d3d8_object_from_guest(g_dev.state.stream[0].guest_ptr)
                         : NULL;
    D3D8Object *ib = g_dev.state.indices
                         ? d3d8_object_from_guest(g_dev.state.indices) : NULL;
    D3D8Object *tx = g_dev.state.texture[0]
                         ? d3d8_object_from_guest(g_dev.state.texture[0]) : NULL;

    memset(req, 0, sizeof *req);
    if (!vb) {
        fprintf(stderr, "d3d8: a draw with no vertex buffer on stream 0.\n");
        return 0;
    }
    req->vertex_buffer = d3d8_resource_buffer(vb);
    req->stride = g_dev.state.stream[0].stride;
    if (ib) {
        req->index_buffer = d3d8_resource_buffer(ib);
        req->index_is_32bit = d3d8_resource_index_is_32bit(ib);
    }
    if (tx) req->texture = d3d8_resource_texture(tx);
    req->primitive_type = prim;
    req->primitive_count = count;
    return 1;
}

static void dev_DrawPrimitive(D3D8Object *self, CPU *C)
{
    D3D8DrawRequest req;
    GpuDraw gd;

    (void)self;
    if (!fill_request(&req, d3d8_arg(C, 0), d3d8_arg(C, 2))) {
        d3d8_ret(C, D3DERR_INVALIDCALL);
        return;
    }
    req.first_vertex = d3d8_arg(C, 1);
    if (!d3d8_build_draw(&g_dev.state, &req, &gd)) {
        d3d8_ret(C, D3DERR_INVALIDCALL);
        return;
    }
    g_dev.draws += gpu_draw(&gd) ? 1u : 0u;
    d3d8_ret(C, D3D_OK);
}

static void dev_DrawIndexedPrimitive(D3D8Object *self, CPU *C)
{
    D3D8DrawRequest req;
    GpuDraw gd;

    (void)self;
    /* (PrimitiveType, MinIndex, NumVertices, StartIndex, PrimitiveCount) */
    if (!fill_request(&req, d3d8_arg(C, 0), d3d8_arg(C, 4))) {
        d3d8_ret(C, D3DERR_INVALIDCALL);
        return;
    }
    if (!req.index_buffer) {
        fprintf(stderr, "d3d8: DrawIndexedPrimitive with no index buffer "
                        "bound.\n");
        d3d8_ret(C, D3DERR_INVALIDCALL);
        return;
    }
    req.first_index = d3d8_arg(C, 3);
    req.base_vertex = g_dev.state.base_vertex_index;
    if (!d3d8_build_draw(&g_dev.state, &req, &gd)) {
        d3d8_ret(C, D3DERR_INVALIDCALL);
        return;
    }
    g_dev.draws += gpu_draw(&gd) ? 1u : 0u;
    d3d8_ret(C, D3D_OK);
}

/* ---- the table --------------------------------------------------------- */


/*
 * Indexed by slot. NULL is not an omission -- it is this device saying it does
 * not implement that method, and the reporter names it when the engine calls
 * it. The list of NULLs IS the renderer's remaining work, and it shrinks in
 * the order the engine asks.
 */
static const D3D8MethodFn g_impl[] = {
    dev_QueryInterface,                 /*  0 */
    dev_AddRef,
    dev_Release,
    dev_TestCooperativeLevel,
    dev_GetAvailableTextureMem,
    dev_ResourceManagerDiscardBytes,    /*  5 */
    dev_GetDirect3D,
    dev_GetDeviceCaps,
    dev_GetDisplayMode,
    dev_GetCreationParameters,
    dev_SetCursorProperties,            /* 10 */
    dev_SetCursorPosition,
    dev_ShowCursor,
    NULL,                               /* 13 CreateAdditionalSwapChain */
    NULL,                               /* 14 Reset */
    dev_Present,                        /* 15 */
    dev_GetBackBuffer,                  /* 16 */
    NULL,                               /* 17 GetRasterStatus */
    dev_SetGammaRamp,                   /* 18 */
    dev_GetGammaRamp,                   /* 19 */
    dev_CreateTexture,                  /* 20 */
    NULL,                               /* 21 CreateVolumeTexture */
    NULL,                               /* 22 CreateCubeTexture */
    dev_CreateVertexBuffer,             /* 23 */
    dev_CreateIndexBuffer,              /* 24 */
    NULL,                               /* 25 CreateRenderTarget */
    NULL,                               /* 26 CreateDepthStencilSurface */
    dev_CreateImageSurface,             /* 27 */
    NULL,                               /* 28 CopyRects */
    NULL,                               /* 29 UpdateTexture */
    NULL,                               /* 30 GetFrontBuffer */
    dev_SetRenderTarget,                /* 31 */
    dev_GetRenderTarget,                /* 32 */
    dev_GetDepthStencilSurface,         /* 33 */
    dev_BeginScene,                     /* 34 */
    dev_EndScene,                       /* 35 */
    dev_Clear,                          /* 36 */
    dev_SetTransform,                   /* 37 */
    dev_GetTransform,                   /* 38 */
    NULL,                               /* 39 MultiplyTransform */
    dev_SetViewport,                    /* 40 */
    dev_GetViewport,                    /* 41 */
    dev_SetMaterial,                    /* 42 */
    NULL,                               /* 43 GetMaterial */
    dev_SetLight,                       /* 44 */
    NULL,                               /* 45 GetLight */
    dev_LightEnable,                    /* 46 */
    NULL,                               /* 47 GetLightEnable */
    NULL,                               /* 48 SetClipPlane */
    NULL,                               /* 49 GetClipPlane */
    dev_SetRenderState,                 /* 50 */
    dev_GetRenderState,                 /* 51 */
    NULL,                               /* 52 BeginStateBlock */
    NULL,                               /* 53 EndStateBlock */
    NULL,                               /* 54 ApplyStateBlock */
    NULL,                               /* 55 CaptureStateBlock */
    NULL,                               /* 56 DeleteStateBlock */
    NULL,                               /* 57 CreateStateBlock */
    NULL,                               /* 58 SetClipStatus */
    NULL,                               /* 59 GetClipStatus */
    NULL,                               /* 60 GetTexture */
    dev_SetTexture,                     /* 61 */
    dev_GetTextureStageState,           /* 62 */
    dev_SetTextureStageState,           /* 63 */
    dev_ValidateDevice,                 /* 64 */
    NULL,                               /* 65 GetInfo */
    NULL,                               /* 66 SetPaletteEntries */
    NULL,                               /* 67 GetPaletteEntries */
    NULL,                               /* 68 SetCurrentTexturePalette */
    NULL,                               /* 69 GetCurrentTexturePalette */
    dev_DrawPrimitive,                  /* 70 */
    dev_DrawIndexedPrimitive,           /* 71 */
    NULL,                               /* 72 DrawPrimitiveUP */
    NULL,                               /* 73 DrawIndexedPrimitiveUP */
    NULL,                               /* 74 ProcessVertices */
    NULL,                               /* 75 CreateVertexShader */
    dev_SetVertexShader,                /* 76 */
    dev_GetVertexShader,                /* 77 */
    NULL,                               /* 78 DeleteVertexShader */
    NULL,                               /* 79 SetVertexShaderConstant */
    NULL,                               /* 80 GetVertexShaderConstant */
    NULL,                               /* 81 GetVertexShaderDeclaration */
    NULL,                               /* 82 GetVertexShaderFunction */
    dev_SetStreamSource,                /* 83 */
    NULL,                               /* 84 GetStreamSource */
    dev_SetIndices,                     /* 85 */
    NULL,                               /* 86 GetIndices */
    NULL,                               /* 87 CreatePixelShader */
    dev_SetPixelShader,                 /* 88 */
    dev_GetPixelShader,                 /* 89 */
    NULL,                               /* 90 DeletePixelShader */
    NULL,                               /* 91 SetPixelShaderConstant */
    NULL,                               /* 92 GetPixelShaderConstant */
    NULL,                               /* 93 GetPixelShaderFunction */
    NULL,                               /* 94 DrawRectPatch */
    NULL,                               /* 95 DrawTriPatch */
    NULL                                /* 96 DeletePatch */
};

void d3d8_device_install(void)
{
    d3d8_iface_implement(D3D8_IF_IDirect3DDevice8, g_impl,
                         (int)(sizeof g_impl / sizeof g_impl[0]));
}

D3D8Object *d3d8_device_create(uint32_t adapter, uint32_t devtype,
                               uint32_t focus_window, uint32_t behaviour,
                               const D3DPRESENT_PARAMETERS *pp)
{
    if (g_dev_obj) {
        fprintf(stderr, "d3d8: CreateDevice called a second time. This host "
                        "keeps one device; the first is still alive.\n");
        return NULL;
    }
    memset(&g_dev, 0, sizeof g_dev);
    g_dev.adapter = adapter;
    g_dev.devtype = devtype;
    g_dev.focus_window = focus_window;
    g_dev.behaviour = behaviour;
    g_dev.pp = *pp;
    d3d8_caps_limits_default(&g_dev.limits);
    d3d8_state_reset(&g_dev.state);

    printf("d3d8: CreateDevice adapter=%u %s %ux%u fmt=%u backbuffers=%u "
           "depth=%s(%u) hwnd=0x%08x\n",
           adapter, (behaviour & 0x40u) ? "software-vertex" : "hardware-vertex",
           pp->BackBufferWidth, pp->BackBufferHeight, pp->BackBufferFormat,
           pp->BackBufferCount,
           pp->EnableAutoDepthStencil ? "auto" : "none",
           pp->AutoDepthStencilFormat, pp->hDeviceWindow);

    if (!gpu_device_create()) {
        fprintf(stderr, "d3d8: the host GPU device could not be created, so "
                        "CreateDevice fails -- which is what the engine's own "
                        "error path is written for.\n");
        return NULL;
    }
    /* The engine's HWND is meaningless here; the host owns one SDL window and
       that is what the swapchain is claimed on. Same reasoning, and the same
       code path, as the --vk backend's setNativeWindowHandle. */
    gpu_device_set_window_provider(win32_sdl_window);
    if (!gpu_device_attach_window(win32_sdl_window()))
        fprintf(stderr, "d3d8: no host window to present into yet; the "
                        "swapchain will be claimed when one exists.\n");

    /*
     * The render destinations exist for the life of the device, because that
     * is what they are on real D3D8: CreateDevice makes the swapchain and,
     * when asked, the automatic depth buffer. Making them here rather than on
     * the first GetBackBuffer means that call is a lookup which cannot fail
     * for a reason the engine has to handle.
     */
    g_backbuffer = d3d8_surface_new(D3D8_SURF_BACKBUFFER,
                                    pp->BackBufferWidth, pp->BackBufferHeight,
                                    pp->BackBufferFormat, 1u /* RENDERTARGET */,
                                    0u /* D3DPOOL_DEFAULT */);
    if (!g_backbuffer) {
        fprintf(stderr, "d3d8: the back buffer surface could not be made.\n");
        return NULL;
    }
    g_render_target = g_backbuffer;
    if (pp->EnableAutoDepthStencil) {
        g_depth = d3d8_surface_new(D3D8_SURF_DEPTHSTENCIL,
                                   pp->BackBufferWidth, pp->BackBufferHeight,
                                   pp->AutoDepthStencilFormat,
                                   2u /* DEPTHSTENCIL */, 0u);
        if (!g_depth) {
            fprintf(stderr, "d3d8: the automatic depth/stencil surface could "
                            "not be made.\n");
            return NULL;
        }
        g_render_depth = g_depth;
    }

    g_dev_obj = d3d8_object_new(D3D8_IF_IDirect3DDevice8, &g_dev);
    d3d8_object_set_destructor(g_dev_obj, device_destroyed);
    printf("d3d8: IDirect3DDevice8 at 0x%08x\n", d3d8_object_guest(g_dev_obj));
    fflush(stdout);
    return g_dev_obj;
}

void d3d8_device_report(void)
{
    if (!g_dev_obj) {
        printf("  d3d8: no device was ever created -- the engine did not get "
               "as far as CreateDevice.\n");
        return;
    }
    printf("  d3d8: %lu scene(s) begun, %lu clear(s), %lu draw(s), %lu "
           "present(s)\n", g_dev.scenes, g_dev.clears, g_dev.draws,
           g_dev.presents);
    d3d8_state_report(&g_dev.state);
    d3d8_surface_report();
    d3d8_resource_report();
    d3d8_drawcall_report();
    gpu_draw_report();
    d3d8_object_report();
    gpu_device_report();
}
