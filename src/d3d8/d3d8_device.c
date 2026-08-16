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
#include "d3d8_stateblock.h"
#include "d3d8_host.h"
#include "d3d8_com.h"
#include "d3d8_caps.h"
#include "d3d8_state.h"
#include "d3d8_surface.h"
#include "d3d8_resource.h"
#include "d3d8_drawcall.h"
#include "d3d8_vertex_shader.h"
#include "d3d8_types.h"

#include "gpu_device.h"
#include "gpu_draw.h"
#include "win32_sdl.h"
#include "x86rt_native.h"   /* naming a guest return address */
#include "probe_rec.h"      /* probe_page_readable */

#include "x86rt.h"

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

/*
 * X2_LIGHTLOG=<path> -- the light path in the SAME FORMAT the stock control
 * writes, so the port and the original engine can be diffed line for line.
 *
 * tools/proxy_d3d8 is a logging d3d8.dll that the real game loads under Wine;
 * it records SetLight / LightEnable / SetMaterial and the two lighting render
 * states as it forwards them. This writes the identical lines from our side.
 * The pair is what tools/lightlog_diff.py compares.
 *
 * Why a second logger rather than reusing the existing counters: those answer
 * "what did the engine do", one question at a time, in our own wording. The
 * question left open by C199 is "does the ORIGINAL engine do the same", and
 * only a byte-comparable format makes that a diff instead of an argument.
 *
 * `t` is milliseconds since the first line, matching the control's tick
 * counter in UNITS but not in origin -- the two runs start at different
 * moments, so lightlog_diff.py never compares it and says so.
 */
/* The two states of the light path, by the numbers d3d8_state.c's own render
   state table gives them (and tools/proxy_d3d8/proxy.c repeats). */
#define D3D8_RS_LIGHTING 137
#define D3D8_RS_AMBIENT  139

static FILE *g_lightlog;
static int   g_lightlog_tried;
static long  g_lightlog_t0;

static long lightlog_ms(void)
{
    struct timespec ts;
    long ms;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    ms = (long)ts.tv_sec * 1000 + ts.tv_nsec / 1000000;
    if (!g_lightlog_t0) g_lightlog_t0 = ms;
    return ms - g_lightlog_t0;
}

static int lightlog_on(void)
{
    const char *path;
    if (g_lightlog_tried) return g_lightlog != NULL;
    g_lightlog_tried = 1;
    path = getenv("X2_LIGHTLOG");
    if (!path || !*path) return 0;
    g_lightlog = fopen(path, "w");
    if (!g_lightlog) {
        /* A log that silently did not open reads as "the engine set no
           lights", which is the very answer this is here to establish. */
        fprintf(stderr, "d3d8: X2_LIGHTLOG=%s could NOT be opened -- no light "
                        "log will be written by this run.\n", path);
        return 0;
    }
    setvbuf(g_lightlog, NULL, _IOLBF, 8192);   /* a kill keeps the tail */
    fprintf(stderr, "d3d8: X2_LIGHTLOG is writing %s\n", path);
    return 1;
}

static void lightlog(const char *fmt, ...) __attribute__((format(printf, 1, 2)));
static void lightlog(const char *fmt, ...)
{
    va_list ap;
    if (!lightlog_on()) return;
    va_start(ap, fmt);
    vfprintf(g_lightlog, fmt, ap);
    va_end(ap);
    fputc('\n', g_lightlog);
}

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
static void up_vertices_destroy(void);

static void *guest_ptr(uint32_t a, const char *what)
{
    if (!a) {
        fprintf(stderr, "d3d8: %s was given a NULL %s\n",
                d3d8_current_method(), what);
        return NULL;
    }
    return (void *)(uintptr_t)a;
}

/*
 * The device holds a REFERENCE on whatever is bound to it.
 *
 * D3D8 does, and it is not bookkeeping: the engine creates an index buffer per
 * mesh, binds it, draws, and releases it, expecting the device's own reference
 * to keep it alive until something else is bound. Without that reference the
 * release takes the object to zero, this host retires it and destroys its GPU
 * buffer -- and the still-bound guest pointer then resolves to a RECYCLED gpu
 * slot holding somebody else's, smaller, buffer. That is issue #38: a draw
 * asking for 204 indices out of a 152-byte buffer, one per frame, which was
 * the game's missing caption.
 *
 * Order matters: addref the new one BEFORE releasing the old, or binding a
 * resource to itself frees it.
 */
