/* Runtime for statically recompiled x86-32 code.
 *
 * The recompiled DLL is built as a 32-bit PE and loaded into the same address
 * space as the original modules, so guest addresses ARE host addresses and
 * memory access is a direct dereference. That keeps interop with the untouched
 * libIG*.dll trivially correct; it is the reason the PC build was chosen as the
 * recomp target rather than a foreign-ISA console image.
 *
 * Flags are lazy: instructions record their operands, result and operation
 * kind, and condition codes are computed only when a Jcc/SETcc asks. Computing
 * six flags per arithmetic instruction would dominate the emitted code.
 */
#ifndef X86RT_H
#define X86RT_H

#include <setjmp.h>
#include <stdint.h>
#ifdef _WIN32
#include <intrin.h>
#else
/*
 * intrin.h is used for exactly one thing here: the FS/GS accessors below.
 * Off Windows there is no TIB, so FS-relative access is modelled as a flat
 * block the runtime owns -- the same choice the Xbox build makes, and the only
 * honest one once no Windows loader is involved.
 *
 * g_fsbase is deliberately 0 until a native host sets it: FS:[0] is the SEH
 * chain, and silently reading address 0 would be a crash with a misleading
 * cause. x86_fs_check() makes it say so instead.
 */
extern uint32_t g_fsbase, g_gsbase;
void x86_seg_unset(const char *seg);
static inline uint32_t __readfsdword(unsigned long o)
{
    if (!g_fsbase) x86_seg_unset("FS");
    return *(volatile uint32_t *)(uintptr_t)(g_fsbase + (uint32_t)o);
}
static inline void __writefsdword(unsigned long o, uint32_t v)
{
    if (!g_fsbase) x86_seg_unset("FS");
    *(volatile uint32_t *)(uintptr_t)(g_fsbase + (uint32_t)o) = v;
}
static inline uint32_t __readgsdword(unsigned long o)
{
    if (!g_gsbase) x86_seg_unset("GS");
    return *(volatile uint32_t *)(uintptr_t)(g_gsbase + (uint32_t)o);
}
static inline void __writegsdword(unsigned long o, uint32_t v)
{
    if (!g_gsbase) x86_seg_unset("GS");
    *(volatile uint32_t *)(uintptr_t)(g_gsbase + (uint32_t)o) = v;
}

/*
 * CPUID and RDTSC, which the MSVC CRT uses for feature detection and timing.
 *
 * On an x86-64 host the honest answer is the host's own: the recompiled code
 * IS running on this CPU, so reporting its features is not a fake. On a
 * non-x86 host there is no answer to give, and inventing a feature word would
 * make the CRT take a code path the machine cannot execute -- so it stops
 * instead, which is the failure this project would rather have.
 */
#if defined(__i386__) || defined(__x86_64__)
/* Written out rather than taken from <cpuid.h>: glibc defines __cpuid there as
   a five-argument macro, which collides with the MSVC-shaped __cpuid(int[4],
   int) the emitted code calls. */
static inline void __cpuid(int regs[4], int leaf)
{
    __asm__ __volatile__("cpuid"
                         : "=a"(regs[0]), "=b"(regs[1]),
                           "=c"(regs[2]), "=d"(regs[3])
                         : "a"(leaf), "c"(0));
}
static inline uint64_t __rdtsc(void)
{
    uint32_t lo, hi;
    __asm__ __volatile__("rdtsc" : "=a"(lo), "=d"(hi));
    return ((uint64_t)hi << 32) | lo;
}
#else
void x86_no_host_cpuid(const char *what);
static inline void __cpuid(int regs[4], int leaf)
{
    (void)regs; (void)leaf; x86_no_host_cpuid("CPUID");
}
static inline uint64_t __rdtsc(void)
{
    x86_no_host_cpuid("RDTSC");
    return 0;
}
#endif
#endif

enum {
    FK_NONE = 0, FK_ADD, FK_SUB, FK_LOGIC, FK_INC, FK_DEC, FK_SHIFT,
    /* Flags restored from a real EFLAGS word (POPFD) rather than derived from
       an operation. PUSHFD/POPFD appear in CPU-feature detection, which the CRT
       runs at startup, so the lazy model has to be able to round-trip. */
    FK_EXPLICIT
};

