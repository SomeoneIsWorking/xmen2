/*
 * The shared part of the native runtime: one dispatcher over several
 * recompiled modules.
 *
 * The hosted DLL build only ever had one module in the process, so its runtime
 * could keep the function table, the image base and the dispatcher as
 * process-wide globals. A native build links every module into one binary, and
 * that assumption breaks in a way that would not announce itself: every
 * libIG*.dll in this game is linked for 0x10000000, so guest entry points
 * COLLIDE across modules. A table keyed on entry point would happily answer
 * libIGCore's 0x10002c00 with libIGDisplay's function.
 *
 * So each module registers its own base, span and table, and dispatch resolves
 * by mapped ADDRESS -- which is unique, because pe_map gives each module a
 * distinct place to live.
 */
#ifndef X86RT_NATIVE_H
#define X86RT_NATIVE_H

#include <stdint.h>

struct CPU;

typedef struct X86Fn {
    uint32_t    ep;                  /* guest entry point at the PREFERRED base */
    void      (*fn)(struct CPU *);
    const char *name;
} X86Fn;

typedef struct X86Import {
    uint32_t    slot_rva;            /* where the IAT slot lives in the image */
    void      (*stub)(struct CPU *); /* the native (or aborting) stub for it */
    const char *mod;
    const char *sym;
} X86Import;

typedef struct X86Module {
    const char     *name;
    uint32_t       *base;            /* where it actually got mapped */
    uint32_t        preferred;       /* what it was linked for */
    uint32_t        size;            /* SizeOfImage */
    const X86Fn    *fns;
    int             nfns;
    const X86Import *imports;
    int             nimports;
    struct X86Module *next;
} X86Module;

/* Called from each generated module's constructor. */
void x86_module_register(X86Module *m);

/* Which module owns a mapped address, or NULL. */
X86Module *x86_module_for(uint32_t addr);

/* Run the recompiled body at a mapped address. Returns 0 if there is none --
   the caller must say so rather than treating a miss as a no-op. */
int x86_native_call_at(uint32_t addr, struct CPU *C);

/* Name of the body at a mapped address, or NULL. */
const char *x86_native_name_at(uint32_t addr);

/* A guest-callable address for a native C function, so engine code can call
   back into the host -- ARK hooks and the slots of a native class's vtable.
   `owner`/`name` appear in ring lines and fault reports; both are required. */
uint32_t x86_native_callback(void (*fn)(struct CPU *), const char *owner,
                             const char *name, void *ctx);

/* Inside a callback: the `ctx` it was registered with. One C function can then
   serve many objects, told apart by which synthetic address the guest called. */
void *x86_callback_ctx(void);

/* Head of the registered-module list. */
X86Module *x86_modules(void);

/* Run `fn` the first time the guest calls `addr`, before the body. Used to act
   at a moment during the run -- engine startup completes long after module
   init, and some host work (ARK registration) is only legal once it has. */
void x86_at_first_call(uint32_t addr, void (*fn)(void), const char *why);

/* Complain about every armed trigger that never fired; returns how many. */
int x86_triggers_report(void);

/* Bind an IAT slot to a callable address when the import is implemented
   natively but is not another recompiled module: the guest sometimes takes an
   import's address and calls through it, bypassing the named stub. Returns 0
   if there is no native implementation for that slot. */
uint32_t x86_native_thunk(const char *mod, const char *sym);

/* Dump guest memory named by X2_PEEK (see the definition for the format).
   Safe from a signal handler: reads via process_vm_readv, so an unmapped
   address reports itself instead of faulting again. */
void x86_peek_report(void);

/* Guest registers at a fault: the file of the last body to cross the host
   boundary, which guest-to-guest calls share. */
void x86_regs_dump(void);

/* Peek + reached + args + ring, in one call. Every stop path uses this, because
   abort() does not run atexit handlers and the reports registered there were
   silent on exactly the failures worth reporting. */
void x86_diag_dump(void);

/* Argument watch (X2_ARGS), trace builds only. Reports at exit whether any
   watched entry point was entered at all. */
void x86_args_report(void);

/* Every registered module, for reporting. */
X86Module *x86_modules(void);

#endif /* X86RT_NATIVE_H */
