/*
 * igVkVisualContext -- the host renderer, substituted for the engine's DirectX
 * one through ARK.
 *
 * The engine is already a multi-platform renderer abstraction (C113):
 * igVisualContext is abstract and records its platform implementation in
 * _Meta+0x3c, which igMetaObject::createInstance follows. On this build that
 * points at igDx8VisualContext, and igDxVisualContext -- 291 of 334 slots
 * different from the abstract base -- is what actually calls Direct3DCreate8.
 * Repointing _Meta+0x3c at a class of ours means the D3D8 path is never
 * entered, rather than reimplemented.
 *
 * WHAT THIS IS TODAY, so nobody reads more into it than is there: the class
 * registers, binds, and is constructed by the engine. It implements igObject's
 * 21 inherited virtuals (C116) and nothing else. The remaining ~313 slots
 * report their INDEX and stop. That is deliberate and is the point of this
 * step -- the engine now tells us, in the order it needs them, exactly which
 * of the 209 pure-virtual methods (C114) matter, instead of us guessing from a
 * vtable dump. Nothing is drawn yet.
 *
 * Addresses below are LINKED addresses recovered by tools/ark_classes.py from
 * libIGGfx's own igArkRegister calls. They are not guesses, and ark_lifted()
 * maps them through the module's real load base.
 */
#include "igvk_ark.h"
#include "igvk_device_slots.h"
#include "guest_heap.h"

#include "x86rt.h"
#include "x86rt_native.h"

#include <stdio.h>
#include <string.h>

#ifdef X2_WITH_SDL
#include <SDL3/SDL.h>
#endif

#define GFX "libIGGfx.dll"

/* From `tools/ark_classes.py scratch/recomp/libIGGfx.json ...`:
     igVisualContext   abstract, size 0x140, meta slot 0x10188ba0,
                       arkRegisterInternal 0x1000b440, getClassMeta 0x1004afc0
   The last two are exactly the pair igDxVisualContext passes as ITS parent
   hooks, which is the cross-check that they are igVisualContext's and not
   something else's. */
#define IGVISUALCONTEXT_META_SLOT      0x10188ba0u
#define IGVISUALCONTEXT_REGINTERNAL    0x1000b440u
#define IGVISUALCONTEXT_GETCLASSMETA   0x1004afc0u

/* igDxVisualContext and igDx8VisualContext are both 0x558, and the Dx8 leaf
   adds no fields (C113). Matching it means the engine gets an object the same
   size as the one it used to get -- anything that reads a igDxVisualContext
   field offset finds allocated memory rather than heap past the end. */
#define IGVK_INSTANCE_SIZE  0x558u

/* igVisualContext's vtable is 334 slots (C114). */
#define IGVK_SLOTS  334

/*
 * igVisualContext's own vtable, and the _purecall stub that marks a slot as
 * something a platform backend owes.
 *
 * C114: 334 slots, 209 of them _purecall, 125 real. Those 125 are ordinary
 * platform-neutral engine code, and the cheapest correct way to inherit them is
 * to point our vtable straight at them -- which is exactly what a C++ subclass
 * of igVisualContext would contain. libIGCore stamps whatever vtable pointer we
 * hand it (C009), so we are free to build ours out of the engine's own
 * functions.
 *
 * This replaces an earlier hand-rolled map of igObject's 21 slots. That map was
 * both too small and wrong here: it stubbed slot 7, which igVisualContext
 * implements as userInstantiate and igDxVisualContext overrides to call
 * Direct3DCreate8, so a "do nothing" there skipped renderer creation while
 * looking like it worked.
 */
#define IGVISUALCONTEXT_VTABLE  0x100da630u
#define IGDX8VISUALCONTEXT_VTABLE 0x100dd0a0u
#define IGGFX_PURECALL          0x100ce258u