typedef struct CPU {
    uint32_t eax, ecx, edx, ebx, esp, ebp, esi, edi;
    /* lazy flag state */
    uint32_t f_a, f_b, f_r;
    int      f_kind;
    int      f_w;        /* operand width in bytes: 1, 2 or 4 */
    long double st[8];   /* x87 register stack */
    int      top;        /* index of ST(0) */
    int      depth;      /* live registers; guards under/overflow */
    uint32_t fsw;        /* status word bits set by FCOM, read by FNSTSW */
    uint32_t fcw;        /* control word; only the rounding-control bits matter */
    uint64_t mm[8];      /* MMX registers, kept SEPARATE from st[] -- see below */
    /* SSE. Present so that packed-logic instructions are translated for real
       rather than special-cased: the engine probes for SSE by executing
       ORPS XMM0,XMM0, and a no-op would be right for that one operand pair and
       wrong for any other. */
    uint64_t xmm[8][2];
} CPU;

/* Runtime base of the ORIGINAL module. Absolute references into the module's
   own image are emitted relative to this, because the DLL is relocated inside
   the game (observed at 0x001C0000 rather than its preferred 0x10000000) and a
   hardcoded address would then read unrelated, still-mapped memory. */
/* Each recompiled module resolves its own absolute references against its own
   base: they are all linked for 0x10000000 and cannot all be there. A module's
   generated .c defines X86_IMGBASE before including this; anything that does
   not (the hosted single-module DLL build) keeps the plain global. */
extern uint32_t g_imgbase;
#ifndef X86_IMGBASE
#define X86_IMGBASE g_imgbase
#endif
extern uint32_t g_image_lo, g_image_hi;   /* guest image bounds; outside = host */
/* Hybrid execution: run ORIGINAL machine code where no recompiled body exists.
   Opt-in, and every distinct fallback address is reported -- a recompilation
   that quietly runs the original is not a recompilation. */
extern int x86_allow_fallback;
void x86_note_fallback(uint32_t target);
/* An import with no native implementation. Names it and stops -- there is
   nothing honest to return. */
void x86_missing_import(const char *mod, const char *sym);
/* Call an import through its (loader-bound) IAT slot; names it if unbound. */
void x86_import_call(CPU *C, uint32_t slot_va, const char *mod, const char *sym);
/* Call a guest function from HOST code: pushes the return address the body's
   RET will pop. Dispatching without it leaks guest stack, upward, silently. */
void x86_guest_call(CPU *C, uint32_t target);

/* ---- setjmp / longjmp --------------------------------------------------
 *
 * These are NOT an import stub, and cannot be: a host longjmp resumes into a
 * frame that must still be alive, and an import stub's frame is dead the
 * moment it returns. The guest's setjmp call site is in the middle of a
 * generated body, so the host setjmp has to be emitted THERE -- which is what
 * recomp.py does when it sees a call to _setjmp3:
 *
 *     ret_push(<return address>);
 *     { int _sj = setjmp(*x86_setjmp_buf(C)); x86_setjmp_done(C, _sj); }
 *
 * x86_setjmp_buf snapshots the guest register file against the guest's own
 * jmp_buf pointer (read from the stack, where _setjmp3's first argument sits)
 * and hands back the host buffer to jump into. x86_setjmp_done finishes the
 * call either way: rc == 0 is the direct return, anything else means a longjmp
 * arrived, so the snapshot is restored and rc becomes the return value.
 *
 * Both are __cdecl, so only the return address is popped.
 */
jmp_buf *x86_setjmp_buf(CPU *C);
void x86_setjmp_done(CPU *C, int rc);
/* Dump the last crossings between guest and host, with ESP on both sides. */
void x86_ring_dump(void);
void x86_untranslated(uint32_t ep, const char *name, const char *reason);
/* A single instruction the translator could not handle, reached at run time.
   The rest of its function IS translated -- see translate() in recomp.py for
   why that is sound -- so this names the exact instruction rather than the
   whole body. */
void x86_unsupported_insn(uint32_t ep, uint32_t addr, const char *name,
                          const char *reason);
/* INT3: the compiler's unreachable trap. Reaching one is a real failure. */
void x86_int3(uint32_t addr);
/* A function body ended without a terminator and the address it falls through
   to is not a known function. */
void x86_fallthrough(uint32_t fn_ep, uint32_t next);
void x86_fallback_report(void);
#define G_IMGBASE (X86_IMGBASE)

/* ---- memory: guest address == host address (see header comment) ---- */
/* FS/GS-relative access. Not modelled: this code runs as a genuine 32-bit PE,
   so FS still addresses the TIB and FS:[0] is the real SEH chain. MSVC emits
   these in the prologue of every function with a try/catch or a destructor --
   825 functions in XMen2.exe -- so faking them would break unwinding wholesale. */
