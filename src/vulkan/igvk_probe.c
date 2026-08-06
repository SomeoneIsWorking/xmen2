/*
 * Does a HOST class actually register with ARK, and will libIGCore construct
 * it? This answers C008's falsifier -- "nothing has yet been CONSTRUCTED this
 * way; the mechanism is read, not exercised" -- before any renderer work is
 * built on top of the assumption.
 *
 * Deliberately NOT the renderer. It derives from igObject, the root of the
 * class model, so it exercises registration and construction without
 * substituting anything the engine depends on. If this cannot be made to work,
 * a 209-method igVkVisualContext certainly cannot, and finding that out here
 * costs one small file instead of a subsystem.
 *
 * WHAT A FAILURE LOOKS LIKE, stated before the run rather than after: the two
 * ways this "passes" without proving anything are (a) igArkRegister returning
 * without filling the meta slot, and (b) createInstance returning an object
 * that is not ours. So the checks are on the META being non-NULL, on the
 * returned pointer being non-NULL, and on the object's vptr being the exact
 * vtable address we handed over -- not on the absence of a crash.
 */
#include "igvk_ark.h"

#include "x86rt.h"
#include "x86rt_native.h"

#include <stdio.h>

#define CORE "libIGCore.dll"

/* igObject's two static hooks, as libIGGfx itself imports them. */
#define IGOBJ_REGINTERNAL \
    "?arkRegisterInternal@igObject@Core@Gap@@SAPAV__internalFunctionList@23@XZ"
#define IGOBJ_GETCLASSMETA \
    "?getClassMeta@igObject@Core@Gap@@SAPAVigMetaObject@23@XZ"

/* Generous: igObject's own vtable length in libIGCore has not been read out,
   and over-allocating costs guest bytes while under-allocating would let a
   dispatch run off the end of the array into unrelated heap. */
#define PROBE_SLOTS 64

static int g_ctor_ran;

/* Slot 0 of an igObject vtable is the scalar-deleting destructor in MSVC's
   layout. We do not know that for certain here, which is the point: this is
   wired only to observe whether ANY slot of ours is dispatched during
   construction, and to say which. */
static void probe_slot0(CPU *C)
{
    g_ctor_ran++;
    /* MSVC's scalar-deleting destructor is __thiscall taking one stack
       argument (the deleting flag) and returning `this`. */
    ark_ret(C, C->ecx, 1);
}

/*
 * igObject's 21-slot interface, implemented from what the binary actually does
 * (C116). libIGCore still carries symbols, and igErrorHandler -- instance size
 * 0x8, i.e. igObject plus no fields -- has its whole vtable filled by five
 * distinct addresses, because MSVC folded every identical trivial body:
 *
 *     RET                       10 slots   do nothing, no stack argument
 *     RET 0x4                    7 slots   do nothing, one stack argument
 *     MOV AL,1 ; RET 0x4         2 slots   return true, one stack argument
 *     igObject::createCopy       slot 19
 *     getClassMeta               slot 20
 *
 * So the inherited behaviour is genuinely nothing for 17 of 21, and copying it
 * is not stubbing: it is what igObject itself does.
 */
static const unsigned char IGOBJ_RET0[]  = {0,2,8,9,10,12,13,14,15,16};
static const unsigned char IGOBJ_RET1[]  = {1,3,4,7,11,17,18};
static const unsigned char IGOBJ_TRUE1[] = {5,6};

static void ig_ret0(CPU *C)  { ark_ret(C, 0, 0); }
static void ig_ret1(CPU *C)  { ark_ret(C, 0, 1); }
static void ig_true1(CPU *C) { ark_ret(C, 1, 1); }

/*
 * Slot 20 is getClassMeta -- the object's own igMetaObject*.
 *
 * Identified, not guessed: 55 of libIGCore's own vtables carry a
 * `*::getClassMeta` at slot 20, and igObject::constructDerived dispatches
 * [vptr+0x50] and then increments +0x2c on whatever comes back, which is the
 * meta's instance count. A stub returning 0 would have "worked" here -- the
 * code checks for NULL and skips -- while silently leaving every instance
 * uncounted and unlinked from its class.
 *
 * __thiscall, no stack arguments.
 */
static void probe_get_class_meta(CPU *C);

/* Designated, so adding a field to ArkClass cannot silently shift these --
   which it already did once: the two lifted-address fields landed in the middle
   and turned base_get_class_meta into an integer. */
static ArkClass g_probe = {
    .name = "igVkProbe",
    .instance_size = 0x10,       /* comfortably over igObject's 8 */
    .is_abstract = 0,
    .base_module = CORE,
    .base_register_internal = IGOBJ_REGINTERNAL,
    .base_get_class_meta = IGOBJ_GETCLASSMETA,
    .nslots = PROBE_SLOTS
};