/*
 * The engine's igStatus singletons.
 *
 * libIGGfx returns status by MSVC's hidden-pointer convention: the caller
 * passes an out-slot as the first stack argument, the callee stores a status
 * pointer into it and returns that same out-slot in EAX. The two singletons
 * live behind these globals -- `*(*(0x100cf4d4))` is the OK one and
 * `*(*(0x100cf4d0))` the failure one, which is how igDxVisualContext's own
 * setVideoMode and the Cg loader both spell success and failure.
 *
 * Reading them from the engine rather than inventing a value matters: the
 * caller compares the returned pointer against its own copy of the same
 * singleton, so a fabricated non-NULL would read as an unrecognised failure.
 */
#define IGGFX_STATUS_OK_PP      0x100cf4d4u
#define IGGFX_STATUS_FAIL_PP    0x100cf4d0u

static uint32_t status_value(uint32_t pp_linked)
{
    uint32_t pp = ark_lifted(GFX, pp_linked);
    uint32_t p = pp ? RD32(pp) : 0;
    return p ? RD32(p) : 0;
}

/*
 * Return an igStatus by the hidden-pointer convention.
 * `stack_args` counts the out-slot too, so RET 0x8 is stack_args = 2.
 */
static void ret_status(CPU *C, uint32_t out, uint32_t status, int stack_args)
{
    if (out) WR32(out, status);
    ark_ret(C, out, stack_args);
}

static ArkClass g_vk;   /* tentative; defined with its initialiser below */

static void vk_get_class_meta(CPU *C)
{
    ark_ret(C, RD32(g_vk.meta_slot), 0);
}

/*
 * Slot 254 -- setVideoMode(igStatus *out, const VideoMode *desc).
 *
 * igDxVisualContext's version (libIGGfx 0x1002f040, RET 0x8) caches the mode
 * byte at this+0x180, derives two flags from desc+0x14, and returns the OK
 * status singleton. Nothing here creates a device yet, so this records the
 * request and accepts it -- accepting is the truthful answer for a renderer
 * that has not yet been asked to present anything, and refusing would stop the
 * engine before it reveals the rest of the interface.
 */
static uint32_t g_video_mode, g_video_flags;

/* The real GPU device. NULL until slot 7 runs. */
#ifdef X2_WITH_SDL
static SDL_GPUDevice *g_gpu;
static SDL_Window *g_window;
#endif

/*
 * Slot 7 -- userInstantiate(arg). __thiscall, RET 0x4.
 *
 * igDxVisualContext's version (0x1002c210) calls the base, then
 * initDefaultDxDeviceParameters, then Direct3DCreate8 -> this+0x140, then
 * twelve init* helpers (render destinations, textures, texture stages,
 * lighting, material, matrices, render lists, geometry, vertex shader, pixel
 * shader, desktop display format).
 *
 * This calls the SAME base -- so igVisualContext's own construction still runs
 * -- and then creates an SDL3 GPU device, which is Vulkan on Linux. It does NOT
 * yet run the init* helpers: each of those is one of the 98 device-touching
 * slots and needs its DirectX calls replaced, which is the work that follows.
 *
 * The device is created for real rather than stubbed, so that a failure to get
 * a Vulkan device is reported HERE, by name, rather than surfacing later as an
 * unexplained blank frame.
 */
