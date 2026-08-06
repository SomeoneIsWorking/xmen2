/*
 * igVkVisualContext -- the class itself: registration, the vtable, and the
 * substitution. The slots live in igvk_slots_*.c; see igvk_context.h for the
 * design and for why super-calling is the default.
 *
 * Addresses below are LINKED addresses recovered by tools/ark_classes.py from
 * libIGGfx's own igArkRegister calls, and by tools/device_slots.py from its
 * vtables. They are not guesses, and ark_lifted() maps them through the
 * module's real load base.
 */
#include "igvk_context.h"
#include "gpu_device.h"
#include "igvk_device_slots.h"

#include "x86rt_native.h"

#include <stdio.h>
#include <string.h>

/* From tools/ark_classes.py over scratch/recomp/libIGGfx.json:
     igVisualContext   abstract, size 0x140, meta slot 0x10188ba0,
                       arkRegisterInternal 0x1000b440, getClassMeta 0x1004afc0
   The last two are exactly the pair igDxVisualContext passes as ITS parent
   hooks, which is the cross-check that they are igVisualContext's. */
#define IGVISUALCONTEXT_META_SLOT      0x10188ba0u
#define IGDXVISUALCONTEXT_META_SLOT    0x10189048u
#define IGDX8VISUALCONTEXT_META_SLOT   0x10189450u

/*
 * OUR parent is igDx8VisualContext, the concrete class we replace -- not the
 * abstract igVisualContext at the top of the chain.
 *
 * This was igVisualContext's pair, and that was the bug behind issue #20.
 * libIGCore runs the PARENT's construction, so inheriting from the abstract
 * root meant igDxVisualContext's constructor never ran: the capability
 * manager at this+0x534 and the shader manager at this+0x53c were never
 * built, and the engine's own teardown dereferenced the latter unguarded
 * (C121). Inheriting from the leaf runs the whole chain, and only the vtable
 * is ours -- which is what a C++ subclass of igDx8VisualContext would be.
 *
 * Both addresses come from the class's OWN igArkRegister call, recovered by
 * tools/ark_classes.py: a child passes its parent's registrar and its
 * parent's getClassMeta. getClassMeta is the non-"safe" form -- every one is
 * literally `MOV EAX,[<the class's meta slot>]; RET`, and this one was found
 * by searching libIGGfx for that exact two-instruction body reading
 * igDx8VisualContext's meta slot. It is also vtable slot 20 of
 * igDx8VisualContext, which is an independent confirmation that it is the
 * right function.
 */
#define IGDX8_REGINTERNAL              0x10014a60u
#define IGDX8_GETCLASSMETA             0x10009870u

/* igDxVisualContext and igDx8VisualContext are both 0x558, and the Dx8 leaf
   adds no fields (C113). Matching it means the engine gets an object the same
   size as the one it used to get -- anything that reads an igDxVisualContext
   field offset finds allocated memory rather than heap past the end. */
#define IGVK_INSTANCE_SIZE  0x558u

/* igVisualContext's vtable is 334 slots (C114). */
#define IGVK_SLOTS  334

#define IGDX8VISUALCONTEXT_VTABLE 0x100dd0a0u
#define IGGFX_PURECALL            0x100ce258u

#define IGGFX_STATUS_OK_PP        0x100cf4d4u
#define IGGFX_STATUS_FAIL_PP      0x100cf4d0u

static ArkClass g_vk;

/* ---- small services the slot modules use ------------------------------ */

float igvk_argf(CPU *C, int i)
{
    union { uint32_t u; float f; } v;
    v.u = IGVK_ARG(C, i);
    return v.f;
}

float igvk_fieldf(uint32_t self, uint32_t off)
{
    union { uint32_t u; float f; } v;
    v.u = RD32(self + off);
    return v.f;
}

static uint32_t status_value(uint32_t pp_linked)
{
    uint32_t pp = ark_lifted(IGVK_GFX, pp_linked);
    uint32_t p = pp ? RD32(pp) : 0;
    return p ? RD32(p) : 0;
}