#define SEGRD32(seg, a)     __read##seg##dword((unsigned long)(a))
#define SEGWR32(seg, a, v)  __write##seg##dword((unsigned long)(a), (unsigned long)(v))
#define __readFSdword(o)     __readfsdword(o)
#define __writeFSdword(o, v) __writefsdword((o), (v))
#define __readGSdword(o)     __readgsdword(o)
#define __writeGSdword(o, v) __writegsdword((o), (v))

#define RDF32(a)    ((long double)(*(volatile float  *)(uintptr_t)(a)))
#define RDF64(a)    ((long double)(*(volatile double *)(uintptr_t)(a)))
#define WRF32(a, v) (*(volatile float  *)(uintptr_t)(a) = (float)(v))
#define WRF64(a, v) (*(volatile double *)(uintptr_t)(a) = (double)(v))
#define RDI32(a)    ((long double)(int32_t)RD32(a))

#define RD8(a)      (*(volatile uint8_t  *)(uintptr_t)(a))
#define RD16(a)     (*(volatile uint16_t *)(uintptr_t)(a))
#define RD32(a)     (*(volatile uint32_t *)(uintptr_t)(a))
#define WR8(a, v)   (*(volatile uint8_t  *)(uintptr_t)(a) = (uint8_t)(v))
#define WR16(a, v)  (*(volatile uint16_t *)(uintptr_t)(a) = (uint16_t)(v))
#define RD64(a)     (*(volatile uint64_t *)(uintptr_t)(a))
#define WR64(a, v)  (*(volatile uint64_t *)(uintptr_t)(a) = (uint64_t)(v))
#define WR32(a, v)  (*(volatile uint32_t *)(uintptr_t)(a) = (uint32_t)(v))

/* ---- lazy flags ---- */
#define SETFLAGS(C, kind, a, b, r, w) \
    do { (C)->f_kind = (kind); (C)->f_a = (a); (C)->f_b = (b); \
         (C)->f_r = (r); (C)->f_w = (w); } while (0)

/* INC/DEC leave CF untouched; the recompiler emits this form for them. The
   distinction matters: code does `inc` inside a carry-carrying loop. */
#define SETFLAGS_NC(C, kind, a, b, r, w) SETFLAGS(C, kind, a, b, r, w)
#define SETFLAGS_SHIFT(C, a, c, r, w)    SETFLAGS(C, FK_SHIFT, a, c, r, w)

static inline uint32_t x86_mask(int w)
{
    return w == 1 ? 0xFFU : w == 2 ? 0xFFFFU : 0xFFFFFFFFU;
}

/* Materialise a real EFLAGS word from the lazy state. Bit 1 is always set. */
static inline uint32_t x86_eflags(const CPU *C);

static inline int x86_msb(uint32_t v, int w)
{
    return (int)((v >> (w * 8 - 1)) & 1U);
}

static inline int FLAG_Z(const CPU *C)
{
    if (C->f_kind == FK_EXPLICIT)
        return (int)((C->f_a >> 6) & 1U);
    return (C->f_r & x86_mask(C->f_w)) == 0;
}

static inline int FLAG_S(const CPU *C)
{
    if (C->f_kind == FK_EXPLICIT)
        return (int)((C->f_a >> 7) & 1U);
    return x86_msb(C->f_r, C->f_w);
}

/*
 * ADC and SBB need the carry IN as well as the two operands, which the lazy
 * triple (a, b, r) cannot express. Modelling `a - b - c` as a SUB of `b - c`
 * gets the borrow wrong whenever a != b, and modelling it as a SUB of `b + c`
 * is wrong when b + c wraps. The first of those shipped, and it made MSVC's
 * sign idiom
 *
 *     sbb eax, eax        ; eax = CF ? -1 : 0, and CF_out = CF_in
 *     sbb eax, -1         ; -> -1 (less) or +1 (greater)
 *
 * yield 0 -- "equal" -- whenever CF was set and eax was non-zero, because the
 * first SBB reported CF_out = 0. Every binary search over a string table then
 * took a mismatch for a hit: the engine's string pool interned "_refCount" and
 * handed back "igObject", so ARK field names were never bound (issue #16).
 *
 * So these compute the real flags once, at the instruction, and hand back an
 * EFLAGS word for FK_EXPLICIT. Correctness here is cheap; getting it wrong is
 * invisible until something reads CF three instructions later.
 */
