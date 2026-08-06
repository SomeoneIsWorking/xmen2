/*
 * Registering a HOST-side class with the guest's ARK meta-object system.
 *
 * The mechanism is written up in docs/RE/ark.md and recorded as C008/C009. This
 * is the other half: the code that actually speaks it. C008's falsifier says
 * "nothing has yet been CONSTRUCTED this way -- the mechanism is read, not
 * exercised", and this exists to exercise it.
 *
 * Why a class rather than a hook: libIGCore owns construction. Every instance
 * goes through igMetaObject::createInstance, which walks the abstract ->
 * concrete chain at _Meta+0x3c and allocates from the meta's pool. Substituting
 * a renderer therefore means BEING one of those classes, not intercepting calls
 * to one -- and being one means registering through igArkRegister with the same
 * eleven arguments the engine's own classes pass.
 */
#ifndef IGVK_ARK_H
#define IGVK_ARK_H

#include <stdint.h>

struct CPU;

/* ---- talking to the guest --------------------------------------------- */

/* Mapped base of a loaded module, by file name ("libIGCore.dll"), or 0. */
uint32_t ark_module_base(const char *name);

/* Mapped address of an export, or 0. Names are the MSVC-mangled ones. */
uint32_t ark_export(const char *module, const char *mangled);

/* Same, but abort with the name if it is missing. A registration built on a
   silently-zero function pointer fails much later and somewhere else. */
uint32_t ark_export_req(const char *module, const char *mangled);

/* Call a guest __cdecl function with `n` stack arguments; returns EAX. The
   caller's arguments are popped here, as cdecl requires. */
uint32_t ark_call_cdecl(uint32_t target, const uint32_t *args, int n);

/* Call a guest __thiscall member: ECX = this, `n` stack arguments. */
uint32_t ark_call_this(uint32_t target, uint32_t self,
                       const uint32_t *args, int n);

/* A copy of `s` in guest-addressable memory, valid for the process lifetime. */
uint32_t ark_guest_str(const char *s);

/*
 * Return from a native callback the guest CALLED.
 *
 * Every emitted call site pushes a return address, and a real function's RET
 * pops it. A native stub reached through a synthetic address must do the same
 * or the guest stack drifts UPWARD by four bytes per call -- and the damage
 * does not surface at the callback. It surfaced, the first time this was got
 * wrong, as igArkRegister reading its dependentArkRegisters argument and
 * finding arkRegisterInitialize there: two un-popped return addresses had
 * shifted all eleven arguments by eight bytes, and the fault was a NULL
 * dereference two hundred instructions later in a different function.
 *
 * `stack_args` is how many arguments the CALLEE pops: 0 for __cdecl (the caller
 * pops) and for a no-argument __thiscall, N for __stdcall/__thiscall with N
 * stack arguments.
 */
void ark_ret(struct CPU *C, uint32_t eax, int stack_args);

/* ---- describing a host class ------------------------------------------ */

typedef struct ArkClass {
    const char *name;             /* as ARK will know it */
    uint32_t    instance_size;    /* bytes, before the pool prefix */
    int         is_abstract;

    /* The parent, by module + mangled name of its two static hooks. */
    const char *base_module;
    const char *base_register_internal;
    const char *base_get_class_meta;

    /* Filled in by ark_register_class. */
    uint32_t    meta_slot;        /* guest address of our igMetaObject* */
    uint32_t    vtable;           /* guest address of our vtable array */
    int         nslots;

    /* Optional: run after registration, to append meta fields or bind an
       abstract parent's _Meta+0x3c. NULL if the class needs neither. */
    void      (*on_initialize)(struct ArkClass *);
} ArkClass;

/*
 * Build a guest-callable vtable of `nslots` entries.
 *
 * Every slot must be filled before the class is registered. A slot left NULL is
 * a slot the engine will eventually dispatch through, and the resulting jump to
 * address 0 names nothing -- so unfilled slots are given a stub that aborts
 * reporting the CLASS and SLOT INDEX, which is the one piece of information
 * needed to go and implement it. See igvk_vtable_fill_unimplemented.
 */
uint32_t igvk_vtable_new(const char *owner, int nslots);
void     igvk_vtable_set(uint32_t vtable, int slot, void (*fn)(struct CPU *),
                         const char *owner, const char *name, void *ctx);
/* Point every still-unset slot at the aborting reporter. */
void     igvk_vtable_fill_unimplemented(uint32_t vtable, const char *owner,
                                        int nslots);

/*
 * Register the class with libIGCore. Returns 1 on success.
 *
 * This does NOT substitute anything. Binding an abstract class to this one is a
 * separate, explicit step (ark_bind_implementation), because registering is
 * reversible and harmless while substituting changes what the whole engine
 * constructs.
 */
int ark_register_class(ArkClass *c);

/* Point an already-registered abstract class's _Meta+0x3c at `c`, so
   createInstance resolves it to us. `abstract_meta` is the guest address of
   that class's igMetaObject* slot. */
int ark_bind_implementation(uint32_t abstract_meta_slot, ArkClass *c);

/* igMetaObject::createInstance(meta, pool) -- pool may be 0 for "the meta's
   own, else the current one". Returns the new object, or 0. */
uint32_t ark_create_instance(uint32_t meta, uint32_t pool);

#endif /* IGVK_ARK_H */
