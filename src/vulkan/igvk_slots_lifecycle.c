/*
 * igVkVisualContext -- the slots that bring the renderer up and take it down.
 *
 * Slot numbers and each body's `RET N` come from
 *
 *   python3 tools/device_slots.py scratch/recomp/libIGGfx.json \
 *       scratch/recomp/libIGGfx.vtab.json --class igDx8VisualContext --list
 *
 * and the argument counts below are that RET divided by four. They are read
 * out of the binary, never guessed: a wrong count drifts the guest stack and
 * the failure lands in an unrelated function.
 */
#include "igvk_context.h"
#include "igvk_device.h"
#include "guest_heap.h"
#include "win32_sdl.h"

#include <stdio.h>
#include <string.h>

/* Linked addresses of the engine bodies these slots replace. */
#define DX_USER_INSTANTIATE          0x1002c210u   /* slot   7, ret 4  */
#define DX_USER_RELEASE              0x1002c410u   /* slot   8, ret 0  */
#define DX_SET_NATIVE_WINDOW_HANDLE  0x1002c970u   /* slot  36, ret 4  */
#define DX_SET_NATIVE_DEVICE_HANDLE  0x1002c9d0u   /* slot  38, ret 4  */
#define BASE_USER_INSTANTIATE        0x100247f0u   /* igVisualContext::  */

/* igDxVisualContext::open's own helpers, in its order. See vk_open. */
#define DX_OPEN_HELPER_A             0x1002fa60u
#define DX_OPEN_HELPER_B             0x1002ca30u
#define G_CURRENT_VISUAL_CONTEXT     0x101895a8u

/*
 * What construction actually produced.
 *
 * This backend runs the engine's construction helpers by hand instead of
 * letting igDxVisualContext::userInstantiate run, because that body creates a
 * Direct3D device unguarded. The cost is that any field the real body would
 * have set, and this sequence does not, stays NULL -- and a NULL pool
 * dereferences hundreds of instructions later in a function whose name says
 * nothing about construction. releaseVolatileResources faulting on
 * this+0x330 is that failure, and it took a disassembly to attribute.
 *
 * So the fields the helpers are supposed to produce are printed once, with
 * what set them. A zero here is the diagnosis, printed at the moment it
 * becomes true rather than at the crash.
 */
static void report_fields(uint32_t self)
{
    static const struct { uint32_t off; const char *what; } f[] = {
        { 0x14u,  "igVisualContext list entry   (base userInstantiate)" },
        { 0x18u,  "driver-data memory pool      (getDriverDataMemoryPool)" },
        { 0x140u, "IDirect3D8                   (NULL BY DESIGN)" },
        { 0x144u, "IDirect3DDevice8             (NULL BY DESIGN)" },
        { 0x148u, "D3DCAPS8 block               (allocated, LEFT ZEROED)" },
        { 0x150u, "present parameters           (allocated here)" },
        { 0x154u, "present parameters 2         (allocated here)" },
        { 0x178u, "render destination pool      (initRenderDestinations)" },
        { 0x330u, "texture state block          (initTexture)" },
        { 0x35cu, "texture data list            (initTexture)" },
        { 0x534u, "capability manager           (constructor)" },
        { 0x53cu, "vertex/pixel shader manager  (constructor)" },
    };
    size_t i;
    int zero = 0;
    static int told;

    if (told++) return;
    printf("igVk: the constructed igVkVisualContext at 0x%08x --\n", self);
    for (i = 0; i < sizeof f / sizeof f[0]; i++) {
        uint32_t v = RD32(self + f[i].off);
        if (!v) zero++;
        printf("        +0x%03x  %08x  %s\n", f[i].off, v, f[i].what);
    }
    printf("      %d of %zu are zero. The two marked BY DESIGN are meant to "
           "be;\n      any other zero is a construction step that did not "
           "happen, and it will\n      fault somewhere that does not mention "
           "construction.\n", zero, sizeof f / sizeof f[0]);
}