static inline uint32_t x86_flags_adc(uint32_t a, uint32_t b, uint32_t c,
                                     uint32_t r, int w)
{
    uint32_t m = x86_mask(w), f = 0;
    uint64_t full = (uint64_t)(a & m) + (uint64_t)(b & m) + (uint64_t)c;
    a &= m; b &= m; r &= m;
    if ((full >> (w * 8)) & 1U)                 f |= 1U << 0;   /* CF */
    if (r == 0)                                 f |= 1U << 6;   /* ZF */
    if (x86_msb(r, w))                          f |= 1U << 7;   /* SF */
    if (((~(a ^ b) & (a ^ r)) >> (w * 8 - 1)) & 1U) f |= 1U << 11; /* OF */
    return f;
}

static inline uint32_t x86_flags_sbb(uint32_t a, uint32_t b, uint32_t c,
                                     uint32_t r, int w)
{
    uint32_t m = x86_mask(w), f = 0;
    uint64_t full = (uint64_t)(a & m) - (uint64_t)(b & m) - (uint64_t)c;
    a &= m; b &= m; r &= m;
    if ((full >> (w * 8)) & 1U)                 f |= 1U << 0;   /* CF (borrow) */
    if (r == 0)                                 f |= 1U << 6;   /* ZF */
    if (x86_msb(r, w))                          f |= 1U << 7;   /* SF */
    if ((((a ^ b) & (a ^ r)) >> (w * 8 - 1)) & 1U) f |= 1U << 11; /* OF */
    return f;
}

static inline int FLAG_C(const CPU *C)
{
    if (C->f_kind == FK_EXPLICIT)
        return (int)((C->f_a >> 0) & 1U);
    uint32_t m = x86_mask(C->f_w);
    switch (C->f_kind) {
    case FK_ADD:   return (C->f_r & m) < (C->f_a & m);
    case FK_SUB:   return (C->f_a & m) < (C->f_b & m);
    case FK_SHIFT: return (int)((C->f_a >> (C->f_b - 1)) & 1U);
    default:       return 0;              /* logic ops clear CF; INC/DEC keep it */
    }
}

static inline int FLAG_O(const CPU *C)
{
    if (C->f_kind == FK_EXPLICIT)
        return (int)((C->f_a >> 11) & 1U);
    int sa = x86_msb(C->f_a, C->f_w);
    int sb = x86_msb(C->f_b, C->f_w);
    int sr = x86_msb(C->f_r, C->f_w);
    switch (C->f_kind) {
    case FK_ADD: return (sa == sb) && (sr != sa);
    case FK_SUB: return (sa != sb) && (sr != sa);
    case FK_INC: return (C->f_r & x86_mask(C->f_w))
                        == (0x80000000U >> ((4 - C->f_w) * 8));
    case FK_DEC: return (C->f_a & x86_mask(C->f_w))
                        == (0x80000000U >> ((4 - C->f_w) * 8));
    default:     return 0;
    }
}

static inline int FLAG_P(const CPU *C)
{
    if (C->f_kind == FK_EXPLICIT)
        return (int)((C->f_a >> 2) & 1U);
    uint32_t v = C->f_r & 0xFFU;
    v ^= v >> 4; v ^= v >> 2; v ^= v >> 1;
    return (int)(~v & 1U);
}

static inline uint32_t x86_eflags(const CPU *C)
{
    uint32_t f = 0x00000202U;          /* bit 1 reserved-1, IF set */
    if (FLAG_C(C)) f |= 1U << 0;
    if (FLAG_P(C)) f |= 1U << 2;
    if (FLAG_Z(C)) f |= 1U << 6;
    if (FLAG_S(C)) f |= 1U << 7;
    if (FLAG_O(C)) f |= 1U << 11;
    return f;
}

/* ---- x87 ----
 * A modelled register stack. `long double` is 80-bit on x86 with GCC, matching
 * the hardware's internal precision, so intermediate results round the same way
 * as the original -- using `double` here would diverge on long expressions in a
 * way no single-instruction test would reveal.
 * Stack faults are hard errors: silently wrapping TOP would turn a translation
 * bug into slightly-wrong arithmetic instead of a stop.
 */
#define X87_ST(C, i)  ((C)->st[((C)->top + (i)) & 7])

void x87_fault(const char *what);