uint32_t igvk_status_ok(void)   { return status_value(IGGFX_STATUS_OK_PP); }
uint32_t igvk_status_fail(void) { return status_value(IGGFX_STATUS_FAIL_PP); }

void igvk_ret_status(CPU *C, uint32_t out, uint32_t status, int nargs)
{
    if (out) WR32(out, status);
    ark_ret(C, out, nargs);
}

uint32_t igvk_super(CPU *C, uint32_t linked_va, int nargs)
{
    uint32_t fn = ark_lifted(IGVK_GFX, linked_va);
    uint32_t args[8];
    int i;

    if (nargs > (int)(sizeof args / sizeof args[0])) {
        fprintf(stderr, "igVk: igvk_super(0x%08x) with %d arguments, more than "
                        "it can forward. Refusing rather than truncating.\n",
                linked_va, nargs);
        return 0;
    }
    if (!fn) {
        /* Not survivable: the slot's whole implementation was "let the engine
           do it", and the engine's body is not there. Anything returned here
           would be an invention. */
        fprintf(stderr, "igVk: cannot map the engine body at linked 0x%08x -- "
                        "%s is not loaded. The slot that super-called it has "
                        "no answer to give.\n", linked_va, IGVK_GFX);
        return 0;
    }
    for (i = 0; i < nargs; i++) args[i] = IGVK_ARG(C, i);
    return ark_call_this(fn, IGVK_SELF(C), args, nargs);
}

void igvk_slot(int slot, void (*fn)(CPU *), const char *name)
{
    igvk_vtable_set(g_vk.vtable, slot, fn, g_vk.name, name, &g_vk);
}

/* ---- the class -------------------------------------------------------- */

static void vk_get_class_meta(CPU *C)
{
    ark_ret(C, RD32(g_vk.meta_slot), 0);
}

static ArkClass g_vk = {
    .name = "igVkVisualContext",
    .instance_size = IGVK_INSTANCE_SIZE,
    .is_abstract = 0,
    .base_module = IGVK_GFX,
    .base_register_internal_va = IGDX8_REGINTERNAL,
    .base_get_class_meta_va = IGDX8_GETCLASSMETA,
    .nslots = IGVK_SLOTS
};

/*
 * The inventory. One line per slot module, in the order they install.
 *
 * Everything not bound by one of these keeps igDx8VisualContext's own
 * function if the slot is platform-neutral, or the aborting reporter if it is
 * one of the 98 that reach the device -- and that reporter names the class and
 * SLOT INDEX, which is the renderer's work queue naming itself.
 */
static void install_slots(void)
{
    igvk_install_lifecycle();
    igvk_install_frame();
}

static int build_vtable(void)
{
    uint32_t src = ark_lifted(IGVK_GFX, IGDX8VISUALCONTEXT_VTABLE);
    uint32_t pure = ark_lifted(IGVK_GFX, IGGFX_PURECALL);
    int inherited = 0, still_pure = 0, k, n;

    if (!src || !pure) {
        fprintf(stderr, "igVk: cannot map igDx8VisualContext's vtable\n");
        return 0;
    }
    /*
     * Seed from igDx8VisualContext, not from the abstract base.
     *
     * C119: of its 334 slots only 98 reach the DirectX device fields. The
     * other 236 are platform-neutral engine bookkeeping -- render destination
     * pools, format tables, capability queries over the class's own fields --
     * and inheriting them verbatim is both correct and free. Seeding from the
     * abstract igVisualContext instead left 209 slots owed, and the first two
     * the engine asked for turned out to be exactly that kind of bookkeeping.
     */
    g_vk.vtable = igvk_vtable_new(g_vk.name, IGVK_SLOTS);
    for (k = 0; k < IGVK_SLOTS; k++) {
        uint32_t e = RD32(src + (uint32_t)k * 4u);
        if (e == pure) { still_pure++; continue; }
        WR32(g_vk.vtable + (uint32_t)k * 4u, e);
        inherited++;
    }
    /* Punch out every device-touching slot so it reports instead of
       dispatching into DirectX code. */
    for (n = 0; n < IGVK_DEVICE_SLOT_COUNT; n++)
        WR32(g_vk.vtable + (uint32_t)IGVK_DEVICE_SLOTS[n] * 4u, 0);
    printf("  vtable seeded from igDx8VisualContext 0x%08x: %d inherited, "
           "%d device-touching slots removed, %d still pure\n",
           src, inherited, IGVK_DEVICE_SLOT_COUNT, still_pure);

    /* Ours regardless of what the base had: the meta must be OUR class's. */
    igvk_slot(20, vk_get_class_meta, "getClassMeta");
    install_slots();
    igvk_vtable_fill_unimplemented(g_vk.vtable, g_vk.name, IGVK_SLOTS,
                                   IGVK_SLOT_ARGS);
    return 1;
}