static void vk_user_instantiate(CPU *C)
{
    uint32_t self = C->ecx;
    uint32_t arg = RD32(C->esp + 4u);
    uint32_t basefn = ark_lifted(GFX, 0x100247f0u);   /* igVisualContext:: */
    uint32_t a[1], r = 0;

    a[0] = arg;
    if (basefn) r = ark_call_this(basefn, self, a, 1);

#ifdef X2_WITH_SDL
    if (!g_gpu) {
        if (!SDL_WasInit(SDL_INIT_VIDEO) && !SDL_Init(SDL_INIT_VIDEO))
            fprintf(stderr, "igVk: SDL_Init(VIDEO) failed: %s\n",
                    SDL_GetError());
        g_gpu = SDL_CreateGPUDevice(SDL_GPU_SHADERFORMAT_SPIRV, true, NULL);
        if (!g_gpu) {
            fprintf(stderr,
                    "igVk: SDL_CreateGPUDevice(SPIRV) FAILED: %s\n"
                    "  No GPU device means nothing can be drawn. Reported here "
                    "rather than later as a blank frame.\n", SDL_GetError());
        } else {
            printf("igVk: GPU device created -- backend \"%s\"\n",
                   SDL_GetGPUDeviceDriver(g_gpu));
        }
        fflush(stdout);
    }
#else
    fprintf(stderr, "igVk: built without SDL; no GPU device can be made.\n");
#endif

    /*
     * Run the engine's own init helpers.
     *
     * igDxVisualContext::userInstantiate calls thirteen of these, and eleven of
     * them touch no device at all -- they allocate the render-destination pool,
     * the render lists, the matrix and material state, the texture-stage
     * tables. Skipping them is what made the first version of this fault:
     * createRenderDestination is inherited, reads this+0x178, and that pool is
     * allocated by initRenderDestinations. A SIGSEGV at 0x4 was the result.
     *
     * The two that DO touch the device -- initDesktopDisplayFormat and initCg,
     * the latter being the NVIDIA Cg shader path (C112) -- are deliberately not
     * called, and are part of the 98 still owed.
     *
     * Called __thiscall with no stack arguments, in the engine's own order.
     */
    /*
     * The parameter blocks at this+0x148/0x150/0x154.
     *
     * igDxVisualContext::userInstantiate allocates these before calling
     * Direct3DCreate8 -- one is zeroed with `MOV ECX,0x35; REP STOSD`, i.e.
     * 0xd4 bytes -- and initDefaultDxDeviceParameters immediately writes
     * through this+0x154 (`MOV EDX,[ECX+0x154]; MOV [EDX+0x10],EAX`). Without
     * them that helper faults at 0x10, which is exactly what happened.
     *
     * Sized GENEROUSLY at 0x200 rather than exactly: the true extents have not
     * been reverse-engineered field by field, and over-allocating zeroed guest
     * memory cannot corrupt anything, whereas guessing too small would produce
     * heap damage that surfaces far away. Marked so the next pass knows this is
     * a bound, not a measurement.
     */
    {
        static const uint32_t blk[] = { 0x148u, 0x150u, 0x154u };
        size_t b;
        for (b = 0; b < sizeof blk / sizeof blk[0]; b++) {
            if (!RD32(self + blk[b])) {
                uint32_t m = guest_malloc(0x200u);
                if (!m) { fprintf(stderr, "igVk: no guest memory for the "
                                          "parameter block at +0x%x\n", blk[b]);
                          ark_ret(C, r, 1); return; }
                memset((void *)(uintptr_t)m, 0, 0x200u);
                WR32(self + blk[b], m);
            }
        }
    }
    {
        static const struct { uint32_t va; const char *name; } init[] = {
            { 0x1002c3a0u, "initDefaultDxDeviceParameters" },
            { 0x1002a760u, "initRenderDestinations" },
            { 0x10041480u, "initTexture" },
            { 0x10044b30u, "initTextureStages" },
            { 0x1003cd60u, "initLighting" },
            { 0x1003dc40u, "initMaterial" },
            { 0x1003e700u, "initMatrices" },
            { 0x1002dea0u, "initRenderLists" },
            { 0x10034590u, "initGeometry" },
            { 0x10049000u, "initVertexShader" },
            { 0x1003fc30u, "initPixelShader" },
        };
        size_t n;
        static int told;
        for (n = 0; n < sizeof init / sizeof init[0]; n++) {
            uint32_t f = ark_lifted(GFX, init[n].va);
            if (f) ark_call_this(f, self, NULL, 0);
            else fprintf(stderr, "igVk: cannot map %s\n", init[n].name);
        }
        if (!told++)
            printf("igVk: ran %zu device-free init helpers; "
                   "initDesktopDisplayFormat and initCg still owed\n",
                   sizeof init / sizeof init[0]);
        fflush(stdout);
    }
    /* +0x140 is where igDxVisualContext keeps its IDirect3D8. Left 0: this
       host has no such object, and the device-touching slots are overridden
       precisely so that nothing dereferences it. */
    ark_ret(C, r, 1);
}

