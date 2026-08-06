/* See igvk_ark.h. The mechanism this speaks is docs/RE/ark.md (C008/C009). */
#include "igvk_ark.h"

#include "x86rt.h"
#include "x86rt_native.h"
#include "pe_map.h"
#include "guest_heap.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define A(i) RD32(C->esp + 4u + (uint32_t)(i) * 4u)

/* ---- talking to the guest --------------------------------------------- */

uint32_t ark_module_base(const char *name)
{
    /* The dispatcher's module list is the authority on what actually got
       mapped and where; pe_map's own bookkeeping is per-image. */
    X86Module *m;
    for (m = x86_modules(); m; m = m->next)
        if (m->name && strcasecmp(m->name, name) == 0)
            return *m->base;
    return 0;
}

uint32_t ark_export(const char *module, const char *mangled)
{
    uint32_t base = ark_module_base(module), rva;
    if (!base) return 0;
    rva = pe_export_rva(base, mangled);
    return rva ? base + rva : 0;
}

uint32_t ark_lifted(const char *module, uint32_t linked_va)
{
    uint32_t base = ark_module_base(module);
    /* Every libIG*.dll is linked for 0x10000000; pe_map relocates all but the
       first. Anything outside that span is a caller error, not a relocation. */
    if (!base || linked_va < 0x10000000u) return 0;
    return base + (linked_va - 0x10000000u);
}

uint32_t ark_export_req(const char *module, const char *mangled)
{
    uint32_t a = ark_export(module, mangled);
    if (!a) {
        fprintf(stderr,
                "ark: %s does not export\n    %s\n"
                "  Registration cannot proceed on a NULL function pointer: "
                "libIGCore would store it and call it much later, and the "
                "crash would name neither this symbol nor this file.\n",
                module, mangled);
        abort();
    }
    return a;
}

/*
 * Guest calls.
 *
 * x86_guest_call pushes the return address and checks the stack balance, so the
 * only thing left here is placing arguments. They go BELOW the current stack
 * top, and ESP is restored afterwards, so a call made from host code never
 * grows the guest stack permanently.
 */
static uint32_t g_call_sp;          /* scratch guest stack for host->guest calls */

static void ensure_stack(void)
{
    if (!g_call_sp) {
        /* 256 KiB, its own arena, so a runaway guest call cannot walk into
           the engine's stack and be diagnosed as engine corruption. */
        uint32_t n = 256u * 1024u;
        uint32_t p = guest_malloc(n);
        if (!p) {
            fprintf(stderr, "ark: cannot allocate a guest stack for "
                            "host-to-guest calls\n");
            abort();
        }
        g_call_sp = p + n - 64u;
    }
}

uint32_t ark_call_cdecl(uint32_t target, const uint32_t *args, int n)
{
    CPU C;
    int i;
    ensure_stack();
    memset(&C, 0, sizeof C);
    C.esp = g_call_sp - (uint32_t)n * 4u;
    for (i = 0; i < n; i++) WR32(C.esp + (uint32_t)i * 4u, args[i]);
    x86_guest_call(&C, target);
    return C.eax;
}

uint32_t ark_call_this(uint32_t target, uint32_t self,
                       const uint32_t *args, int n)
{
    CPU C;
    int i;
    ensure_stack();
    memset(&C, 0, sizeof C);
    C.ecx = self;
    C.esp = g_call_sp - (uint32_t)n * 4u;
    for (i = 0; i < n; i++) WR32(C.esp + (uint32_t)i * 4u, args[i]);
    x86_guest_call(&C, target);
    return C.eax;
}

uint32_t ark_guest_str(const char *s)
{
    uint32_t n = (uint32_t)strlen(s) + 1u, p = guest_malloc(n);
    if (!p) {
        fprintf(stderr, "ark: no guest memory for the string \"%s\"\n", s);
        abort();
    }
    memcpy((void *)(uintptr_t)p, s, n);
    return p;
}

void ark_ret(CPU *C, uint32_t eax, int stack_args)
{
    C->eax = eax;
    C->esp += 4u + (uint32_t)stack_args * 4u;
}

/* ---- vtables ----------------------------------------------------------- */

/*
 * The reporter every unset slot points at.
 *
 * The whole reason a native class is viable is C009: libIGCore stamps whatever
 * vtable pointer we hand it, so slot ORDER is the only thing that has to match.
 * That makes an unimplemented slot the expected state for most of the 209, not
 * an error -- what matters is that reaching one says WHICH one, because the
 * slot index is exactly what has to be looked up to implement it.
 *
 * A slot left at 0 instead would jump to address 0 and report nothing at all.
 */
