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

struct X86pCpu;

/* ---- talking to the guest --------------------------------------------- */

/* Mapped base of a loaded module, by file name ("libIGCore.dll"), or 0. */
uint32_t ark_module_base(const char *name);

/* Mapped address of an export, or 0. Names are the MSVC-mangled ones. */
uint32_t ark_export(const char *module, const char *mangled);

/*
 * Mapped address of a LINKED address in a module.
 *
 * The libIG*.dll files export almost nothing by name -- their ARK hooks are
 * static functions -- so a class's parent hooks cannot be reached the way
 * libIGCore's can. They CAN be named as linked addresses, which is what
 * tools/ark_classes.py recovers from each igArkRegister call, and every module
 * is linked for 0x10000000 and relocated at load, so mapping one is base
 * arithmetic. Returns 0 if the module is not loaded.
 */
uint32_t ark_lifted(const char *module, uint32_t linked_va);

/* Same, but abort with the name if it is missing. A registration built on a
   silently-zero function pointer fails much later and somewhere else. */
uint32_t ark_export_req(const char *module, const char *mangled);

/* Call a guest __cdecl function with `n` stack arguments; returns EAX. The
   caller's arguments are popped here, as cdecl requires. */
uint32_t ark_call_cdecl(uint32_t target, const uint32_t *args, int n);
uint32_t ark_call_stdcall(uint32_t target, const uint32_t *args, int n);

/* Call a guest __thiscall member: ECX = this, `n` stack arguments. */
uint32_t ark_call_this(uint32_t target, uint32_t self, const uint32_t *args,
                       int n);

/* A copy of `s` in guest-addressable memory, valid for the process lifetime. */
uint32_t ark_guest_str(const char *s);

/*
 * Return from a native callback the guest CALLED.
 *
 * Every guest CALL pushes a return address, and a real function's RET
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
void ark_ret(struct X86pCpu *C, uint32_t eax, int stack_args);

/* ---- describing a host class ------------------------------------------ */

typedef struct ArkClass {
  const char *name;       /* as ARK will know it */
  uint32_t instance_size; /* bytes, before the pool prefix */
  int is_abstract;

  /* The parent's two static hooks. Either by mangled export name (libIGCore
     exports its own), or -- when the module exports neither, which is every
     libIG*.dll but Core -- as LINKED addresses recovered by ark_classes.py.
     Exactly one pair must be set. */
  const char *base_module;
  const char *base_register_internal; /* mangled name, or NULL */
  const char *base_get_class_meta;    /* mangled name, or NULL */
  uint32_t base_register_internal_va; /* linked address, or 0 */
  uint32_t base_get_class_meta_va;    /* linked address, or 0 */

  /* Filled in by ark_register_class. */
  uint32_t meta_slot; /* guest address of our igMetaObject* */
  uint32_t vtable;    /* guest address of our vtable array */
  int nslots;

  /* Optional: run after registration, to append meta fields or bind an
     abstract parent's _Meta+0x3c. NULL if the class needs neither. */
  void (*on_initialize)(struct ArkClass *);
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
void igvk_vtable_set(uint32_t vtable, int slot, void (*fn)(struct X86pCpu *),
                     const char *owner, const char *name, void *ctx);
/*
 * Point every still-unset slot at the reporter.
 *
 * `slot_args` gives each slot's stack-argument count (from its own RET N,
 * generated by tools/device_slots.py), or NULL if unknown. It is what makes
 * the permissive mode below possible: popping the right count is the
 * difference between "this slot did nothing" and "this slot corrupted the
 * guest stack".
 */
void igvk_vtable_fill_unimplemented(uint32_t vtable, const char *owner,
                                    int nslots, const signed char *slot_args);

/*
 * PERMISSIVE MODE -- a staging tool, not a way of being finished.
 *
 * Off by default, and it must stay that way: the default is that an
 * unimplemented slot ABORTS by name and index, because a renderer that
 * silently ignores what it was asked is one whose output cannot be trusted.
 *
 * Turned on, an unimplemented slot returns zero and pops its arguments
 * correctly instead of stopping, so the engine can be walked THROUGH the
 * state calls that are not written yet and made to reach the frame boundary.
 * That answers one question and only one: does the engine's own
 * beginDraw/clear/endDraw drive this backend's frame path. Everything it
 * skips is counted per slot and printed at exit, so the result is always
 * read alongside the list of what was ignored to get it.
 *
 * A slot whose argument count is UNKNOWN still aborts even here -- guessing
 * it would shift the guest stack, which is a different and much worse
 * failure than not implementing the slot.
 */
void igvk_vtable_permissive(int on);
void igvk_vtable_permissive_report(void);

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