/* Call history. Without it, "x86_dispatch: no function at 0xX" says nothing
   about how execution got there, and 11,061 functions is far too many to
   bisect by hand. Compiled out entirely unless X86_TRACE_CALLS is defined, so
   the shipping build pays nothing. */
#ifdef X86_TRACE_CALLS
# define X86_HIST 64
extern uint32_t x86_hist[X86_HIST];
extern unsigned x86_hist_n;
extern volatile unsigned long x86_fn_calls;
# define X86_ENTER_FN(a) (x86_hist[x86_hist_n++ & (X86_HIST - 1)] = (a), \
                          x86_fn_calls++)
void x86_dump_history(void);
#elif defined(X86_WATCH)
/* Entry-point watch (src/x86watch.c). The macro is expanded inside a
   recompiled body, where the CPU pointer is always the parameter `C`, so the
   register state reaches the watch without changing what the generator emits
   -- the generated code stays byte-identical between a watch build and a
   shipping one, which is the property that makes a watch build's evidence
   apply to the shipping build. */
void x86_watch_enter(uint32_t ep, const CPU *C);
void x86_watch_exit(uint32_t ep, const CPU *C);
void x86_watch_selftest(void);
/* Crash reporter (src/x86fault.c): names the module a fault EIP falls in and
   dumps the annotated stack, which is the only way to see who transferred
   control once execution has left recompiled code for a host callee. */
void x86_fault_install(void);
/* Ring of recompiled/host boundary crossings, dumped by the fault reporter.
   kind: 0 entered a body, 1 called host, 2 host returned, 3 body returned. */
void x86_watch_note(int kind, uint32_t a, uint32_t b);
/* Reports where a recompiled body's C frame sits relative to the guest stack
   pointer it was entered with -- the fact that decides whether a guest PUSH
   overwrites live C state. */
void x86_watch_stack(uint32_t ep, uint32_t guest_esp, const void *cpu, unsigned long cpu_size);
# define X86_ENTER_FN(a) x86_watch_enter((a), C)
# define X86_EXIT_FN(a)  x86_watch_exit((a), C)
# define x86_dump_history() ((void)0)
#elif defined(X86_NATIVE_TRACE)
/* Native build, tracing every recompiled body.
 *
 * The boundary ring only sees DISPATCHED calls, because a guest-to-guest call
 * inside a module is emitted as a direct C call and never touches the
 * dispatcher. That is most of them -- which is why a stack imbalance in an
 * ordinary function left no trace at all. This makes every entry and exit
 * visible, at the cost of a ring write per call, so it is a build option
 * rather than the default.
 *
 * The hook is passed the module's own base as well as the entry point, because
 * generated code knows only its LINKED ep and the ring has to be able to tell
 * which module that ep belongs to -- see the ring in x86rt_native.c. */
void x86_trace_enter(uint32_t ep, uint32_t base, const CPU *C);
void x86_trace_exit(uint32_t ep, uint32_t base, const CPU *C);
# define X86_ENTER_FN(a) x86_trace_enter((a), X86_IMGBASE, C)
# define X86_EXIT_FN(a)  x86_trace_exit((a), X86_IMGBASE, C)
# define x86_dump_history() ((void)0)
#elif defined(X86_NATIVE_REACHED)
/* Native build, recording WHICH bodies were ever entered.
 *
 * A different question from the trace ring, and the ring cannot answer it: the
 * ring holds the last N crossings, so a function called once during startup is
 * evicted long before the failure and its absence from the ring means nothing.
 * "Was 0x1003d900 ever reached?" is exactly the question issue #14 turns on,
 * and answering it from a ring would have been a guess dressed as evidence.
 *
 * So this keeps a SET, not a history: entered-or-not for every entry point,
 * with no eviction, reported with the total distinct count as its denominator
 * so that NEVER is distinguishable from "the instrument never ran".
 *
 * Keyed on (entry point, MODULE BASE), not the entry point alone. Every
 * libIG*.dll is linked for 0x10000000, so the same linked address exists in
 * several of them and a set keyed on it alone reports one module's count under
 * another's name -- observed: FUN_1006a500 came back as a single x52 spanning
 * libIGCore and libIGSg. X86_IMGBASE is this module's own runtime base and is
 * already defined in every generated translation unit, so the pair is free. */