#define MAX_VT 8
static struct { uint32_t va; const char *owner; int nslots; } g_vt[MAX_VT];
static int g_nvt;

/* One record per unimplemented slot, so the report can name the INDEX. The
   first version shared a single stub across every slot to save synthetic
   addresses; it fired correctly and said only "some slot", which is the one
   thing the reader already knew. Saving 63 table entries was not worth a
   diagnostic that cannot answer its own question. */
typedef struct { const char *owner; int slot; } Unimpl;

static void unimplemented(CPU *C)
{
    const Unimpl *u = (const Unimpl *)x86_callback_ctx();
    fprintf(stderr,
            "\n*** %s: the engine dispatched vtable SLOT %d, which this host "
            "class does not implement.\n"
            "    Implement it, or -- if it should be inherited -- copy the "
            "parent vtable's entry for that slot.\n",
            u ? u->owner : "(a native ARK class)", u ? u->slot : -1);
    fflush(stderr);
    x86_diag_dump();
    abort();
    (void)C;
}

uint32_t igvk_vtable_new(const char *owner, int nslots)
{
    uint32_t va;
    if (g_nvt == MAX_VT) {
        fprintf(stderr, "ark: more than %d native vtables\n", MAX_VT);
        abort();
    }
    va = guest_malloc((uint32_t)nslots * 4u);
    if (!va) {
        fprintf(stderr, "ark: no guest memory for a %d-slot vtable (%s)\n",
                nslots, owner);
        abort();
    }
    memset((void *)(uintptr_t)va, 0, (size_t)nslots * 4u);
    g_vt[g_nvt].va = va;
    g_vt[g_nvt].owner = owner;
    g_vt[g_nvt].nslots = nslots;
    g_nvt++;
    return va;
}

void igvk_vtable_set(uint32_t vtable, int slot, void (*fn)(CPU *),
                     const char *owner, const char *name, void *ctx)
{
    WR32(vtable + (uint32_t)slot * 4u,
         x86_native_callback(fn, owner, name, ctx));
}

void igvk_vtable_fill_unimplemented(uint32_t vtable, const char *owner,
                                    int nslots)
{
    Unimpl *recs = (Unimpl *)calloc((size_t)nslots, sizeof *recs);
    int k, n = 0;
    if (!recs) { fprintf(stderr, "ark: out of memory\n"); abort(); }
    for (k = 0; k < nslots; k++)
        if (RD32(vtable + (uint32_t)k * 4u) == 0) {
            recs[k].owner = owner;
            recs[k].slot = k;
            WR32(vtable + (uint32_t)k * 4u,
                 x86_native_callback(unimplemented, owner,
                                     "<unimplemented slot>", &recs[k]));
            n++;
        }
    printf("  %s: %d of %d vtable slots implemented, %d left reporting by "
           "index\n", owner, nslots - n, nslots, n);
    fflush(stdout);
}

/* ---- registration ------------------------------------------------------ */

#define CORE "libIGCore.dll"
#define SYM_ARKREG11 \
    "?igArkRegister@Core@Gap@@YAPAV__internalFunctionList@12@_N" \
    "PAPAVigMetaObject@12@P6APAV312@XZP6APAV412@XZ3PBDHP6APAXXZ" \
    "P6AXXZ6PAP6AXXZ@Z"
#define SYM_CREATEINSTANCE \
    "?createInstance@igMetaObject@Core@Gap@@QBEPAVigObject@23@" \
    "PAVigMemoryPool@23@@Z"

/*
 * Which class a hook belongs to.
 *
 * These are shared C functions, but libIGCore calls them back long after
 * registration -- getClassMetaSafe in particular is what createInstance follows
 * from _Meta+0x3c on every single instantiation. A "class currently being
 * registered" global would therefore be NULL exactly when it matters and return
 * meta 0, which createInstance reads as "abstract" and refuses. The identity
 * comes from the synthetic address the guest called instead, which is distinct
 * per class because each gets its own callback registration.
 */
static ArkClass *hook_self(void)
{
    ArkClass *c = (ArkClass *)x86_callback_ctx();
    if (!c) {
        fprintf(stderr, "ark: an ARK hook ran with no class context. The "
                        "callback was registered without one, so there is no "
                        "way to know which class the engine is asking about.\n");
        abort();
    }
    return c;
}

/* Guards against a second registration overlapping the first. */
static ArkClass *g_registering;

static void hook_get_class_meta_safe(CPU *C)
{
    /* __cdecl, no arguments -- pops only its return address. */
    ark_ret(C, RD32(hook_self()->meta_slot), 0);
}