/* ---- slot 7: userInstantiate ------------------------------------------ */

/*
 * igDxVisualContext's version calls the base, then
 * initDefaultDxDeviceParameters, then Direct3DCreate8 -> this+0x140, then
 * thirteen init* helpers.
 *
 * NOT a super-call, and this is the one place that is right: the body's whole
 * purpose is to create a Direct3D device, and it does so unguarded. So this
 * calls the same BASE -- igVisualContext's own construction still runs -- then
 * creates the host device, then runs the engine's init helpers itself.
 */
static void vk_user_instantiate(CPU *C)
{
    uint32_t self = IGVK_SELF(C);
    uint32_t arg = IGVK_ARG(C, 0);
    uint32_t basefn = ark_lifted(IGVK_GFX, BASE_USER_INSTANTIATE);
    uint32_t r = 0;

    if (basefn) r = ark_call_this(basefn, self, &arg, 1);

    igvk_device_create();

    /*
     * The parameter blocks at this+0x150, this+0x154 and this+0x148.
     *
     * The engine allocates all three itself, and the sizes are MEASURED from
     * its own body rather than bounded:
     *
     *     PUSH 0x34 ; CALL [0x100cf498] -> this+0x150   REP STOSD ECX=0xd
     *     PUSH 0x34 ; CALL [0x100cf498] -> this+0x154   REP STOSD ECX=0xd
     *     PUSH 0xd4 ; CALL [0x100cf498] -> this+0x148   REP STOSD ECX=0x35
     *
     * 0xd dwords is 0x34 and 0x35 dwords is 0xd4, so the zero-fills agree
     * with the allocations and there is no slack to guess at. this+0x148 is
     * the D3DCAPS8 the engine fills with IDirect3D8::GetDeviceCaps, which is
     * 0xd4 bytes on PC D3D8 -- an independent confirmation of both the size
     * and of C108's finding that this build uses the PC vtable layout.
     *
     * They are allocated here because the engine's own allocation is
     * interleaved with the Direct3DCreate8 call this backend does not make.
     * initDefaultDxDeviceParameters writes through this+0x154 immediately
     * (`MOV EDX,[ECX+0x154]; MOV [EDX+0x10],EAX`) and faults at 0x10 without
     * them.
     *
     * NOTE that this+0x148 is left ZEROED. The engine would have filled it
     * from GetDeviceCaps, so every capability the game asks about currently
     * reads "not supported". That is a real gap, not a neutral default, and
     * it is reported below.
     */
    {
        static const struct { uint32_t off, size; } blk[] = {
            { 0x150u, 0x34u }, { 0x154u, 0x34u }, { 0x148u, 0xd4u },
        };
        size_t b;
        for (b = 0; b < sizeof blk / sizeof blk[0]; b++) {
            if (!RD32(self + blk[b].off)) {
                uint32_t m = guest_malloc(blk[b].size);
                if (!m) {
                    fprintf(stderr, "igVk: no guest memory for the parameter "
                                    "block at +0x%x\n", blk[b].off);
                    ark_ret(C, r, 1);
                    return;
                }
                memset((void *)(uintptr_t)m, 0, blk[b].size);
                WR32(self + blk[b].off, m);
            }
        }
    }
    /*
     * Run the engine's own init helpers, in the engine's order.
     *
     * Eleven of the thirteen touch no device at all -- they allocate the
     * render-destination pool, the render lists, the matrix and material
     * state, the texture-stage tables. Skipping them is what made the first
     * version of this fault: createRenderDestination is inherited, reads
     * this+0x178, and that pool is allocated by initRenderDestinations.
     *
     * The two that DO touch the device -- initDesktopDisplayFormat and initCg,
     * the latter being the NVIDIA Cg shader path (C112) -- are deliberately
     * not called, and are part of the work still owed.
     */
    {
        /*
         * Transcribed from igDxVisualContext::userInstantiate 0x1002c210, in
         * its order, INCLUDING the two calls to 0x10094490 that the engine
         * makes twice in a row and the trailing 0x1002d230. An earlier
         * version of this list was assembled from the named init* helpers
         * only and silently dropped those three.
         */
        static const struct { uint32_t va; const char *name; } init[] = {
            { 0x1002c3a0u, "initDefaultDxDeviceParameters" },
            { 0x1002a760u, "initRenderDestinations" },
            { 0x10041480u, "initTexture" },
            { 0x10044b30u, "initTextureStages" },
            { 0x1003cd60u, "initLighting" },
            { 0x1003dc40u, "initMaterial" },
            { 0x1003e700u, "initMatrices" },
            { 0x10094490u, "0x10094490 (unnamed, called twice)" },
            { 0x10094490u, "0x10094490 (unnamed, second call)" },
            { 0x1002dea0u, "initRenderLists" },
            { 0x10034590u, "initGeometry" },
            { 0x10049000u, "initVertexShader" },
            { 0x1003fc30u, "initPixelShader" },
            { 0x1002d230u, "0x1002d230 (unnamed, last)" },
        };
        size_t n;
        static int told;
        for (n = 0; n < sizeof init / sizeof init[0]; n++) {
            uint32_t f = ark_lifted(IGVK_GFX, init[n].va);
            if (f) ark_call_this(f, self, NULL, 0);
            else fprintf(stderr, "igVk: cannot map %s\n", init[n].name);
        }
        if (!told++)
            printf("igVk: ran %zu of the engine's construction helpers; "
                   "initDesktopDisplayFormat, initCg and the [this+0x534] and "
                   "[this+0x53c] virtual calls are still owed\n",
                   sizeof init / sizeof init[0]);
        report_fields(self);
        fflush(stdout);
    }
    /* +0x140/+0x144 are where igDxVisualContext keeps its IDirect3D8 and its
       device. Left 0: this host has no such objects, and the device-touching
       slots are overridden precisely so that nothing dereferences them. Every
       engine body super-called from here checks them for NULL first. */
    ark_ret(C, r, 1);
}