static void vk_set_video_mode(CPU *C)
{
    uint32_t out = RD32(C->esp + 4u);
    uint32_t desc = RD32(C->esp + 8u);
    static int told;
    g_video_mode = desc ? RD8(desc) : 0;
    g_video_flags = desc ? RD32(desc + 0x14u) : 0;
    if (!told++)
        printf("igVk: setVideoMode(mode=%u, flags=0x%x) accepted; no device is "
               "created yet.\n", g_video_mode, g_video_flags);
    fflush(stdout);   /* the run may abort in a later slot before a flush */
    ret_status(C, out, status_value(IGGFX_STATUS_OK_PP), 2);
}

static ArkClass g_vk = {
    .name = "igVkVisualContext",
    .instance_size = IGVK_INSTANCE_SIZE,
    .is_abstract = 0,
    .base_module = GFX,
    .base_register_internal_va = IGVISUALCONTEXT_REGINTERNAL,
    .base_get_class_meta_va = IGVISUALCONTEXT_GETCLASSMETA,
    .nslots = IGVK_SLOTS
};

static int g_done;

int igvk_visualcontext_install(void)
{
    uint32_t meta_slot, meta;
    int k;

    if (g_done) return 1;

    meta_slot = ark_lifted(GFX, IGVISUALCONTEXT_META_SLOT);
    if (!meta_slot) {
        fprintf(stderr, "igVk: %s is not loaded; nothing was substituted.\n",
                GFX);
        return 1;                        /* waiting cannot fix this */
    }
    /*
     * Readiness is igDx8VisualContext existing, not igVisualContext.
     *
     * The abstract root registers well before its DirectX subclasses, and
     * binding at that moment left igDxVisualContext and igDx8VisualContext
     * unregistered and therefore un-rebound -- so anything instantiating those
     * directly still built a DirectX context and still called Direct3DCreate8.
     * Waiting for the CONCRETE class we are replacing means the whole chain
     * exists by the time we rebind it.
     */
    {
        uint32_t dx8 = ark_lifted(GFX, 0x10189450u);
        if (!dx8 || !RD32(dx8)) return 0;
    }
    meta = RD32(meta_slot);
    if (!meta) return 0;

    g_done = 1;
    printf("\n=== igVkVisualContext: substituting the renderer ===\n");
    printf("  igVisualContext meta            0x%08x\n", meta);
    fflush(stdout);

    /*
     * Seed from igDx8VisualContext, not from the abstract base.
     *
     * C119: of igDx8VisualContext's 334 slots only 98 reach the DirectX device
     * fields. The other 236 are platform-neutral engine bookkeeping -- render
     * destination pools, format tables, capability queries over the class's own
     * fields -- and inheriting them verbatim is both correct and free.
     * Seeding from the abstract igVisualContext instead left 209 slots owed,
     * and the first two the engine asked for (setVideoMode, then
     * createRenderDestination) turned out to be exactly that kind of
     * bookkeeping: createRenderDestination is 109 instructions of free-list
     * management that touches no device at all.
     *
     * The 98 are overridden. Left inherited, they would call through a DirectX
     * device this host never created.
     */
    {
        uint32_t src = ark_lifted(GFX, IGDX8VISUALCONTEXT_VTABLE);
        uint32_t pure = ark_lifted(GFX, IGGFX_PURECALL);
        int inherited = 0, still_pure = 0, n;
        if (!src || !pure) {
            fprintf(stderr, "igVk: cannot map igDx8VisualContext's vtable\n");
            return 1;
        }
        g_vk.vtable = igvk_vtable_new(g_vk.name, IGVK_SLOTS);
        for (k = 0; k < IGVK_SLOTS; k++) {
            uint32_t e = RD32(src + (uint32_t)k * 4u);
            if (e == pure) { still_pure++; continue; }
            WR32(g_vk.vtable + (uint32_t)k * 4u, e);
            inherited++;
        }
        /* Now punch out every device-touching slot so it reports instead of
           dispatching into DirectX code. */
        for (n = 0; n < IGVK_DEVICE_SLOT_COUNT; n++)
            WR32(g_vk.vtable + (uint32_t)IGVK_DEVICE_SLOTS[n] * 4u, 0);
        printf("  vtable seeded from igDx8VisualContext 0x%08x: %d inherited, "
               "%d device-touching slots removed, %d still pure\n",
               src, inherited, IGVK_DEVICE_SLOT_COUNT, still_pure);
    }
    /* Ours regardless of what the base had: the meta must be OUR class's. */
    igvk_vtable_set(g_vk.vtable, 20, vk_get_class_meta, g_vk.name,
                    "getClassMeta", &g_vk);
    igvk_vtable_set(g_vk.vtable, 254, vk_set_video_mode, g_vk.name,
                    "setVideoMode", &g_vk);
    igvk_vtable_set(g_vk.vtable, 7, vk_user_instantiate, g_vk.name,
                    "userInstantiate", &g_vk);
    igvk_vtable_fill_unimplemented(g_vk.vtable, g_vk.name, IGVK_SLOTS);

    if (!ark_register_class(&g_vk)) {
        fprintf(stderr, "igVk: registration failed; NOTHING was substituted.\n");
        return 1;
    }
    /*
     * Rebind the WHOLE chain, not just the abstract root.
     *
     * createInstance follows _Meta+0x3c from whichever meta it was handed, so
     * binding igVisualContext alone only catches code that instantiates the
     * abstract class. Anything calling igDxVisualContext::_instantiateFromPool
     * uses THAT meta, whose +0x3c still points at igDx8VisualContext -- which
     * is exactly what happened: the binding installed, and the engine built a
     * DirectX context anyway and called Direct3DCreate8.
     */
    {
        static const struct { const char *name; uint32_t slot; } chain[] = {
            { "igVisualContext",    IGVISUALCONTEXT_META_SLOT },
            { "igDxVisualContext",  0x10189048u },
            { "igDx8VisualContext", 0x10189450u },
        };
        size_t n;
        int bound = 0;
        for (n = 0; n < sizeof chain / sizeof chain[0]; n++) {
            uint32_t ms = ark_lifted(GFX, chain[n].slot);
            if (!ms || !RD32(ms)) {
                printf("  %-20s not registered yet -- NOT rebound\n",
                       chain[n].name);
                continue;
            }
            if (ark_bind_implementation(ms, &g_vk)) {
                printf("  %-20s -> igVkVisualContext\n", chain[n].name);
                bound++;
            }
        }
        if (!bound) {
            fprintf(stderr, "igVk: nothing was rebound; the engine will build "
                            "its DirectX context as before.\n");
            return 1;
        }
    }
    printf("  igVisualContext now resolves to igVkVisualContext.\n"
           "  From here the engine dispatches into this host, and every slot it "
           "needs that is\n"
           "  unimplemented reports its INDEX -- that list IS the renderer's "
           "work queue.\n\n");
    fflush(stdout);
    return 0;
}