static void bind_ref(uint32_t *slot, uint32_t next)
{
    D3D8Object *o;
    if (*slot == next) return;
    if (next && (o = d3d8_object_from_guest(next)) != NULL)
        d3d8_object_addref(o);
    if (*slot && (o = d3d8_object_from_guest(*slot)) != NULL)
        d3d8_object_release(o);
    *slot = next;
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
    up_vertices_destroy();
    d3d8_vs_reset();
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
    {
        static int told;
        if (!told++) d3d8_caps_dump(caps, "IDirect3DDevice8::GetDeviceCaps");
    }
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

/*
 * The IDirect3D8 that made this device, with a reference of its own.
 *
 * This used to refuse, on the grounds that the AddRef could not be balanced.
 * Both halves of that were wrong. The object layer refcounts already --
 * Direct3DCreate8 AddRefs the singleton on every repeat call, exactly as the
 * real one does -- and refusing is not the safe option: the engine writes the
 * out-pointer and uses it, so INVALIDCALL with a NULL there became a SIGSEGV
 * at (nil) one call later. A refusal is only honest when the caller can act on
 * it, and this caller cannot.
 *
 * With no IDirect3D8 in existence there is genuinely nothing to hand back, and
 * that answer stays INVALIDCALL with a NULL out-pointer.
 */
static void dev_GetDirect3D(D3D8Object *self, CPU *C)
{
    uint32_t out = d3d8_arg(C, 0);
    (void)self;
    if (!out) { d3d8_ret(C, D3DERR_INVALIDCALL); return; }
    if (!d3d8_the_direct3d8_addref()) {
        fprintf(stderr, "d3d8: GetDirect3D, but no IDirect3D8 exists in this "
                        "process -- this device was not made by one.\n");
        WR32(out, 0);
        d3d8_ret(C, D3DERR_INVALIDCALL);
        return;
    }
    WR32(out, d3d8_the_direct3d8());
    d3d8_ret(C, D3D_OK);
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

/* ---- state blocks ------------------------------------------------------
 *
 * The device owns the live state, so these four are the only place a block and
 * the device meet; everything about the blocks themselves is in
 * src/d3d8/d3d8_stateblock.c.
 */
static void dev_CreateStateBlock(D3D8Object *self, CPU *C)
{
    uint32_t type = d3d8_arg(C, 0), out = d3d8_arg(C, 1);
    uint32_t token = 0;
    (void)self;
    if (!out) { d3d8_ret(C, D3DERR_INVALIDCALL); return; }
    if (!d3d8_sb_create(type, &g_dev.state, &token)) {
        /* The out-parameter is zeroed on failure: a caller that ignores the
           HRESULT would otherwise pass whatever was in that DWORD to Apply,
           and 0 is guaranteed not to name a block. */
        WR32(out, 0);
        d3d8_ret(C, D3DERR_INVALIDCALL);
        return;
    }
    WR32(out, token);
    d3d8_ret(C, D3D_OK);
}

static void dev_ApplyStateBlock(D3D8Object *self, CPU *C)
{
    /* Issue #38: the engine creates, applies and deletes exactly one block per
       frame, and exactly one draw per frame runs off the end of its index
       buffer. Whether Apply is what MOVES the index binding is the question,
       so the binding is printed on both sides of it. Capped: the answer is in
       the first frames or it is nowhere. */
    /* A block replaces the bindings wholesale, so the device's references have
       to follow it: the same invariant bind_ref keeps, re-established after
       the copy rather than during it. */
    uint32_t old_idx = g_dev.state.indices;
    uint32_t old_str[D3D8_MAX_STREAMS], old_tex[D3D8_MAX_STAGES];
    unsigned i;
    int ok;
    (void)self;
    for (i = 0; i < D3D8_MAX_STREAMS; i++)
        old_str[i] = g_dev.state.stream[i].guest_ptr;
    for (i = 0; i < D3D8_MAX_STAGES; i++)
        old_tex[i] = g_dev.state.texture[i];
    ok = d3d8_sb_apply(d3d8_arg(C, 0), &g_dev.state);
    if (ok) {
        uint32_t nw;
        nw = g_dev.state.indices; g_dev.state.indices = old_idx;
        bind_ref(&g_dev.state.indices, nw);
        for (i = 0; i < D3D8_MAX_STREAMS; i++) {
            nw = g_dev.state.stream[i].guest_ptr;
            g_dev.state.stream[i].guest_ptr = old_str[i];
            bind_ref(&g_dev.state.stream[i].guest_ptr, nw);
        }
        for (i = 0; i < D3D8_MAX_STAGES; i++) {
            nw = g_dev.state.texture[i]; g_dev.state.texture[i] = old_tex[i];
            bind_ref(&g_dev.state.texture[i], nw);
        }
    }
    d3d8_ret(C, ok ? D3D_OK : D3DERR_INVALIDCALL);
}

static void dev_CaptureStateBlock(D3D8Object *self, CPU *C)
{
    (void)self;
    d3d8_ret(C, d3d8_sb_capture(d3d8_arg(C, 0), &g_dev.state)
                ? D3D_OK : D3DERR_INVALIDCALL);
}

static void dev_DeleteStateBlock(D3D8Object *self, CPU *C)
{
    (void)self;
    d3d8_ret(C, d3d8_sb_delete(d3d8_arg(C, 0)) ? D3D_OK : D3DERR_INVALIDCALL);
}

static void dev_SetRenderState(D3D8Object *self, CPU *C)
{
    uint32_t which = d3d8_arg(C, 0), value = d3d8_arg(C, 1);
    (void)self;
    /* Only the two lighting states, as the control's proxy logs: the rest
       arrive at Present rate and would bury the light path. */
    if (which == D3D8_RS_LIGHTING)
        lightlog("SETRENDERSTATE t=%lu LIGHTING=%lu",
                 lightlog_ms(), (unsigned long)value);
    else if (which == D3D8_RS_AMBIENT)
        lightlog("SETRENDERSTATE t=%lu AMBIENT=%08lx",
                 lightlog_ms(), (unsigned long)value);
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
    /*
     * X2_MATERIAL_DUMP=<n> -- what the ENGINE actually sets.
     *
     * The level renders black because every lit draw arrives with a material
     * whose diffuse is 0,0,0 and a vertex format with no colour to stand in
     * for it. That is either what the engine asked for, or a mirror of ours
     * that has drifted -- and those need opposite fixes, so the value is
     * printed AT THE CALL rather than inferred from the draw.
     *
     * A run where the count reaches n and every diffuse is zero is a real
     * answer; so is one where it never fires, which says SetMaterial is not
     * the path this engine uses.
     */
    {
        static long want = -2, done;
        if (want == -2) {
            const char *e = getenv("X2_MATERIAL_DUMP");
            want = (e && *e) ? atol(e) : -1;
        }
        if (want > 0 && done < want) {
            done++;
            fprintf(stderr, "d3d8 SetMaterial %ld/%ld: diffuse %.3f %.3f %.3f "
                    "%.3f  ambient %.3f %.3f %.3f  emissive %.3f %.3f %.3f  "
                    "power %.2f\n", done, want,
                    m[0], m[1], m[2], m[3], m[4], m[5], m[6],
                    m[12], m[13], m[14], m[16]);
        }
    }
    lightlog("SETMATERIAL t=%lu diffuse=%.4f,%.4f,%.4f,%.4f "
             "ambient=%.4f,%.4f,%.4f,%.4f emissive=%.4f,%.4f,%.4f,%.4f "
             "specular=%.4f,%.4f,%.4f,%.4f power=%.2f", lightlog_ms(),
             m[0], m[1], m[2], m[3], m[4], m[5], m[6], m[7],
             m[12], m[13], m[14], m[15], m[8], m[9], m[10], m[11], m[16]);
    memcpy(g_dev.state.material, m, sizeof g_dev.state.material);
    g_dev.state.material_set = 1;
    d3d8_ret(C, D3D_OK);
}

/*
 * A light index this table cannot hold.
 *
 * The engine does not check SetLight's HRESULT -- nothing in D3D8 obliges it
 * to -- so a refusal here is INVISIBLE unless it is said. It was silent, and
 * with the table at 16 slots that silence swallowed 63% of the original
 * engine's SetLight calls (it uses indices up to 51, measured against the
 * stock game through tools/proxy_d3d8). Said by name the first few times, and
 * counted with its denominator in the report, so a light this device dropped
 * can never again look like a light the engine never set.
 */
static unsigned long g_light_idx_refused, g_light_idx_max_seen;
static unsigned long g_lightenable_calls;

static void light_index_refused(const char *what, uint32_t idx)
{
    static int told;
    g_light_idx_refused++;
    if (idx > g_light_idx_max_seen) g_light_idx_max_seen = idx;
    if (told < 8) {
        told++;
        fprintf(stderr, "d3d8: %s(%u) -- this device holds %d light(s) and the "
                        "engine asked for index %u. REFUSED; that light will "
                        "not reach any draw.\n",
                what, idx, D3D8_MAX_LIGHTS, idx);
    }
}

/* SetLight call sites, for "who sets a black light" -- see dev_SetLight. */
#define SETLIGHT_SITES 16
static struct { uint32_t ra; unsigned long calls, black; }
             g_setlight_site[SETLIGHT_SITES];
static int   g_nsetlight_site;
static unsigned long g_setlight_calls, g_setlight_black, g_setlight_over;
/* What the LAST SetLight left in each slot -- see dev_SetLight. */
static struct { unsigned long calls, black; float last[3]; uint32_t last_type; }
             g_light_slot[D3D8_MAX_LIGHTS];
/* Whether the engine's own light-position transform did anything -- see
   dev_SetLight. */
static unsigned long g_light_transformed, g_light_untransformed,
                     g_light_tail_unreadable;

static void dev_SetLight(D3D8Object *self, CPU *C)
{
    uint32_t idx = d3d8_arg(C, 0);
    const float *l = (const float *)guest_ptr(d3d8_arg(C, 1), "light");
    (void)self;
    /* Before the refusal, for the reason given at dev_LightEnable. */
    if (!l)
        lightlog("SETLIGHT t=%lu idx=%lu NULL-POINTER", lightlog_ms(),
                 (unsigned long)idx);
    else
        lightlog("SETLIGHT t=%lu idx=%lu type=%u "
                 "diffuse=%.4f,%.4f,%.4f,%.4f specular=%.4f,%.4f,%.4f,%.4f "
                 "ambient=%.4f,%.4f,%.4f,%.4f pos=%.2f,%.2f,%.2f "
                 "dir=%.3f,%.3f,%.3f range=%.2f falloff=%.2f "
                 "atten=%.6f,%.6f,%.8f theta=%.3f phi=%.3f",
                 lightlog_ms(), (unsigned long)idx, ((const uint32_t *)l)[0],
                 l[1], l[2], l[3], l[4], l[5], l[6], l[7], l[8],
                 l[9], l[10], l[11], l[12], l[13], l[14], l[15],
                 l[16], l[17], l[18], l[19], l[20], l[21], l[22], l[23],
                 l[24], l[25]);
    if (idx >= D3D8_MAX_LIGHTS) light_index_refused("SetLight", idx);
    if (!l || idx >= D3D8_MAX_LIGHTS) { d3d8_ret(C, D3DERR_INVALIDCALL); return; }
    /*
     * X2_LIGHT_RAW=<n> -- the D3DLIGHT8 as WORDS, before any interpretation.
     *
     * A level light came back with range 1.8446743e19 (about 2^64), which is
     * not a range any engine sets; that is what a misread field looks like.
     * The only way to settle a struct layout is to print the words and see
     * where the recognisable ones fall: Position and Direction are the giveaway
     * because they must be plausible world coordinates and a unit vector.
     * Type is printed as an integer because it IS one -- D3DLIGHT8 begins with
     * a DWORD, and a float view of 1 or 3 prints as 0.000 and looks like a
     * black colour channel.
     */
    {
        static long want = -2, done;
        if (want == -2) {
            const char *e = getenv("X2_LIGHT_RAW");
            want = (e && *e) ? atol(e) : -1;
        }
        if (want > 0 && done < want) {
            int k;
            done++;
            fprintf(stderr, "d3d8 SetLight[%u] raw %ld/%ld: type=%u then floats:",
                    idx, done, want, ((const uint32_t *)l)[0]);
            for (k = 1; k < 26; k++)
                fprintf(stderr, "%s[%d]%.4g", (k % 8 == 1) ? "\n    " : " ",
                        k, (double)l[k]);
            fprintf(stderr, "\n");
        }
    }
    /*
     * WHO SETS A BLACK LIGHT.
     *
     * The red chamber arrives with four point lights that have real positions,
     * real quadratic attenuation and a diffuse of exactly zero, which is not
     * something a level author places -- so the colour is lost UPSTREAM of
     * here and the question is which engine function hands it over. The word
     * at ESP is the guest return address (every emitted call site pushes one),
     * so grouping by it names the caller.
     *
     * Kept as a histogram rather than a line per call: this is called several
     * times a frame for the life of the run. Reported at exit ALWAYS, at zero
     * and with its denominator, so "no black light was ever set" and "the
     * counter never ran" cannot look the same.
     */
    {
        uint32_t ra = RD32(C->esp);
        int black = l[1] == 0.0f && l[2] == 0.0f && l[3] == 0.0f;
        int i;
        g_setlight_calls++;
        if (black) g_setlight_black++;
        for (i = 0; i < g_nsetlight_site; i++)
            if (g_setlight_site[i].ra == ra) break;
        if (i == g_nsetlight_site && i < SETLIGHT_SITES)
            g_setlight_site[g_nsetlight_site++].ra = ra;
        if (i < SETLIGHT_SITES) {
            g_setlight_site[i].calls++;
            if (black) g_setlight_site[i].black++;
        } else {
            g_setlight_over++;
        }
    }
    /*
     * PER INDEX, because the draw reads one index at a time.
     *
     * The run-wide total says 159 of 153,907 SetLights were black, and a draw
     * that finds four black point lights is not explained by that: what
     * matters is what the LAST call to each index left behind. Counting per
     * index turns "black lights come from somewhere" into "index 5 was set
     * 2,000 times and the last one was black".
     */
    /*
     * DID THE ENGINE'S OWN TRANSFORM RUN?
     *
     * libIGGfx setLightPosition (0x1003d5e0) does not store the position it is
     * given. It multiplies it by the top of a matrix stack and stores the
     * RESULT at D3DLIGHT8+0x34, keeping the untransformed vector 0x34 bytes
     * further on in the same record. Those two being EQUAL means the transform
     * was a no-op -- an identity or empty stack -- which would leave every
     * light in the wrong space while the geometry's world matrix is in
     * another, and that is exactly the shape of "everything lit is black".
     *
     * The record is the engine's, not ours, so the tail is validated as guest
     * memory in its own right before it is read; a light whose tail is not
     * addressable is counted rather than assumed either way.
     */
    if (idx < D3D8_MAX_LIGHTS) {
        const float *tail =
            (const float *)guest_ptr(d3d8_arg(C, 1) + 0x68u, NULL);
        if (!tail) {
            g_light_tail_unreadable++;
        } else if (l[13] == tail[0] && l[14] == tail[1] && l[15] == tail[2]) {
            g_light_untransformed++;
        } else {
            g_light_transformed++;
        }
    }
    if (idx < D3D8_MAX_LIGHTS) {
        g_light_slot[idx].calls++;
        if (l[1] == 0.0f && l[2] == 0.0f && l[3] == 0.0f)
            g_light_slot[idx].black++;
        g_light_slot[idx].last[0] = l[1];
        g_light_slot[idx].last[1] = l[2];
        g_light_slot[idx].last[2] = l[3];
        g_light_slot[idx].last_type = ((const uint32_t *)l)[0];
    }
    /*
     * X2_LIGHT_ADDR=1 -- the GUEST ADDRESS the engine handed this light at.
     *
     * Ground truth for tools/light_probe.py. A matcher that finds D3DLIGHT8
     * records by shape has to be checked against a case it MUST find, in real
     * program memory rather than in random bytes: validated against random
     * bytes alone it had zero false positives and then matched six million
     * times in a live process, because real memory is full of 0.0 and 1.0
     * floats. These addresses are the positive control.
     */
    {
        static int addr_on = -1, told;
        if (addr_on < 0) {
            const char *e = getenv("X2_LIGHT_ADDR");
            addr_on = (e && *e) ? atoi(e) : 0;
        }
        if (addr_on && told < 24) {
            told++;
            fprintf(stderr, "[LIGHT ADDR] index %u at guest 0x%08x, type %u, "
                    "diffuse %.4f %.4f %.4f\n",
                    idx, d3d8_arg(C, 1), ((const uint32_t *)l)[0],
                    l[1], l[2], l[3]);
        }
    }
    memcpy(g_dev.state.light[idx], l, sizeof g_dev.state.light[0]);
    g_dev.state.light_set[idx] = 1;
    d3d8_ret(C, D3D_OK);
}

/*
 * What the most recent SetLight left in this slot.
 *
 * The draw path reads the light table that SetLight writes, so the two cannot
 * disagree -- unless something between them changes it. A gameplay frame shows
 * four enabled point lights with a diffuse of 0.00 while the engine's own
 * calls for those indices carry real colours, and only a comparison AT DRAW
 * TIME can tell "the engine blacked them just now" from "we lost the colour".
 * Returns 0 when the slot has never been set, which is a third answer and not
 * the same as either.
 */
int d3d8_last_setlight_diffuse(unsigned idx, float out[3])
{
    if (idx >= D3D8_MAX_LIGHTS || !g_light_slot[idx].calls) return 0;
    out[0] = g_light_slot[idx].last[0];
    out[1] = g_light_slot[idx].last[1];
    out[2] = g_light_slot[idx].last[2];
    return 1;
}

void d3d8_setlight_report(void)
{
    int i;
    printf("  d3d8 SetLight: %lu call(s), %lu of them with a BLACK diffuse, "
           "from %d distinct call site(s)%s\n",
           g_setlight_calls, g_setlight_black, g_nsetlight_site,
           g_setlight_over ? " (the site table is FULL -- some are not listed)"
                           : "");
    /* AT ZERO and with its denominator: a line that appears only when
       something is wrong cannot be told from a check that never ran. */
    printf("         %lu of %lu SetLight/LightEnable call(s) named an index "
           "this device cannot hold (capacity %d, highest asked for %lu)\n",
           g_light_idx_refused, g_setlight_calls + g_lightenable_calls,
           D3D8_MAX_LIGHTS,
           g_light_idx_refused ? g_light_idx_max_seen : 0UL);
    if (!g_setlight_calls) {
        printf("         SetLight was never called, so this run says nothing "
               "about where light colour comes from.\n");
        return;
    }
    printf("         the engine transforms a light POSITION by the top of its "
           "own matrix stack before handing it over (libIGGfx 0x1003d5e0) and "
           "keeps the untransformed vector beside it:\n"
           "         %lu call(s) arrived TRANSFORMED, %lu arrived with the two "
           "equal (the transform was a no-op), %lu had no readable record "
           "tail.\n",
           g_light_transformed, g_light_untransformed, g_light_tail_unreadable);
    for (i = 0; i < (int)D3D8_MAX_LIGHTS; i++) {
        if (!g_light_slot[i].calls) continue;
        printf("         light[%d] %lu call(s), %lu black; LAST was type %u "
               "diffuse %.3f %.3f %.3f%s\n",
               i, g_light_slot[i].calls, g_light_slot[i].black,
               g_light_slot[i].last_type,
               g_light_slot[i].last[0], g_light_slot[i].last[1],
               g_light_slot[i].last[2],
               (g_light_slot[i].last[0] == 0.0f && g_light_slot[i].last[1] == 0.0f
                && g_light_slot[i].last[2] == 0.0f) ? "   <- BLACK" : "");
    }
    for (i = 0; i < g_nsetlight_site; i++) {
        uint32_t ra = g_setlight_site[i].ra;
        const char *nm = x86_native_name_at(ra);
        X86Module *rm = x86_module_for(ra);
        printf("         0x%08x  %lu call(s), %lu black", ra,
               g_setlight_site[i].calls, g_setlight_site[i].black);
        if (nm)
            printf("  -- %s\n", nm);
        else if (rm)
            printf("  -- inside %s at guest 0x%08x, not at a named body\n",
                   rm->name, rm->preferred + (ra - *rm->base));
        else
            printf("  -- in NO module; the return address is not trustworthy\n");
    }
}

static void dev_LightEnable(D3D8Object *self, CPU *C)
{
    uint32_t idx = d3d8_arg(C, 0), on = d3d8_arg(C, 1);
    (void)self;
    /* Logged BEFORE the range refusal: an index we refuse and the control
       accepts is exactly the kind of difference this log exists to show, and
       it cannot show it from behind the refusal. */
    lightlog("LIGHTENABLE t=%lu idx=%lu on=%d", lightlog_ms(),
             (unsigned long)idx, on != 0);
    g_lightenable_calls++;
    if (idx >= D3D8_MAX_LIGHTS) {
        light_index_refused("LightEnable", idx);
        d3d8_ret(C, D3DERR_INVALIDCALL);
        return;
    }
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

static void dev_CreateCubeTexture(D3D8Object *self, CPU *C)
{
    uint32_t size = d3d8_arg(C, 0), levels = d3d8_arg(C, 1);
    uint32_t usage = d3d8_arg(C, 2), fmt = d3d8_arg(C, 3), pool = d3d8_arg(C, 4);
    uint32_t out = d3d8_arg(C, 5);
    D3D8Object *t;

    (void)self;
    if (!out) { d3d8_ret(C, D3DERR_INVALIDCALL); return; }
    t = d3d8_cubetexture_new(size, levels, usage, fmt, pool);
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
    bind_ref(&g_dev.state.texture[stage], tex);
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
    bind_ref(&g_dev.state.stream[stream].guest_ptr, buf);
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
    bind_ref(&g_dev.state.indices, buf);
    g_dev.state.base_vertex_index = base;
    d3d8_ret(C, D3D_OK);
}

/*
 * Every DISTINCT value SetVertexShader is handed, with its count.
 *
 * D3D8 overloads this call: the argument is an FVF code OR a vertex shader
 * handle, and the two are told apart by bit 0 (D3DFVF_RESERVED0), which an
 * FVF may never set. Measured against the stock engine through
 * tools/proxy_d3d8, the control binds handle 0x003 for two draws of every
 * gameplay frame -- the character hulls -- while this port bound 0x002 for
 * the same two draws and created no shader at all in the whole run.
 *
 * That divergence was invisible because nothing recorded what this call
 * RECEIVED: "0 vertex shaders created" is equally consistent with an engine
 * that wanted none and an engine that asked and was turned away somewhere
 * upstream. The census makes the two distinguishable, and prints AT ZERO.
 */
#define VS_SEEN_MAX 16
static struct { uint32_t handle; unsigned long n; } g_vs_seen[VS_SEEN_MAX];
static int g_vs_nseen;
static unsigned long g_vs_seen_dropped;

static void vs_seen_note(uint32_t handle)
{
    int i;
    for (i = 0; i < g_vs_nseen; i++)
        if (g_vs_seen[i].handle == handle) { g_vs_seen[i].n++; return; }
    if (g_vs_nseen == VS_SEEN_MAX) { g_vs_seen_dropped++; return; }
    g_vs_seen[g_vs_nseen].handle = handle;
    g_vs_seen[g_vs_nseen].n = 1;
    g_vs_nseen++;
}

/* The same thing in one line, for the heartbeat -- nothing here stops on its
   own, so a census that only prints at shutdown is one a killed run loses.
   That has already cost one hand-driven run's answer. */
void d3d8_vertex_shader_binding_line(char *buf, size_t n)
{
    int i;
    size_t at = 0;
    unsigned shaders = 0;
    for (i = 0; i < g_vs_nseen; i++)
        if (g_vs_seen[i].handle & 1u) shaders++;
    at += (size_t)snprintf(buf + at, n - at,
                           "%d distinct SetVertexShader value(s), %u of them "
                           "SHADER handles:", g_vs_nseen, shaders);
    if (!g_vs_nseen)
        snprintf(buf + at, n - at, " the engine bound NONE");
    for (i = 0; i < g_vs_nseen && at + 20 < n; i++)
        at += (size_t)snprintf(buf + at, n - at, " 0x%x%s",
                               g_vs_seen[i].handle,
                               (g_vs_seen[i].handle & 1u) ? "*" : "");
}

void d3d8_vertex_shader_binding_report(void)
{
    int i;
    printf("  d3d8 SetVertexShader: %d distinct value(s)", g_vs_nseen);
    if (g_vs_seen_dropped)
        printf(", and %lu call(s) with further values this table could not "
               "hold", g_vs_seen_dropped);
    printf("\n");
    if (!g_vs_nseen) {
        printf("        the engine NEVER bound a vertex format or a shader. "
               "Every draw used whatever was current at device creation.\n");
        return;
    }
    for (i = 0; i < g_vs_nseen; i++) {
        uint32_t h = g_vs_seen[i].handle;
        /* D3D8's own rule, not a threshold: bit 0 is D3DFVF_RESERVED0 and an
           FVF may never set it, so a handle with it set is a SHADER. */
        printf("        0x%08x  x%-8lu  %s\n", h, g_vs_seen[i].n,
               (h & 1u) ? "a SHADER HANDLE (D3DFVF_RESERVED0 set)"
                        : "an FVF code");
    }
}

static void dev_SetVertexShader(D3D8Object *self, CPU *C)
{
    uint32_t shader = d3d8_arg(C, 0);
    (void)self;
    vs_seen_note(shader);
    if (shader > 0xf0000000u && !d3d8_vs_get(shader, "SetVertexShader")) {
        d3d8_ret(C, D3DERR_INVALIDCALL);
        return;
    }
    g_dev.state.vertex_shader = shader;
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

static void dev_CreateVertexShader(D3D8Object *self, CPU *C)
{
    const uint32_t *decl = (const uint32_t *)guest_ptr(d3d8_arg(C, 0),
                                                       "vertex declaration");
    const uint32_t *code = (const uint32_t *)guest_ptr(d3d8_arg(C, 1),
                                                       "vertex shader bytecode");
    uint32_t *out = (uint32_t *)guest_ptr(d3d8_arg(C, 2), "shader handle");
    uint32_t handle;
    (void)self;
    if (!decl || !out) { d3d8_ret(C, D3DERR_INVALIDCALL); return; }
    handle = d3d8_vs_create(decl, code, d3d8_arg(C, 3));
    *out = handle;
    d3d8_ret(C, handle ? D3D_OK : D3DERR_INVALIDCALL);
}

static void dev_DeleteVertexShader(D3D8Object *self, CPU *C)
{
    uint32_t h = d3d8_arg(C, 0);
    (void)self;
    if (g_dev.state.vertex_shader == h) g_dev.state.vertex_shader = 0;
    d3d8_ret(C, d3d8_vs_delete(h) ? D3D_OK : D3DERR_INVALIDCALL);
}

/*
 * WHO fills the bone palette.
 *
 * The palette arrives here already non-rigid (issue #80), and the oracle
 * probes have now cleared everything in libIGMath that could have produced it
 * -- slerp and getMatrix agree with the real engine on six thousand distinct
 * inputs, and the five concatenation entry points are never called at all. So
 * the corruption happens in whatever assembles this block, and the fastest way
 * to name that function is to ask the guest: its return address is sitting on
 * the stack this method was called with.
 *
 * A census, not a log line per call -- there are tens of thousands of these
 * per run. Distinct call sites only, with a count each, resolved to the
 * function containing them. Printed AT ZERO with its denominator, because "no
 * call site was identified" and "this never ran" must not look the same.
 */
#define VSC_SITES 16
#define VSC_DEPTH 6
static struct { uint32_t ret; unsigned long n; uint32_t lo, hi;
                uint32_t up[VSC_DEPTH]; int nup; } g_vsc[VSC_SITES];
static int g_nvsc;
static unsigned long g_vsc_calls, g_vsc_overflow;

/*
 * Walk the guest stack for the callers above this one.
 *
 * setVertexShaderConstant_Dx turns out to be reached through a function
 * pointer -- nothing in libIGGfx CALLs it directly and it is in no vtable this
 * export knows about -- so the one return address on the stack names a thin
 * wrapper and not the code that assembled the palette.
 *
 * This is a SCAN, not an unwind: the guest has no frame pointer to trust, so
 * it reads words off the stack and keeps the ones that land inside a known
 * function in a mapped module. That admits false positives -- a stale return
 * address left in a dead frame looks exactly like a live one -- so the result
 * is a lead to confirm, and the report says so rather than presenting it as a
 * call chain. It is still the difference between a name and nothing.
 */
static void vsc_walk(CPU *C, uint32_t *out, int *nout)
{
    uint32_t sp;
    int n = 0;
    for (sp = C->esp + 4u; sp < C->esp + 4u + 512u && n < VSC_DEPTH; sp += 4u) {
        const char *nm = NULL;
        uint32_t w, ep;
        /* Reuse the oracle recorder's page check rather than a second
           copy of it -- the last duplicate of this logic silently made
           every read fail (issue #80's capture). */
        if (!probe_page_readable(sp & ~4095u)) break;
        w = RD32(sp);
        if (!x86_module_for(w)) continue;
        ep = x86_native_entry_containing(w, &nm);
        if (!ep || !nm) continue;
        if (n && out[n - 1] == ep) continue;         /* same frame twice */
        out[n++] = ep;
    }
    *nout = n;
}

static void vsc_note(uint32_t ret, uint32_t first, uint32_t count)
{
    int i;
    g_vsc_calls++;
    for (i = 0; i < g_nvsc; i++)
        if (g_vsc[i].ret == ret) {
            g_vsc[i].n++;
            if (first < g_vsc[i].lo) g_vsc[i].lo = first;
            if (first + count > g_vsc[i].hi) g_vsc[i].hi = first + count;
            return;
        }
    if (g_nvsc == VSC_SITES) { g_vsc_overflow++; return; }
    g_vsc[g_nvsc].ret = ret;
    g_vsc[g_nvsc].n = 1;
    g_vsc[g_nvsc].lo = first;
    g_vsc[g_nvsc].hi = first + count;
    g_nvsc++;
}

int d3d8_vsconst_caller_line(char *buf, int n)
{
    return snprintf(buf, (size_t)n,
                    "SetVertexShaderConstant: %lu call(s) from %d distinct "
                    "site(s)%s", g_vsc_calls, g_nvsc,
                    g_vsc_overflow ? " (MORE than the census holds)" : "");
}

void d3d8_vsconst_caller_report(void)
{
    int i;
    fprintf(stderr, "SetVertexShaderConstant: %lu call(s) from %d distinct "
                    "call site(s) (census holds %d):\n",
            g_vsc_calls, g_nvsc, VSC_SITES);
    if (!g_nvsc)
        fprintf(stderr, "  NONE. Either no shader constant was ever set, or "
                        "the census never ran -- not that the callers are "
                        "unknown.\n");
    for (i = 0; i < g_nvsc; i++) {
        const char *nm = NULL;
        uint32_t ep = x86_native_entry_containing(g_vsc[i].ret, &nm);
        X86Module *m = x86_module_for(g_vsc[i].ret);
        int k;
        fprintf(stderr, "  ret 0x%08x  %10lu call(s)  c[%u..%u]  %s%s+0x%x  %s\n",
                g_vsc[i].ret, g_vsc[i].n, g_vsc[i].lo, g_vsc[i].hi,
                m ? m->name : "?", m ? "!" : "",
                m ? (unsigned)(ep - *m->base) : 0u, nm ? nm : "(unnamed)");
        if (!g_vsc[i].nup)
            fprintf(stderr, "      (no caller above it was identified on the "
                            "stack -- not that there is none)\n");
        for (k = 0; k < g_vsc[i].nup; k++) {
            const char *un = NULL;
            X86Module *um = x86_module_for(g_vsc[i].up[k]);
            x86_native_entry_containing(g_vsc[i].up[k], &un);
            fprintf(stderr, "      above [%d] %s+0x%x  %s\n", k,
                    um ? um->name : "?",
                    um ? (unsigned)(g_vsc[i].up[k] - *um->base) : 0u,
                    un ? un : "(unnamed)");
        }
        fprintf(stderr, "      ^ a STACK SCAN, not an unwind: a stale return "
                        "address in a dead frame reads the same as a live "
                        "one. Leads to confirm, not a call chain.\n");
    }
    if (g_vsc_overflow)
        fprintf(stderr, "  and %lu call(s) from sites past the census.\n",
                g_vsc_overflow);
}

static void dev_SetVertexShaderConstant(D3D8Object *self, CPU *C)
{
    uint32_t first = d3d8_arg(C, 0), count = d3d8_arg(C, 2);
    const float *data = (const float *)guest_ptr(d3d8_arg(C, 1), "constants");
    (void)self;
    if (!data || first >= D3D8_MAX_VS_CONSTANTS
            || count > D3D8_MAX_VS_CONSTANTS - first) {
        d3d8_ret(C, D3DERR_INVALIDCALL); return;
    }
    /* RD32(C->esp) is the guest return address: this method has not popped
       anything yet, so it is the word the CALL pushed. */
    vsc_note(RD32(C->esp), first, count);
    if (g_nvsc && !g_vsc[g_nvsc - 1].nup)
        vsc_walk(C, g_vsc[g_nvsc - 1].up, &g_vsc[g_nvsc - 1].nup);
    memcpy(g_dev.state.vertex_shader_constant[first], data,
           count * 4u * sizeof(float));
    d3d8_ret(C, D3D_OK);
}

static void dev_GetVertexShaderConstant(D3D8Object *self, CPU *C)
{
    uint32_t first = d3d8_arg(C, 0), count = d3d8_arg(C, 2);
    float *data = (float *)guest_ptr(d3d8_arg(C, 1), "constants");
    (void)self;
    if (!data || first >= D3D8_MAX_VS_CONSTANTS
            || count > D3D8_MAX_VS_CONSTANTS - first) {
        d3d8_ret(C, D3DERR_INVALIDCALL); return;
    }
    memcpy(data, g_dev.state.vertex_shader_constant[first],
           count * 4u * sizeof(float));
    d3d8_ret(C, D3D_OK);
}

static void vertex_shader_bytes(CPU *C, int function)
{
    D3D8VertexShader *s = d3d8_vs_get(d3d8_arg(C, 0),
            function ? "GetVertexShaderFunction" : "GetVertexShaderDeclaration");
    uint8_t *data = (uint8_t *)(uintptr_t)d3d8_arg(C, 1);
    uint32_t *size = (uint32_t *)guest_ptr(d3d8_arg(C, 2), "byte count");
    const uint32_t *src;
    size_t need;
    if (!s || !size) { d3d8_ret(C, D3DERR_INVALIDCALL); return; }
    src = function ? d3d8_vs_function(s, &need) : d3d8_vs_declaration(s, &need);
    if (!data) { *size = (uint32_t)need; d3d8_ret(C, D3D_OK); return; }
    if (*size < need) { d3d8_ret(C, D3DERR_INVALIDCALL); return; }
    if (need) memcpy(data, src, need);
    d3d8_ret(C, D3D_OK);
}

static void dev_GetVertexShaderDeclaration(D3D8Object *self, CPU *C)
{ (void)self; vertex_shader_bytes(C, 0); }
static void dev_GetVertexShaderFunction(D3D8Object *self, CPU *C)
{ (void)self; vertex_shader_bytes(C, 1); }

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

/*
 * `indexed` is not a convenience -- it is the bug this parameter exists to
 * stop.
 *
 * SetIndices is STATE: it stays bound across draws, and DrawPrimitive (which
 * takes no indices) must ignore it. Carrying it into every draw made the
 * backend take its indexed path for a non-indexed draw, so a 202-primitive
 * strip -- 204 vertices -- was drawn as 204 INDICES out of whatever index
 * buffer happened to be bound, which held 76. That was issue #38: one draw per
 * frame reading off the end of a buffer, and it was the game's missing
 * caption.
 */
static unsigned long g_texture_unresolved;

void d3d8_device_texture_unresolved(unsigned long *n)
{ *n = g_texture_unresolved; }

static int fill_request(D3D8DrawRequest *req, uint32_t prim, uint32_t count,
                        int indexed, int require_vb)
{
    D3D8Object *vb = g_dev.state.stream[0].guest_ptr
                         ? d3d8_object_from_guest(g_dev.state.stream[0].guest_ptr)
                         : NULL;
    D3D8Object *ib = g_dev.state.indices
                         ? d3d8_object_from_guest(g_dev.state.indices) : NULL;
    D3D8Object *tx = g_dev.state.texture[0]
                         ? d3d8_object_from_guest(g_dev.state.texture[0]) : NULL;

    memset(req, 0, sizeof *req);
    if (!vb && require_vb) {
        fprintf(stderr, "d3d8: a draw with no vertex buffer on stream 0.\n");
        return 0;
    }
    if (vb) {
        d3d8_resource_note_drawn(vb);
        req->vertex_buffer = d3d8_resource_buffer(vb);
        req->vertex_guest_bytes = d3d8_resource_guest_bytes(vb);
        req->vertex_bytes = d3d8_resource_bytes(vb);
        req->stride = g_dev.state.stream[0].stride;
    }
    if (indexed && ib) {
        d3d8_resource_note_drawn(ib);
        req->index_buffer = d3d8_resource_buffer(ib);
        req->index_is_32bit = d3d8_resource_index_is_32bit(ib);
        req->index_guest_bytes = d3d8_resource_guest_bytes(ib);
        req->index_bytes = d3d8_resource_bytes(ib);
    }
    /*
     * A texture the guest BOUND but this host could not resolve is a draw that
     * comes out untextured with nothing to say so.
     *
     * "74,251 draws with no texture bound" is a fine number if the engine
     * really bound none, and a serious defect if it bound one and the lookup
     * failed -- and the two are indistinguishable from the count alone. Three
     * ways this can happen: the guest pointer names no object of ours, the
     * object is not a texture, or it is a texture whose GPU resource was never
     * created (a format that was refused). All three are counted, and the
     * first of each is named.
     */
    if (tx) {
        req->texture = d3d8_resource_texture(tx);
        if (!req->texture) {
            static int told;
            g_texture_unresolved++;
            if (!told++)
                fprintf(stderr, "d3d8: a texture bound at stage 0 (guest "
                                "0x%08x) has no GPU resource -- its creation "
                                "was refused, and this draw is UNTEXTURED "
                                "rather than refused. Reported once; the total "
                                "is in the shutdown report.\n",
                        g_dev.state.texture[0]);
        }
    } else if (g_dev.state.texture[0]) {
        static int told;
        g_texture_unresolved++;
        if (!told++)
            fprintf(stderr, "d3d8: the guest bound 0x%08x at texture stage 0 "
                            "and this host has no object at that address, so "
                            "the draw is UNTEXTURED. Reported once.\n",
                    g_dev.state.texture[0]);
    }
    req->primitive_type = prim;
    req->primitive_count = count;
    return 1;
}

static void dev_DrawPrimitive(D3D8Object *self, CPU *C)
{
    D3D8DrawRequest req;
    GpuDraw gd;

    (void)self;
    if (!fill_request(&req, d3d8_arg(C, 0), d3d8_arg(C, 2), 0, 1)) {
        d3d8_ret(C, D3DERR_INVALIDCALL);
        return;
    }
    req.first_vertex = d3d8_arg(C, 1);
    if (!d3d8_build_draw(&g_dev.state, &req, &gd)) {
        d3d8_ret(C, D3DERR_INVALIDCALL);
        return;
    }
    g_dev.draws += gpu_draw(&gd) ? 1u : 0u;
    d3d8_release_draw(&gd);
    d3d8_ret(C, D3D_OK);
}

static void dev_DrawIndexedPrimitive(D3D8Object *self, CPU *C)
{
    D3D8DrawRequest req;
    GpuDraw gd;

    (void)self;
    /* (PrimitiveType, MinIndex, NumVertices, StartIndex, PrimitiveCount) */
    if (!fill_request(&req, d3d8_arg(C, 0), d3d8_arg(C, 4), 1, 1)) {
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
    req.min_index = d3d8_arg(C, 1);
    req.num_vertices = d3d8_arg(C, 2);
    if (!d3d8_build_draw(&g_dev.state, &req, &gd)) {
        d3d8_ret(C, D3DERR_INVALIDCALL);
        return;
    }
    g_dev.draws += gpu_draw(&gd) ? 1u : 0u;
    d3d8_release_draw(&gd);
    d3d8_ret(C, D3D_OK);
}

/*
 * DrawPrimitiveUP is not a second rendering path. D3D8 copies the caller's
 * transient vertex bytes before returning, then executes the same current
 * device state as DrawPrimitive. Keep one upload buffer and feed that exact
 * state through d3d8_build_draw, so shader/FVF handling cannot diverge.
 */
static GpuBuffer g_up_vertices;
static uint32_t g_up_capacity;

static void up_vertices_destroy(void)
{
    if (g_up_vertices) gpu_buffer_destroy(g_up_vertices);
    g_up_vertices = 0;
    g_up_capacity = 0;
}

static int primitive_vertices(uint32_t prim, uint32_t count, uint32_t *out)
{
    uint64_t n;
    switch (prim) {
    case D3DPT_POINTLIST:     n = count; break;
    case D3DPT_LINELIST:      n = (uint64_t)count * 2u; break;
    case D3DPT_LINESTRIP:     n = (uint64_t)count + 1u; break;
    case D3DPT_TRIANGLELIST:  n = (uint64_t)count * 3u; break;
    case D3DPT_TRIANGLESTRIP:
    case D3DPT_TRIANGLEFAN:   n = (uint64_t)count + 2u; break;
    default:
        fprintf(stderr, "d3d8: DrawPrimitiveUP primitive type %u is outside "
                        "D3D8's 1..6 range.\n", prim);
        return 0;
    }
    if (n > UINT32_MAX) {
        fprintf(stderr, "d3d8: DrawPrimitiveUP vertex count overflow for %u "
                        "primitive(s) of type %u.\n", count, prim);
        return 0;
    }
    *out = (uint32_t)n;
    return 1;
}

static void dev_DrawPrimitiveUP(D3D8Object *self, CPU *C)
{
    uint32_t prim = d3d8_arg(C, 0), count = d3d8_arg(C, 1);
    const void *data = guest_ptr(d3d8_arg(C, 2), "inline vertex data");
    uint32_t stride = d3d8_arg(C, 3), vertices, bytes;
    D3D8DrawRequest req;
    GpuDraw gd;
    (void)self;
    if (!data || !stride || !primitive_vertices(prim, count, &vertices)
            || vertices > UINT32_MAX / stride) {
        d3d8_ret(C, D3DERR_INVALIDCALL); return;
    }
    bytes = vertices * stride;
    if (bytes > g_up_capacity) {
        if (g_up_vertices) gpu_buffer_destroy(g_up_vertices);
        g_up_vertices = gpu_buffer_create(GPU_BUF_VERTEX, bytes);
        g_up_capacity = g_up_vertices ? bytes : 0;
    }
    if (!g_up_vertices || !gpu_buffer_upload(g_up_vertices, 0, data, bytes)
            || !fill_request(&req, prim, count, 0, 0)) {
        d3d8_ret(C, D3DERR_INVALIDCALL); return;
    }
    req.vertex_buffer = g_up_vertices;
    req.vertex_guest_bytes = (uint32_t)(uintptr_t)data;
    req.vertex_bytes = bytes;
    req.stride = stride;
    if (!d3d8_build_draw(&g_dev.state, &req, &gd)) {
        d3d8_ret(C, D3DERR_INVALIDCALL); return;
    }
    g_dev.draws += gpu_draw(&gd) ? 1u : 0u;
    d3d8_release_draw(&gd);
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
    dev_CreateCubeTexture,              /* 22 */
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
    dev_ApplyStateBlock,                /* 54 */
    dev_CaptureStateBlock,              /* 55 */
    dev_DeleteStateBlock,               /* 56 */
    dev_CreateStateBlock,               /* 57 */
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
    dev_DrawPrimitiveUP,                /* 72 */
    NULL,                               /* 73 DrawIndexedPrimitiveUP */
    NULL,                               /* 74 ProcessVertices */
    dev_CreateVertexShader,             /* 75 */
    dev_SetVertexShader,                /* 76 */
    dev_GetVertexShader,                /* 77 */
    dev_DeleteVertexShader,              /* 78 */
    dev_SetVertexShaderConstant,         /* 79 */
    dev_GetVertexShaderConstant,         /* 80 */
    dev_GetVertexShaderDeclaration,      /* 81 */
    dev_GetVertexShaderFunction,         /* 82 */
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

int d3d8_device_counts(unsigned long *scenes, unsigned long *presents,
                       unsigned long *clears, unsigned long *draws)
{
    *scenes = *presents = *clears = *draws = 0;
    if (!g_dev_obj) return 0;
    *scenes   = g_dev.scenes;
    *presents = g_dev.presents;
    *clears   = g_dev.clears;
    *draws    = g_dev.draws;
    return 1;
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
    /*
     * Printed at ZERO too, because zero is the interesting value.
     *
     * The stage report counts draws with "no texture bound", and on its own
     * that number cannot distinguish an engine that bound none from a host
     * that failed to resolve what was bound -- the second is a silently
     * untextured surface. This is the second half of that number, and stating
     * it as 0 of N is what makes the first half readable.
     */
    printf("        %lu draw(s) had a texture BOUND that this host could not "
           "resolve (of %lu draws) -- those are untextured with nothing to "
           "show for it\n", g_texture_unresolved, g_dev.draws);
    d3d8_state_report(&g_dev.state);
    d3d8_sb_report();
    d3d8_vs_report();
    d3d8_surface_report();
    d3d8_resource_report();
    d3d8_drawcall_report();
    gpu_draw_report();
    d3d8_object_report();
    gpu_device_report();
}