/* ---- slot 8: userRelease ---------------------------------------------- */

/*
 * The engine's body releases the render destinations, frees the parameter
 * blocks at this+0x148/0x150/0x154, runs the uninit* counterparts, and then
 * releases this+0x144 and this+0x140 -- both guarded with `CMP EAX,0; JZ`, so
 * a NULL device is a case it already handles. Super-called for exactly that
 * reason: it is the counterpart of eleven helpers we did not write and must
 * not re-derive.
 *
 * Ordering: the host device goes first. The engine's body does not know about
 * it, and tearing the guest side down while a frame is still open would leave
 * a command buffer referencing a swapchain nobody owns.
 */
static void vk_user_release(CPU *C)
{
    igvk_device_report();
    igvk_device_destroy();
    ark_ret(C, igvk_super(C, DX_USER_RELEASE, 0), 0);
}

/* ---- slot 34: getLastError -------------------------------------------- */

/*
 * The engine's body answers three things: no device -> error; device lost ->
 * try to reset it; otherwise fine. Its first test is literally
 * `MOV EAX,[ESI+0x144]; TEST EAX,EAX; JZ -> return 1`.
 *
 * For this backend that whole question reduces to "is there a GPU device",
 * because SDL_GPU has no lost-device state to recover from -- a swapchain
 * that cannot be acquired is a per-frame condition handled in
 * igvk_frame_begin, not a persistent error. So this is implemented directly
 * rather than super-called: super-calling would report a permanent error,
 * since this+0x144 is NULL by design and always will be.
 *
 * Returned in EAX as the engine does: 0 means usable.
 */
static void vk_get_last_error(CPU *C)
{
    ark_ret(C, igvk_device_ready() ? 0u : 1u, 0);
}

/* ---- slot 36: setNativeWindowHandle ----------------------------------- */