/*
 * Rebind the WHOLE chain, not just the abstract root.
 *
 * createInstance follows _Meta+0x3c from whichever meta it was handed, so
 * binding igVisualContext alone only catches code that instantiates the
 * abstract class. Anything calling igDxVisualContext::_instantiateFromPool
 * uses THAT meta, whose +0x3c still points at igDx8VisualContext -- which is
 * exactly what happened: the binding installed, and the engine built a
 * DirectX context anyway and called Direct3DCreate8.
 */
static int rebind_chain(void)
{
    static const struct { const char *name; uint32_t slot; } chain[] = {
        { "igVisualContext",    IGVISUALCONTEXT_META_SLOT },
        { "igDxVisualContext",  IGDXVISUALCONTEXT_META_SLOT },
        { "igDx8VisualContext", IGDX8VISUALCONTEXT_META_SLOT },
    };
    size_t n;
    int bound = 0;

    for (n = 0; n < sizeof chain / sizeof chain[0]; n++) {
        uint32_t ms = ark_lifted(IGVK_GFX, chain[n].slot);
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
    return bound;
}

static int g_done;

int igvk_context_install(void)
{
    uint32_t meta_slot, meta;

    if (g_done) return 1;

    meta_slot = ark_lifted(IGVK_GFX, IGVISUALCONTEXT_META_SLOT);
    if (!meta_slot) {
        fprintf(stderr, "igVk: %s is not loaded; nothing was substituted.\n",
                IGVK_GFX);
        return 1;                        /* waiting cannot fix this */
    }
    /*
     * Readiness is igDx8VisualContext existing, not igVisualContext.
     *
     * The abstract root registers well before its DirectX subclasses, and
     * binding at that moment left igDxVisualContext and igDx8VisualContext
     * unregistered and therefore un-rebound -- so anything instantiating those
     * directly still built a DirectX context and still called
     * Direct3DCreate8. Waiting for the CONCRETE class we are replacing means
     * the whole chain exists by the time we rebind it.
     */
    {
        uint32_t dx8 = ark_lifted(IGVK_GFX, IGDX8VISUALCONTEXT_META_SLOT);
        if (!dx8 || !RD32(dx8)) return 0;
    }
    meta = RD32(meta_slot);
    if (!meta) return 0;

    g_done = 1;
    printf("\n=== igVkVisualContext: substituting the renderer ===\n");
    printf("  igVisualContext meta            0x%08x\n", meta);
    fflush(stdout);

    if (!build_vtable()) return 1;

    if (!ark_register_class(&g_vk)) {
        fprintf(stderr, "igVk: registration failed; NOTHING was substituted.\n");
        return 1;
    }
    if (!rebind_chain()) {
        fprintf(stderr, "igVk: nothing was rebound; the engine will build its "
                        "DirectX context as before.\n");
        return 1;
    }
    printf("  igVisualContext now resolves to igVkVisualContext.\n"
           "  From here the engine dispatches into this host, and every slot "
           "it needs that is\n"
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
static int install_trampoline(void) { return igvk_context_install(); }

int igvk_context_arm(void)
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
