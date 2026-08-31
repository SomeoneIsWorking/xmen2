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

#include "x86_tail_policy.h"

#include <stdint.h>
#include <stddef.h>

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
    /* POINTER TO where it actually got mapped -- the generated module owns the
       variable and the loader fills it in, so this is `*m->base`, never
       `m->base`. Reading the pointer as the base gives a host address that can
       look plausible; it cost two crashes in a shutdown diagnostic. */
    uint32_t       *base;
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

/* The MAPPED entry point of the function containing `addr`, and its name.
   An approximation -- the table has entry points, not sizes -- so check the
   name before acting on it. See the note in x86rt_native.c. */
uint32_t x86_native_entry_containing(uint32_t addr, const char **name_out);

/* A guest-callable address for a native C function, so engine code can call
   back into the host -- ARK hooks and the slots of a native class's vtable.
   `owner`/`name` appear in ring lines and fault reports; both are required. */
uint32_t x86_native_callback(void (*fn)(struct CPU *), const char *owner,
                             const char *name, void *ctx);

/* Inside a callback: the `ctx` it was registered with. One C function can then
   serve many objects, told apart by which synthetic address the guest called. */
void *x86_callback_ctx(void);

/* Host->guest call with an explicit stdcall/thiscall cleanup contract. The
   zero-argument/cdecl wrapper remains declared by x86rt.h. */
void x86_guest_call_args(struct CPU *C, uint32_t target,
                         uint32_t callee_pop_bytes);

/*
 * Publish an entry point of a module this host implements but nothing
 * statically imports, so a run-time LoadLibraryA + GetProcAddress can find it.
 *
 * x86_native_thunk resolves through the mapped modules' import tables, which
 * cannot see a symbol the guest looks up by name at run time -- and that is how
 * XMen2.exe reaches dinput8.dll (issue #32). Registering here is the single
 * source of truth for BOTH questions: which entry points exist, and therefore
 * which modules LoadLibraryA may honestly hand back a handle for.
 */
void     x86_native_export(const char *mod, const char *sym,
                           void (*fn)(struct CPU *));
uint32_t x86_native_export_addr(const char *mod, const char *sym);
int      x86_native_module_implemented(const char *mod);
void     x86_native_export_report(void);

/* Head of the registered-module list. */
X86Module *x86_modules(void);

/* Lookup helpers for diagnostics. A thunk is a bound native import; poison is
 * an intentionally unmapped placeholder for an import that could not bind. */
const char *x86_thunk_name(uint32_t addr, const char **module_out);
const char *x86_poison_name(uint32_t addr, const char **module_out);

/* Run `fn` when the guest calls `addr`, before the body, RETRYING on every
   call until it returns non-zero. Used to act at a moment during the run --
   engine startup completes long after module init, and some host work (ARK
   registration) is only legal once the subsystem it touches has registered,
   which is a state the handler must test rather than a call site we can name. */
void x86_at_first_call(uint32_t addr, int (*fn)(void), const char *why);

/* Complain about every armed trigger that never fired; returns how many. */
int x86_triggers_report(void);

/*
 * Register a NATIVE implementation of a guest entry point, declared in C where
 * the override belongs (src/native/startup.c, movie.c, reportbox.c, ...) --
 * no JSON, no generator. The dispatcher consults this table BEFORE the
 * recompiled body, so both direct calls (which recomp.py routes through the
 * dispatcher when the target is registered here) and vtable/callback dispatch
 * reach the native function. The recompiled body stays emitted and linked, so
 * an override that defers to the original just calls its fn_<module>_<ep>
 * symbol directly.
 *
 * `module` is the module that owns the entry point and `linked_ep` is the
 * address at that module's PREFERRED base -- the address the disassembly
 * shows. A bare entry point is NOT a key: every libIG*.dll is linked for
 * 0x10000000, so one linked address names a different function in each of
 * them, while the dispatcher works in mapped addresses. x86_overrides_resolve
 * turns each pair into the mapped address once the modules are in place.
 */
/* x86_override_fn, X86OverrideSlot and x86_override_slots_register live in
   x86rt.h -- the GENERATED code needs them and a generated chunk includes
   only that header -- so this one includes it rather than redeclaring them
   and letting the two drift. */
#include "x86rt.h"
long x86_override_slot_count(void);
int  x86_override_chunk_count(void);
void x86_register_override(const char *module, uint32_t linked_ep,
                           x86_override_fn fn);
int x86_override_is_bound(const char *module, uint32_t linked_ep,
                          x86_override_fn fn);

/*
 * Resolve every registration to a mapped address. Call once, after all modules
 * are mapped and before any guest code runs -- registration happens in
 * constructors, long before pe_map has placed anything. Aborts if a module is
 * missing or an entry point names no recompiled body, because an override that
 * does not resolve never fires and the run still looks healthy.
 */
void x86_overrides_resolve(void);

/*
 * Resolve one (module, linked_ep) the way x86_overrides_resolve does, but
 * report instead of aborting: 0 and *mapped_out on success, non-zero with the
 * reason in `why`. Exists so --override-selftest can show the resolver
 * ACCEPTING a real override and REJECTING an unmapped module, an address
 * outside the image and a mid-function address -- a resolver only ever seen
 * accepting is not known to reject anything.
 */
int x86_override_resolve_check(const char *module, uint32_t linked_ep,
                               uint32_t *mapped_out, char *why, size_t whyn);