/*
 * WHEN this can run, which was not obvious and cost a fault to learn.
 *
 * Not at module-init time: igArkRegister goes through igGetMemoryPool, and the
 * engine's pools do not exist until the exe's startup has run. Registering
 * there faults inside libIGCore on `MOV EDX,[EAX]` with EAX=0 -- a NULL pool,
 * reported as a SIGSEGV in igGetMemoryPool with no mention of ARK.
 *
 * Not after --run returns either: the engine has torn down by then.
 *
 * So it is armed on the engine's own first igMetaObject::createInstance. That
 * call cannot happen before pools and ARK exist, because it allocates from a
 * pool and walks a meta -- which makes it a moment defined by the engine's
 * state rather than by a guess about ordering.
 */
static int g_probe_rc = -1;

static int probe_trigger(void)
{
    extern int igvk_ark_probe(void);
    g_probe_rc = igvk_ark_probe();
    return 1;                    /* runs once; it needs nothing but pools */
}

int igvk_ark_probe_arm(void)
{
    uint32_t ci = ark_export(CORE,
        "?createInstance@igMetaObject@Core@Gap@@QBEPAVigObject@23@"
        "PAVigMemoryPool@23@@Z");
    if (!ci) {
        fprintf(stderr, "probe: libIGCore does not export createInstance; "
                        "cannot arm\n");
        return 1;
    }
    x86_at_first_call(ci, probe_trigger,
                      "the engine's first createInstance -- proof that pools "
                      "and ARK are up, which is when a host class may register");
    printf("probe: armed on igMetaObject::createInstance 0x%08x\n", ci);
    return 0;
}

int igvk_ark_probe_result(void) { return g_probe_rc; }

int igvk_ark_probe(void)
{
    uint32_t meta, obj, vptr;
    int fails = 0;

    printf("\n=== ARK host-class probe (C008: is the mechanism EXERCISED?) ===\n");
    fflush(stdout);   /* an abort inside the guest must not swallow the report */

    g_probe.vtable = igvk_vtable_new(g_probe.name, PROBE_SLOTS);
    igvk_vtable_set(g_probe.vtable, 0, probe_slot0, g_probe.name,
                    "slot0", &g_probe);
    igvk_vtable_set(g_probe.vtable, 20, probe_get_class_meta, g_probe.name,
                    "getClassMeta", &g_probe);
    {
        size_t k;
        for (k = 0; k < sizeof IGOBJ_RET0; k++)
            igvk_vtable_set(g_probe.vtable, IGOBJ_RET0[k], ig_ret0,
                            g_probe.name, "igObject:<ret>", &g_probe);
        for (k = 0; k < sizeof IGOBJ_RET1; k++)
            igvk_vtable_set(g_probe.vtable, IGOBJ_RET1[k], ig_ret1,
                            g_probe.name, "igObject:<ret 4>", &g_probe);
        for (k = 0; k < sizeof IGOBJ_TRUE1; k++)
            igvk_vtable_set(g_probe.vtable, IGOBJ_TRUE1[k], ig_true1,
                            g_probe.name, "igObject:<return true>", &g_probe);
    }
    igvk_vtable_fill_unimplemented(g_probe.vtable, g_probe.name, PROBE_SLOTS, NULL);

    if (!ark_register_class(&g_probe)) {
        printf("  FAIL  registration did not complete\n");
        return 1;
    }

    meta = RD32(g_probe.meta_slot);
    printf("  ok    meta object allocated            0x%08x\n", meta);

    /* +0x48 is the instance size libIGCore recorded, +0x1a the isAbstract
       byte (docs/RE/ark.md). Reading them back checks that our eleven
       arguments landed where the engine expects, not merely that the call
       returned. */
    {
        uint32_t sz = RD32(meta + 0x48u);
        uint8_t ab = RD8(meta + 0x1au);
        if (sz != g_probe.instance_size) {
            printf("  FAIL  meta+0x48 instance size       0x%08x, want 0x%x\n",
                   sz, g_probe.instance_size);
            fails++;
        } else {
            printf("  ok    meta+0x48 instance size       0x%x\n", sz);
        }
        if (ab != 0) {
            printf("  FAIL  meta+0x1a isAbstract          %u, want 0 "
                   "(createInstance refuses an abstract meta)\n", ab);
            fails++;
        } else {
            printf("  ok    meta+0x1a isAbstract          0\n");
        }
    }

    obj = ark_create_instance(meta, 0);
    if (!obj) {
        printf("  FAIL  createInstance returned NULL -- libIGCore declined to "
               "construct the class\n");
        return fails + 1;
    }
    printf("  ok    createInstance returned         0x%08x\n", obj);

    vptr = RD32(obj);
    if (vptr != g_probe.vtable) {
        printf("  FAIL  object vptr 0x%08x, want our vtable 0x%08x\n"
               "        libIGCore built something, but not with our vtable, so "
               "dispatch would not reach this host at all.\n",
               vptr, g_probe.vtable);
        fails++;
    } else {
        printf("  ok    object vptr == our vtable      0x%08x\n", vptr);
    }
    printf("  info  host vtable slots dispatched during construction: %d\n",
           g_ctor_ran);

    printf("  %s\n", fails ? "PROBE FAILED" :
           "PROBE PASSED -- libIGCore constructs a host-defined class");
    fflush(stdout);
    return fails;
}

static void probe_get_class_meta(CPU *C)
{
    ark_ret(C, RD32(g_probe.meta_slot), 0);
}