/*
 * The engine stores the HWND at this+0x170 and, if a device exists, pushes it
 * into the presentation parameters and resets. With this+0x144 NULL its body
 * takes the branch that only stores -- which is the correct bookkeeping and
 * is why this super-calls rather than storing the field itself.
 *
 * The HWND is then meaningless to us: win32_sdl.c backs the guest's single
 * HWND with one SDL_Window, and that window is what the swapchain needs. So
 * the handle's ARRIVAL is the signal, not its value.
 */
static void vk_set_native_window_handle(CPU *C)
{
    uint32_t r = igvk_super(C, DX_SET_NATIVE_WINDOW_HANDLE, 1);
    static int told;
    if (!told++)
        printf("igVk: setNativeWindowHandle(0x%08x) -- the swapchain follows "
               "the host's SDL window, not this handle\n", IGVK_ARG(C, 0));
    igvk_device_attach_window(win32_sdl_window());
    ark_ret(C, r, 1);
}

/* ---- slot 38: setNativeDeviceHandle ----------------------------------- */

/*
 * The counterpart of slot 36, and eight instructions long:
 *
 *     if (this->device == NULL || this->f174 != h) this->f174 = h;
 *
 * It guards on this+0x144 exactly as setNativeWindowHandle does, so with the
 * device left NULL by design its body takes the plain store and is safe to
 * super-call. Nothing on this host has a "native device handle" to speak of --
 * the Vulkan device is ours and the engine never sees it -- so the store is
 * the whole of the correct behaviour.
 */
static void vk_set_native_device_handle(CPU *C)
{
    ark_ret(C, igvk_super(C, DX_SET_NATIVE_DEVICE_HANDLE, 1), 1);
}

/* ---- slot 30: open ----------------------------------------------------- */

/*
 * open(igStatus *out), RET 4, hidden-pointer status return.
 *
 * The engine's body (libIGGfx 0x1002c5c0) is:
 *
 *     if (this->device) return OK;              // already open
 *     this->vtbl[29]();                         // slot 0x74
 *     if (!createDevice(this->f17c)) return FAIL;
 *     helperA(); helperB();                     // 0x1002fa60, 0x1002ca30
 *     this->vtbl[217](-1);                      // slot 0x364
 *     g_currentVisualContext = this;            // 0x101895a8
 *     return OK;
 *
 * NOT super-called, and the reason is one call: igDxVisualContext::createDevice
 * (0x1002cb80) opens with `if (this->f140 == 0) return false`, and this+0x140
 * is the IDirect3D8 this backend deliberately never creates. Super-calling
 * would therefore always return FAIL -- which is exactly what the engine was
 * concluding before this existed.
 *
 * So the body is transcribed with that ONE call replaced by "the host device
 * already exists". Everything else is the engine's own, called through the
 * object's own vtable so that an override of slot 29 or 217, if one is ever
 * added, is honoured -- slots 29 and 217 are both inherited today (neither is
 * among the 98), so this runs igDx8VisualContext's code for them.
 */
static void vk_open(CPU *C)
{
    uint32_t self = IGVK_SELF(C);
    uint32_t out = IGVK_ARG(C, 0);
    uint32_t vt = RD32(self);
    uint32_t minus1 = 0xFFFFFFFFu;
    static int told;

    if (!igvk_device_ready()) {
        /* The one honest failure: no GPU device means the display genuinely
           did not come up, and saying OK would move the symptom elsewhere. */
        fprintf(stderr, "igVk: open() refused -- there is no GPU device.\n");
        igvk_ret_status(C, out, igvk_status_fail(), 1);
        return;
    }
    ark_call_this(RD32(vt + 29u * 4u), self, NULL, 0);
    {
        uint32_t f;
        if ((f = ark_lifted(IGVK_GFX, DX_OPEN_HELPER_A)))
            ark_call_this(f, self, NULL, 0);
        if ((f = ark_lifted(IGVK_GFX, DX_OPEN_HELPER_B)))
            ark_call_this(f, self, NULL, 0);
    }
    ark_call_this(RD32(vt + 217u * 4u), self, &minus1, 1);
    {
        uint32_t g = ark_lifted(IGVK_GFX, G_CURRENT_VISUAL_CONTEXT);
        if (g) WR32(g, self);
    }
    if (!told++)
        printf("igVk: open() -- the host device already exists, so the "
               "engine's createDevice is the one call skipped; the rest of "
               "its body ran.\n");
    fflush(stdout);
    igvk_ret_status(C, out, igvk_status_ok(), 1);
}