/*
 * The reached set -- "was this body ever entered, how often, and in what
 * order" -- compiled into every native build and ARMED at runtime. Arming
 * late is honest but lossy, and the report says so; -DX2_NATIVE_REACHED=ON
 * arms before the first guest instruction for the cases that need it.
 */
void x86_reached_arm(const char *why);
int  x86_reached_is_armed(void);
/* Ask about one linked entry point: 1 if it was entered, with *count and *seq
   filled in (seq is the 1-based order of first entry). 0 for never entered --
   which is only meaningful when the set is armed, so callers must check. */
int  x86_reached_query(uint32_t linked_ep, const char *module,
                       unsigned long *count, unsigned long *seq);

/* Count of registered native overrides (0 is a measurement, not silence). */
int x86_override_count(void);

/*
 * Sampling profiler (X2_PROFILE=<period-ms>): a thread samples the running
 * guest body every period and histograms the samples, so a body that runs a
 * lot is sampled a lot. This is the instrument that CAN name a load-window
 * hotspot -- the hotep hash cannot, because the level build dispatches ~460k
 * distinct entry points and a fixed hash refuses most of them. The report
 * prints at the end of the run through x2_interrupt_reports.
 */
void x86_profiler_start(const char *arg);
void x86_profiler_report(void);

/*
 * X2_WRITE_WATCH=<guest-addr>: report the running body the instant any guest
 * WR32 touches the address (the definitive catch for a stack overrun whose
 * writer is a DIRECT call, invisible to the dispatch-boundary ring). WR32
 * checks x2_write_watch_addr; unarmed it is one predictable compare.
 */
void x86_write_watch_arm(const char *arg);
/* Writes the watch saw, so a report can carry its denominator. */
unsigned long x86_write_watch_hits(void);

/*
 * X2_STACKCHECK=<file>: while armed, record every dispatched call's esp delta
 * so tools/stackcheck.py can check it against the callee's own RET immediate.
 * A delta is only wrong relative to an expectation, and that expectation is in
 * the guest binary, not in this runtime.
 */
void x86_stackcheck_arm(int on);
extern volatile uint32_t x2_write_watch_addr;
extern void x2_write_watch_fire(uint32_t a, uint32_t v);

/* Bind an IAT slot to a callable address when the import is implemented
   natively but is not another recompiled module: the guest sometimes takes an
   import's address and calls through it, bypassing the named stub. Returns 0
   if there is no native implementation for that slot. */
uint32_t x86_native_thunk(const char *mod, const char *sym);

/* Dump guest memory named by X2_PEEK (see the definition for the format).
   Safe from a signal handler: reads via process_vm_readv, so an unmapped
   address reports itself instead of faulting again. */
void x86_peek_report(void);

/*
 * Read guest memory WITHOUT dereferencing it: process_vm_readv returns an
 * error for an unmapped address instead of raising a signal. Any diagnostic
 * that follows a guest pointer should use this rather than a range check --
 * the engine allocates from pools that are in neither the guest heap nor a
 * mapped module, so "not in a range I know" and "not readable" are different
 * answers, and only the second is the one that matters.
 */
int x86_peek(uint32_t addr, void *dst, size_t n);
int x86_peek32(uint32_t addr, uint32_t *out);

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

/*
 * A monotonic count of everything the ring records, for the heartbeat: it is
 * the cheapest proof that the guest is still executing. WHAT it counts differs
 * by build -- every body entry and exit in a trace build, only host-boundary
 * crossings otherwise -- so x86_crossings_what() says which, and the heartbeat
 * prints it rather than letting a reader assume.
 */
unsigned long x86_crossings(void);
const char   *x86_crossings_what(void);

/*
 * The raw per-import call probe: the most-called host imports in an interval,
 * descending. The caller allocates `snapshot` with x86_thunk_count() entries,
 * zero-filled once, and keeps it across calls: each call fills `mod/sym/hits`
 * with the top `cap` imports by DELTA since the previous call and updates
 * `snapshot`, so the heartbeat can name the imports behind a slow window.
 */
unsigned int x86_thunk_count(void);
unsigned int x86_thunk_crossings_sorted(unsigned long *snapshot,
                                        const char **mod, const char **sym,
                                        unsigned long *hits, unsigned int cap);

/*
 * The hot-guest-body probe: raw per-entry-point dispatch counts since armed,
 * for naming the guest body a slow window spends its crossings in. X2_HOTEP
 * arms it (see x86_hotep_arm). x86_hotep_sorted returns the top `cap` EPs by
 * count, decoded to module+name by the caller. x86_hotep_collisions counts
 * hash collisions -- non-zero means the table had to refuse new keys and the
 * probe may be missing the true top.
 */
void         x86_hotep_arm(const char *arg);
unsigned int x86_hotep_sorted(uint32_t *ep, unsigned long long *ns,
                              unsigned long *hits, unsigned int cap);
unsigned int x86_hotep_collisions(void);

/* Wall-time split between host import stubs and guest bodies per interval
   (see x86_probe_time_delta). Armed with X2_HOTEP; otherwise zeroes. */
void x86_probe_time_delta(unsigned long long *host_import_ns,
                          unsigned long long *guest_body_ns);

/* Every registered module, for reporting. */
X86Module *x86_modules(void);

/* Say at STARTUP whether X2_ARGS can be honoured by this build. */
void x86_args_build_check(void);

/* X2_EPCOUNT: how often a dispatched body is entered. Reports at zero. */
void x86_epcount_report(void);

#endif /* X86RT_NATIVE_H */