void x86_reached_enter(uint32_t ep, uint32_t base);
void x86_reached_report(void);
# define X86_ENTER_FN(a) x86_reached_enter((a), X86_IMGBASE)
# define x86_dump_history() ((void)0)
#else
# define X86_ENTER_FN(a) ((void)0)
# define x86_dump_history() ((void)0)
#endif
#ifndef X86_EXIT_FN
# define X86_EXIT_FN(a) ((void)0)
#endif
/* call/jump into the region with no identified function; aborts by address */
void x86_call_unknown(CPU *C, uint32_t target);
/* RET popped an address other than the one the function was entered with */
/* A RET popped something other than the value the function was entered with.
   Carries the function's own entry point and that expected value, because
   "which function's epilogue disagreed with its prologue" is the question, and
   the popped address alone cannot answer it. */
void x86_return_to(CPU *C, uint32_t target, uint32_t fn_ep, uint32_t expected);

/* FIST/FISTP round per the control word's RC bits (11:10). MSVC's float->int
   cast sets RC=truncate, does the store, then restores -- so treating FLDCW as
   a no-op and always truncating would be right for that idiom and WRONG for
   everything else. Model it rather than assume the common case. */
static inline int32_t x87_to_int(const CPU *C, long double v)
{
    switch ((C->fcw >> 10) & 3) {
    case 1:  return (int32_t)__builtin_floorl(v);          /* round down */
    case 2:  return (int32_t)__builtin_ceill(v);           /* round up   */
    case 3:  return (int32_t)v;                            /* truncate   */
    default: return (int32_t)__builtin_rintl(v);           /* to nearest */
    }
}

/* On real hardware MMX registers ALIAS the x87 stack, which is why EMMS exists.
   Modelling them separately is only safe because code that mixes the two
   without an intervening EMMS is malformed; EMMS is therefore a no-op here. If
   a module is ever found interleaving them, this must become an error. */

static inline void x87_push(CPU *C, long double v)
{
    C->top = (C->top - 1) & 7;
    if (++C->depth > 8) x87_fault("x87 stack overflow");
    C->st[C->top] = v;
}

static inline long double x87_pop(CPU *C)
{
    long double v;
    if (--C->depth < 0) x87_fault("x87 stack underflow");
    v = C->st[C->top];
    C->top = (C->top + 1) & 7;
    return v;
}

/* C3/C2/C0 of the status word, as FNSTSW AX delivers them. */
static inline void x87_cmp(CPU *C, long double a, long double b)
{
    C->fsw = (a > b) ? 0x0000U : (a < b) ? 0x0100U : (a == b) ? 0x4000U : 0x4500U;
}

/* ---- indirect dispatch ----
 * Virtual calls and function pointers resolve at runtime. An address with no
 * recompiled body is a hard error, never a silent no-op: continuing past an
 * unresolved call produces a plausible-looking run that is wrong.
 */
void x86_dispatch(CPU *C, uint32_t target);
/* called when an indirect target has no recompiled body; aborts by default */
void x86_dispatch_miss(uint32_t target);

/* A function the recompiler could not translate. Emitted as its whole body so
   the module links; reaching one aborts naming the function and the exact
   instruction form that blocked it, which is the work item. */
void x86_untranslated(uint32_t ep, const char *name, const char *reason);
/* INT3: the compiler's unreachable trap. Reaching one is a real failure. */
void x86_int3(uint32_t addr);
/* A function body ended without a terminator and the address it falls through
   to is not a known function. */
void x86_fallthrough(uint32_t fn_ep, uint32_t next);

/* real code -> recompiled body; returns EAX */
/* Entry from host code into a recompiled body. Reached by `jmp` from a naked
   export shim with the guest's own ESP still in place -- see x86_enter_tramp in
   the generated runtime for why nothing may be pushed before the switch. */
void     x86_enter_tramp(void);
uint32_t x86_enter_body(uint32_t ep, uint32_t guest_esp, uint32_t ecx);
/* The runtime's private per-thread stack. Everything below a recompiled
   function's entry esp belongs to the GUEST -- guest pushes descend into it and
   host callees run their frames there -- so the runtime's own C frames cannot
   live there too. init in DLL_PROCESS_ATTACH, free in DLL_THREAD_DETACH. */
int  x86_rt_stack_init(void);
void x86_rt_stack_free(void);
uint32_t x86_rt_stack_take(void);
void x86_rt_stack_give(void);
/* recompiled body -> real code, running on the guest stack */
void x86_call_host(CPU *C, void *fn, const char *what);
#define DISPATCH(C, t) x86_dispatch((C), (uint32_t)(t))

#endif /* X86RT_H */