/* ---- slot 25: detectDriverDatabaseProperties --------------------------- */

/*
 * detectDriverDatabaseProperties(arg), RET 4.
 *
 * The engine's 128-instruction body reads the Direct3D ADAPTER IDENTIFIER --
 * vendor, device and driver version -- and looks the result up in a table of
 * known-bad DirectX drivers so it can disable features that misbehave on
 * them. Every input it needs comes from an IDirect3D8 this backend does not
 * have, which is why it is one of the 98.
 *
 * There is no DirectX driver here to have quirks, so the truthful answer is
 * that no entry applies and the engine's defaults stand. That is a decision,
 * not a stub, and it has a real consequence worth stating: any workaround the
 * database would have switched ON for a given card will NOT be applied. On
 * this backend that is correct -- the workarounds are for D3D drivers -- but
 * if a Vulkan driver ever needs the same shape of quirk list, it belongs here
 * and not somewhere else.
 *
 * Said once rather than silently, so a later "why is this feature enabled on
 * hardware that cannot do it" question has somewhere to land.
 */
static void vk_detect_driver_database_properties(CPU *C)
{
    static int told;
    if (!told++)
        printf("igVk: detectDriverDatabaseProperties -- no DirectX adapter to "
               "identify, so no driver-quirk entry applies and the engine's "
               "defaults stand.\n");
    fflush(stdout);
    ark_ret(C, 0, 1);
}

/* ---- slot 254: setVideoMode ------------------------------------------- */

/*
 * setVideoMode(igStatus *out, const VideoMode *desc), MSVC hidden-pointer
 * struct return, RET 8.
 *
 * The engine's body caches the mode byte at this+0x180, derives two flags
 * from desc+0x14, and returns the OK status singleton -- then reconfigures
 * the D3D device unguarded. So this records the request and accepts it.
 *
 * Accepting is the truthful answer for a backend whose swapchain follows the
 * host window's size: there is no mode to fail to set. What is NOT yet done
 * is resizing that window to the requested mode, which is why the request is
 * printed rather than silently swallowed.
 */
static uint32_t g_video_mode, g_video_flags;

static void vk_set_video_mode(CPU *C)
{
    uint32_t out = IGVK_ARG(C, 0);
    uint32_t desc = IGVK_ARG(C, 1);
    static int told;

    g_video_mode = desc ? RD8(desc) : 0;
    g_video_flags = desc ? RD32(desc + 0x14u) : 0;
    if (!told++)
        printf("igVk: setVideoMode(mode=%u, flags=0x%x) accepted; the "
               "swapchain follows the host window and is not resized to "
               "match.\n", g_video_mode, g_video_flags);
    fflush(stdout);   /* the run may abort in a later slot before a flush */
    igvk_ret_status(C, out, igvk_status_ok(), 2);
}

/* ---- installation ------------------------------------------------------ */

void igvk_install_lifecycle(void)
{
    igvk_slot(7,   vk_user_instantiate,         "userInstantiate");
    igvk_slot(8,   vk_user_release,             "userRelease");
    igvk_slot(25,  vk_detect_driver_database_properties,
                                                "detectDriverDatabaseProperties");
    igvk_slot(30,  vk_open,                     "open");
    igvk_slot(34,  vk_get_last_error,           "getLastError");
    igvk_slot(36,  vk_set_native_window_handle, "setNativeWindowHandle");
    igvk_slot(38,  vk_set_native_device_handle, "setNativeDeviceHandle");
    igvk_slot(254, vk_set_video_mode,           "setVideoMode");
}