static void hook_retrieve_vtable(CPU *C)
{
    /*
     * The real classes build a throwaway instance, stamp their vtable into it
     * and read it back at the offset igArkCore keeps at +0x394 -- because the
     * COMPILER decided where the vptr sits and they must not assume. We are not
     * a C++ object: our vptr is wherever we say, and we say offset 0. So the
     * honest answer is simply the vtable address, with no dance.
     */
    ark_ret(C, hook_self()->vtable, 0);
}

static void hook_register_initialize(CPU *C)
{
    ArkClass *c = hook_self();
    if (c->on_initialize) c->on_initialize(c);
    ark_ret(C, 0, 0);
}

int ark_register_class(ArkClass *c)
{
    uint32_t args[11], reg11, meta;
    uint32_t name_va;

    if (g_registering) {
        fprintf(stderr, "ark: %s cannot be registered while %s is mid-"
                        "registration -- the hooks libIGCore calls back carry "
                        "no class identity, so this must be sequential.\n",
                c->name, g_registering->name);
        abort();
    }

    reg11 = ark_export_req(CORE, SYM_ARKREG11);

    /* The meta slot must be guest-addressable: libIGCore writes our
       igMetaObject* into it. */
    c->meta_slot = guest_malloc(4);
    if (!c->meta_slot) return 0;
    WR32(c->meta_slot, 0);
    name_va = ark_guest_str(c->name);

    g_registering = c;
    args[0]  = (uint32_t)(c->is_abstract ? 1 : 0);
    args[1]  = c->meta_slot;
    if (c->base_register_internal) {
        args[2] = ark_export_req(c->base_module, c->base_register_internal);
        args[3] = ark_export_req(c->base_module, c->base_get_class_meta);
    } else {
        args[2] = ark_lifted(c->base_module, c->base_register_internal_va);
        args[3] = ark_lifted(c->base_module, c->base_get_class_meta_va);
        if (!args[2] || !args[3]) {
            fprintf(stderr,
                    "ark: cannot map %s's parent hooks (linked 0x%08x / "
                    "0x%08x) -- is the module loaded?\n  Registration on a "
                    "NULL parent would be accepted here and fail much later "
                    "inside libIGCore.\n",
                    c->name, c->base_register_internal_va,
                    c->base_get_class_meta_va);
            return 0;
        }
    }
    args[4]  = x86_native_callback(hook_get_class_meta_safe, c->name,
                                   "getClassMetaSafe", c);
    args[5]  = name_va;
    args[6]  = c->instance_size;
    args[7]  = c->is_abstract ? 0u
             : x86_native_callback(hook_retrieve_vtable, c->name,
                                   "retrieveVTablePointer", c);
    args[8]  = x86_native_callback(hook_register_initialize, c->name,
                                   "arkRegisterInitialize", c);
    args[9]  = 0;                                  /* arkRegisterUser */
    args[10] = 0;                                  /* dependentArkRegisters */

    ark_call_cdecl(reg11, args, 11);
    g_registering = NULL;

    meta = RD32(c->meta_slot);
    if (!meta) {
        fprintf(stderr,
                "ark: igArkRegister returned without filling in %s's meta "
                "slot.\n  The class is NOT registered. Nothing downstream of "
                "this may be treated as working.\n", c->name);
        return 0;
    }
    printf("  ark: registered %-22s meta=0x%08x size=0x%-4x %s\n",
           c->name, meta, c->instance_size,
           c->is_abstract ? "(abstract)" : "");
    return 1;
}

int ark_bind_implementation(uint32_t abstract_meta_slot, ArkClass *c)
{
    uint32_t am = RD32(abstract_meta_slot), ours = RD32(c->meta_slot);
    if (!am || !ours) {
        fprintf(stderr, "ark: cannot bind -- abstract meta %s, our meta %s\n",
                am ? "ok" : "NULL", ours ? "ok" : "NULL");
        return 0;
    }
    /*
     * +0x3c holds igMetaObject *(*)(void), not an igMetaObject *:
     * createInstance CALLS it and follows the result. Ours is the same
     * getClassMetaSafe hook the registration handed over.
     */
    WR32(am + 0x3Cu, x86_native_callback(hook_get_class_meta_safe, c->name,
                                         "getClassMetaSafe(bound)", c));
    printf("  ark: abstract meta 0x%08x now resolves to %s\n", am, c->name);
    return 1;
}

uint32_t ark_create_instance(uint32_t meta, uint32_t pool)
{
    uint32_t fn = ark_export_req(CORE, SYM_CREATEINSTANCE);
    uint32_t a[1];
    a[0] = pool;
    return ark_call_this(fn, meta, a, 1);
}