/*
 * Arming.
 *
 * Same constraint as the ARK probe: registration needs the engine's memory
 * pools, which do not exist until the exe has started, so this cannot run at
 * module-init time. It is armed on the engine's own first
 * igMetaObject::createInstance -- a moment defined by the engine being ready
 * rather than by an ordering we assumed -- and x86_at_first_call reports any
 * armed trigger that never fired, so a run in which nothing was substituted
 * cannot be mistaken for one in which it was.
 */
/* Returns non-zero to disarm: either installed, or failed for a reason waiting
   longer cannot fix. A NULL igVisualContext meta means libIGGfx has simply not
   registered it yet, so that one retries. */
static int install_trampoline(void) { return igvk_visualcontext_install(); }

int igvk_visualcontext_arm(void)
{
    uint32_t ci = ark_export("libIGCore.dll",
        "?createInstance@igMetaObject@Core@Gap@@QBEPAVigObject@23@"
        "PAVigMemoryPool@23@@Z");
    if (!ci) {
        fprintf(stderr, "igVk: libIGCore does not export createInstance; "
                        "cannot arm the substitution.\n");
        return 1;
    }
    x86_at_first_call(ci, install_trampoline,
                      "the engine's first createInstance -- when pools and ARK "
                      "are up, which is when igVisualContext may be rebound");
    printf("igVk: substitution armed on igMetaObject::createInstance 0x%08x\n",
           ci);
    return 0;
}
