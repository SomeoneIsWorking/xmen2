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

#include "x86rt.h"
#include "x86rt_native.h"

#include <stdio.h>

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
#define IGGFX_PURECALL          0x100ce258u

static ArkClass g_vk;   /* tentative; defined with its initialiser below */

static void vk_get_class_meta(CPU *C)
{
    ark_ret(C, RD32(g_vk.meta_slot), 0);
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

    /* Seed from igVisualContext's own vtable: every non-pure slot becomes a
       direct pointer to the engine's implementation. */
    {
        uint32_t src = ark_lifted(GFX, IGVISUALCONTEXT_VTABLE);
        uint32_t pure = ark_lifted(GFX, IGGFX_PURECALL);
        int inherited = 0, owed = 0;
        if (!src || !pure) {
            fprintf(stderr, "igVk: cannot map igVisualContext's vtable\n");
            return 1;
        }
        g_vk.vtable = igvk_vtable_new(g_vk.name, IGVK_SLOTS);
        for (k = 0; k < IGVK_SLOTS; k++) {
            uint32_t e = RD32(src + (uint32_t)k * 4u);
            if (e == pure) { owed++; continue; }   /* left 0; filled below */
            WR32(g_vk.vtable + (uint32_t)k * 4u, e);
            inherited++;
        }
        printf("  vtable seeded from igVisualContext 0x%08x: %d inherited, "
               "%d pure-virtual owed\n", src, inherited, owed);
    }
    /* Ours regardless of what the base had: the meta must be OUR class's. */
    igvk_vtable_set(g_vk.vtable, 20, vk_get_class_meta, g_vk.name,
                    "getClassMeta", &g_vk);
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
