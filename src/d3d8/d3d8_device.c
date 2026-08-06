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
#include "d3d8_types.h"

#include "gpu_device.h"
#include "win32_sdl.h"

#include "x86rt.h"

#include <stdio.h>
#include <string.h>

typedef struct {
    long                  refs;
    uint32_t              adapter, devtype, focus_window, behaviour;
    D3DPRESENT_PARAMETERS pp;
    D3D8CapsLimits        limits;
    D3D8State             state;

    unsigned long         scenes;         /* BeginScene calls */
    unsigned long         presents;
    unsigned long         clears;
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
    (void)self;
    d3d8_ret(C, (uint32_t)++g_dev.refs);
}

static void dev_Release(D3D8Object *self, CPU *C)
{
    long n = --g_dev.refs;
    (void)self;
    if (n <= 0) {
        printf("d3d8: the engine released the device; tearing down the GPU "
               "device with it.\n");
        gpu_device_destroy();
        n = 0;
    }
    d3d8_ret(C, (uint32_t)n);
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
    NULL,                               /* 16 GetBackBuffer */
    NULL,                               /* 17 GetRasterStatus */
    NULL,                               /* 18 SetGammaRamp */
    NULL,                               /* 19 GetGammaRamp */
    NULL,                               /* 20 CreateTexture */
    NULL,                               /* 21 CreateVolumeTexture */
    NULL,                               /* 22 CreateCubeTexture */
    NULL,                               /* 23 CreateVertexBuffer */
    NULL,                               /* 24 CreateIndexBuffer */
    NULL,                               /* 25 CreateRenderTarget */
    NULL,                               /* 26 CreateDepthStencilSurface */
    NULL,                               /* 27 CreateImageSurface */
    NULL,                               /* 28 CopyRects */
    NULL,                               /* 29 UpdateTexture */
    NULL,                               /* 30 GetFrontBuffer */
    NULL,                               /* 31 SetRenderTarget */
    NULL,                               /* 32 GetRenderTarget */
    NULL,                               /* 33 GetDepthStencilSurface */
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
    NULL,                               /* 61 SetTexture */
    dev_GetTextureStageState,           /* 62 */
    dev_SetTextureStageState,           /* 63 */
    dev_ValidateDevice,                 /* 64 */
    NULL,                               /* 65 GetInfo */
    NULL,                               /* 66 SetPaletteEntries */
    NULL,                               /* 67 GetPaletteEntries */
    NULL,                               /* 68 SetCurrentTexturePalette */
    NULL,                               /* 69 GetCurrentTexturePalette */
    NULL,                               /* 70 DrawPrimitive */
    NULL,                               /* 71 DrawIndexedPrimitive */
    NULL,                               /* 72 DrawPrimitiveUP */
    NULL,                               /* 73 DrawIndexedPrimitiveUP */
    NULL,                               /* 74 ProcessVertices */
    NULL,                               /* 75 CreateVertexShader */
    NULL,                               /* 76 SetVertexShader */
    NULL,                               /* 77 GetVertexShader */
    NULL,                               /* 78 DeleteVertexShader */
    NULL,                               /* 79 SetVertexShaderConstant */
    NULL,                               /* 80 GetVertexShaderConstant */
    NULL,                               /* 81 GetVertexShaderDeclaration */
    NULL,                               /* 82 GetVertexShaderFunction */
    NULL,                               /* 83 SetStreamSource */
    NULL,                               /* 84 GetStreamSource */
    NULL,                               /* 85 SetIndices */
    NULL,                               /* 86 GetIndices */
    NULL,                               /* 87 CreatePixelShader */
    NULL,                               /* 88 SetPixelShader */
    NULL,                               /* 89 GetPixelShader */
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
    g_dev.refs = 1;
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
    if (!gpu_device_attach_window(win32_sdl_window()))
        fprintf(stderr, "d3d8: no host window to present into yet; the "
                        "swapchain will be claimed when one exists.\n");

    g_dev_obj = d3d8_object_new(D3D8_IF_IDirect3DDevice8, &g_dev);
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
    printf("  d3d8: %lu scene(s) begun, %lu clear(s), %lu present(s)\n",
           g_dev.scenes, g_dev.clears, g_dev.presents);
    d3d8_state_report(&g_dev.state);
    gpu_device_report();
}
