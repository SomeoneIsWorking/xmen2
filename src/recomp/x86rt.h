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
#include <string.h>
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
/*
 * PER-THREAD. FS:[0] is the SEH chain head and every recompiled prologue
 * writes it, so two guest threads sharing one base would overwrite each
 * other's exception chain -- see src/native/threads.c.
 */
extern __thread uint32_t g_fsbase, g_gsbase;
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
    uint32_t fcw;        /* control word: rounding control AND the exception
                            masks. Both are read by guest code -- see
                            X87_CW_INIT and cpu_reset() below. */
    uint64_t mm[8];      /* MMX registers, kept SEPARATE from st[] -- see below */
    /* SSE. Present so that packed-logic instructions are translated for real
       rather than special-cased: the engine probes for SSE by executing
       ORPS XMM0,XMM0, and a no-op would be right for that one operand pair and
       wrong for any other. */
    uint64_t xmm[8][2];
    /* Logical generated-call nesting and the hosted tail-dispatch frame. */
    uint32_t call_depth;
    uint32_t dispatch_depth;
    uint32_t tail_target;
    /* Enter the next body at a LABEL rather than at its entry point.
     *
     * MSVC shares one epilogue between paths, so a JMP lands in the middle of
     * another function -- 28 such targets in XMen2.exe, the worked example
     * being 0x0066cf3c inside FUN_0066ced2, reached by the switch in the block
     * after it (issue #29). The target has no function name, so before this it
     * became x86_call_unknown and stopped the run.
     *
     * Set by the jumping body (or by a dispatch shim) to the MAPPED address to
     * resume at; the entered body consumes it -- CLEARING it -- and jumps
     * through the same offset switch its computed jumps use. Cleared on
     * consumption because a value left set would send the next ordinary call
     * to that label and silently skip the prologue. */
    uint32_t enter_at;
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
/* Preferred base recorded by the hosted runtime generator. Diagnostics use
   this to translate mapped addresses back to disassembly addresses. */
extern uint32_t g_guest_preferred_base;
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
/*
 * The buffer table gives slots back: a jmp_buf living in a guest heap block
 * that has been freed can no longer be jumped to. That is the ONLY rule -- see
 * crt.c for the second one the real run refuted. x86_setjmp_reclaim returns how
 * many it freed, so "reclaimed nothing" and "reclaimed everything" are
 * distinguishable; x86_setjmp_live is how many are held.
 */
int x86_setjmp_reclaim(void);
int x86_setjmp_live(void);
/* Dump the last crossings between guest and host, with ESP on both sides. */
void x86_ring_dump(void);
void x86_untranslated(uint32_t ep, const char *name, const char *reason);
/* A single instruction the translator could not handle, reached at run time.
   The rest of its function IS translated -- see translate() in recomp.py for
   why that is sound -- so this names the exact instruction rather than the
   whole body. */
void x86_unsupported_insn(uint32_t ep, uint32_t addr, const char *name,
                          const char *reason);
/*
 * REGION RECORDING -- capture what a stretch of guest code actually does.
 *
 * Some blocks cannot be ported from the disassembly alone: MSVC leaves floats
 * on the x87 stack across intervening pushes, and which helper consumes which
 * is not visible in the listing. Declining to port such a block leaves the
 * translated original in place forever; guessing at it ships a defect. So the
 * third option is built here -- RUN it and write down exactly what happened,
 * one line per instruction, with the registers, the x87 stack and the top of
 * the guest stack.
 *
 * `recomp.py emit --record LO-HI` (repeatable) inserts the call before each
 * instruction in the range and NOWHERE else, so a build with no ranges pays
 * nothing. The runtime announces the compiled-in ranges at startup and reports
 * at zero, because "the block never executed" and "recording was not compiled
 * in" are the two answers that must not look alike. `X2_RECORD_ARM=<guest
 * address>` ignores those calls until the named recorded instruction runs, so
 * a shared helper can be correlated to its caller rather than filling the ring
 * earlier in the run.
 */
void x86_record(uint32_t addr, const struct CPU *C, const char *text);
extern int x86_record_on;
#define X86_RECORD(a, C, t) \
    do { if (x86_record_on) x86_record((a), (C), (t)); } while (0)

/* INT3: the compiler's unreachable trap. Reaching one is a real failure. */
void x86_int3(uint32_t addr);
/* A function body ended without a terminator and the address it falls through
   to is not a known function. */
void x86_fallthrough(uint32_t fn_ep, uint32_t next);
/* The body ends at a call to a function that never returns; getting past it
   means this port's implementation of that callee came back. */
void x86_after_noreturn(uint32_t fn_ep, const char *callee);
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
/*
 * The integer x87 loads, ONE PER WIDTH.
 *
 * FILD takes m16, m32 or m64 and there is no default: for a whole session
 * every FILD was translated as RDI32, so `FILD qword ptr` read the LOW dword
 * of a 64-bit value and sign-extended it. The engine's frame timer is a 64-bit
 * nanosecond count, so its value wrapped negative at 2^31 ns -- 2.147 seconds
 * of run time -- and the frame limiter at XMen2.exe 0x00401ff0 spun forever
 * waiting for a clock that had gone backwards (issue #35).
 */
#define RDI16(a)    ((long double)(int16_t)RD16(a))
#define RDI32(a)    ((long double)(int32_t)RD32(a))
#define RDI64(a)    ((long double)(int64_t)RD64(a))

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

/* ---- rotates ----
 *
 * ROL/ROR/RCL/RCR. They are here rather than inline in the generated code
 * because RCL and RCR rotate a quantity one bit WIDER than the operand -- the
 * carry flag is part of it -- and that is not expressible as a shift pair.
 * MSVC's 64-bit division helpers (__allrem, __aulldiv) halve a 64-bit value
 * with `shr edx,1; rcr eax,1`, so this is on the path of any guest that
 * divides a long long.
 *
 * Flags: a rotate writes ONLY CF and OF; every other flag keeps its value,
 * which is why these take and return a whole EFLAGS word instead of feeding
 * the lazy-flag model. A masked count of zero changes nothing AT ALL,
 * including the flags -- that is architectural, not an optimisation.
 *
 * OF is architecturally UNDEFINED for a count other than 1. This host defines
 * it by the count-1 rule in every case, which is a choice; it is stated here
 * rather than left for a reader to discover, and no guest may rely on it
 * because no real CPU guarantees it either.
 */
#define X86_ROL 0
#define X86_ROR 1
#define X86_RCL 2
#define X86_RCR 3

static inline uint32_t x86_rotate(uint32_t v, uint32_t cnt, int w, int kind,
                                  uint32_t *fl)
{
    unsigned bits = (unsigned)w * 8u;
    uint32_t mask = x86_mask(w);
    uint32_t a = v & mask, r;
    uint32_t cf = *fl & 1u, cf_in = cf, of;
    unsigned n = cnt & 31u;              /* 386 and later mask the count */

    /*
     * A count of zero AFTER the 5-bit mask does nothing at all -- not even to
     * the flags. A count that is a nonzero MULTIPLE of the width is a
     * different case and the difference is measurable: `rol al, 8` leaves the
     * value alone but still writes CF from the result, because the SDM's CF
     * assignment sits outside the rotate loop. A model that returned early on
     * "the value will not change" got that wrong on the host CPU.
     *
     * The through-carry forms rotate a quantity one bit wider, so their count
     * is taken modulo width+1 -- and there a modded count of zero DOES leave
     * the flags alone, because RCL/RCR assign CF inside the loop.
     */
    if (!n) return a;
    if (kind == X86_RCL || kind == X86_RCR) {
        n %= bits + 1u;
        if (!n) return a;
    } else {
        n %= bits;
    }

    if (kind == X86_ROL) {
        r = n ? (((a << n) | (a >> (bits - n))) & mask) : a;
        cf = r & 1u;
        of = ((r >> (bits - 1)) & 1u) ^ cf;
    } else if (kind == X86_ROR) {
        r = n ? (((a >> n) | (a << (bits - n))) & mask) : a;
        cf = (r >> (bits - 1)) & 1u;
        of = ((r >> (bits - 1)) & 1u) ^ ((r >> (bits - 2)) & 1u);
    } else {
        unsigned i;
        r = a;
        for (i = 0; i < n; i++) {
            if (kind == X86_RCL) {
                uint32_t top = (r >> (bits - 1)) & 1u;
                r = ((r << 1) | cf) & mask;
                cf = top;
            } else {
                uint32_t bot = r & 1u;
                r = ((r >> 1) | (cf << (bits - 1))) & mask;
                cf = bot;
            }
        }
        of = (kind == X86_RCL)
                 ? (((r >> (bits - 1)) & 1u) ^ cf)
                 /* RCR's OF is taken from the operand BEFORE the rotate. */
                 : (((a >> (bits - 1)) & 1u) ^ cf_in);
    }
    *fl = (*fl & ~((1u << 0) | (1u << 11))) | cf | (of << 11);
    return r;
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

/*
 * The x87 control word a real FPU powers up with, and what FINIT restores.
 *
 * 0x037F is: every exception MASKED (bits 0-5), precision control 3 (extended,
 * bits 8-9), rounding to nearest (bits 10-11 = 0).
 *
 * The mask bits are the part that was missing. A fresh CPU here is memset to
 * zero, which meant fcw = 0 -- every FP exception UNMASKED, the opposite of the
 * hardware default. Nothing noticed while only the rounding bits were read
 * (they are 0 either way), and then cg.dll's statically-linked CRT read the
 * control word, correctly concluded that inexact results were unmasked, and
 * raised EXCEPTION_FLT_INEXACT_RESULT (0xC000008F) on the first rounding
 * conversion it did. Which was right: it was told they were unmasked.
 */
#define X87_CW_INIT 0x037FU

/*
 * A CPU as the hardware hands it over: zeroed, except for the state that is
 * NOT zero at power-on. Every fresh guest context has to go through this --
 * a bare memset leaves the FPU claiming a configuration no real one has.
 */
static inline void cpu_reset(struct CPU *C)
{
    memset(C, 0, sizeof *C);
    C->fcw = X87_CW_INIT;
}

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
# define X86_ENTER_FN_DIAG(a) (x86_hist[x86_hist_n++ & (X86_HIST - 1)] = (a), \
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
# define X86_ENTER_FN_DIAG(a) x86_watch_enter((a), C)
# define X86_EXIT_FN_DIAG(a)  x86_watch_exit((a), C)
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
# define X86_ENTER_FN_DIAG(a) x86_trace_enter((a), X86_IMGBASE, C)
# define X86_EXIT_FN_DIAG(a)  x86_trace_exit((a), X86_IMGBASE, C)
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
# define X86_ENTER_FN_DIAG(a) x86_reached_enter((a), X86_IMGBASE)
# define x86_dump_history() ((void)0)
#else
# define X86_ENTER_FN_DIAG(a) ((void)0)
# define x86_dump_history() ((void)0)
#endif
#ifndef X86_EXIT_FN_DIAG
# define X86_EXIT_FN_DIAG(a) ((void)0)
#endif

/*
 * THE PREEMPTION POINT, in every recompiled body, in every build.
 *
 * It has to be here and not at the dispatch boundary, and that was measured
 * rather than reasoned: the boundary only sees DISPATCHED calls, and a
 * guest-to-guest call inside a module is emitted as a direct C call that never
 * reaches it. libCriMovie's decoder spins on exactly such calls -- FUN_10002e80
 * and FUN_100085e0, round and round -- so a quantum counted at the boundary
 * never fired for it. With guest threads as coroutines (src/native/threads.c)
 * nothing else can take the CPU away from a thread that does that, and the run
 * stalled with the main thread reading "runnable, waiting its turn for 132.1s"
 * while the decoder ran without interruption.
 *
 * The cost is one decrement and a not-taken branch per call. The budget is a
 * plain global rather than per-thread on purpose: it measures WORK DONE, and
 * re-arming it at each preemption is what makes the quantum mean "this many
 * bodies between switches" no matter which thread ran them.
 *
 * In a build with no scheduler (the Wine/DLL path) x86_preempt_now just
 * re-arms, so the hook costs the same and does nothing.
 */
extern unsigned long x86_preempt_budget;
void x86_preempt_now(void);
#define X86_ENTER_FN(a) \
    do { if (--x86_preempt_budget == 0) x86_preempt_now(); \
         C->call_depth++; \
         X86_ENTER_FN_DIAG(a); } while (0)
#define X86_EXIT_FN(a) \
    do { X86_EXIT_FN_DIAG(a); C->call_depth--; } while (0)
#define X86_TAIL_FN(a) \
    do { (void)(a); C->call_depth--; } while (0)
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
static inline int64_t x87_to_i64(const CPU *C, long double v)
{
    switch ((C->fcw >> 10) & 3) {
    case 1:  return (int64_t)__builtin_floorl(v);          /* round down */
    case 2:  return (int64_t)__builtin_ceill(v);           /* round up   */
    case 3:  return (int64_t)v;                            /* truncate   */
    default: return (int64_t)__builtin_rintl(v);           /* to nearest */
    }
}

/* The narrow stores. Rounding happens at full width and the result is then
   narrowed, which is what the hardware does; a store whose value does not fit
   is a different question (the hardware writes the integer indefinite) and is
   not modelled here. */
static inline int32_t x87_to_int(const CPU *C, long double v)
{
    return (int32_t)x87_to_i64(C, v);
}

/* On real hardware MMX registers ALIAS the x87 stack, which is why EMMS exists.
   Modelling them separately is only safe because code that mixes the two
   without an intervening EMMS is malformed; EMMS is therefore a no-op here. If
   a module is ever found interleaving them, this must become an error. */

/*
 * MMX, as lane arithmetic over a uint64_t.
 *
 * libCriMovie's video decoder is an ordinary MMX IDCT and motion-compensation
 * kernel -- 1106 instructions across 13 functions, all of them lane ops on the
 * eight 64-bit registers. Each one is written out per lane rather than reached
 * for with a host intrinsic: the host is x86-64 and WOULD have the instruction,
 * but the guest's semantics (which way a shift saturates, whether a pack is
 * signed or unsigned, what a shift count above the lane width does) are the
 * thing being reproduced, and an intrinsic that differs in one of those is a
 * silent difference in decoded video.
 *
 * The counts are the guest's: a shift count is the WHOLE 64-bit source, and a
 * count at or above the lane width produces 0 for a logical shift and a copy
 * of the sign bit for an arithmetic one. That is not an edge case here -- MPEG
 * IDCT code shifts by variable amounts.
 */
#define MMX_LANES(w) (64 / (w))

static inline uint64_t mmx_lane_get(uint64_t v, int i, int w)
{
    return (v >> (i * w)) & (w == 64 ? ~0ULL : ((1ULL << w) - 1ULL));
}
static inline uint64_t mmx_lane_put(uint64_t acc, int i, int w, uint64_t v)
{
    uint64_t mask = (w == 64 ? ~0ULL : ((1ULL << w) - 1ULL));
    return (acc & ~(mask << (i * w))) | ((v & mask) << (i * w));
}

/* Signed view of a lane, sign-extended to int64_t. */
static inline int64_t mmx_lane_s(uint64_t v, int i, int w)
{
    uint64_t u = mmx_lane_get(v, i, w);
    uint64_t sign = 1ULL << (w - 1);
    return (int64_t)((u ^ sign) - sign);
}

#define MMX_BINOP(name, w, expr)                                        \
static inline uint64_t name(uint64_t a, uint64_t b)                     \
{                                                                       \
    uint64_t r = 0; int i;                                              \
    for (i = 0; i < MMX_LANES(w); i++) {                                \
        int64_t x = mmx_lane_s(a, i, w), y = mmx_lane_s(b, i, w);       \
        uint64_t ux = mmx_lane_get(a, i, w), uy = mmx_lane_get(b, i, w);\
        (void)x; (void)y; (void)ux; (void)uy;                           \
        r = mmx_lane_put(r, i, w, (uint64_t)(expr));                    \
    }                                                                   \
    return r;                                                           \
}

MMX_BINOP(mmx_paddb,  8,  ux + uy)
MMX_BINOP(mmx_paddw, 16,  ux + uy)
MMX_BINOP(mmx_paddd, 32,  ux + uy)
MMX_BINOP(mmx_psubb,  8,  ux - uy)
MMX_BINOP(mmx_psubw, 16,  ux - uy)
MMX_BINOP(mmx_psubd, 32,  ux - uy)
/* Unsigned saturating: clamp at the lane's maximum, never wrap. */
MMX_BINOP(mmx_paddusb, 8,  (ux + uy) > 0xFFULL   ? 0xFFULL   : ux + uy)
MMX_BINOP(mmx_paddusw, 16, (ux + uy) > 0xFFFFULL ? 0xFFFFULL : ux + uy)
MMX_BINOP(mmx_psubusb, 8,  ux > uy ? ux - uy : 0)
MMX_BINOP(mmx_psubusw, 16, ux > uy ? ux - uy : 0)
/* Signed saturating. */
MMX_BINOP(mmx_paddsb,  8,  (x + y) >  127 ?  127 : (x + y) <  -128 ?  -128 : x + y)
MMX_BINOP(mmx_paddsw, 16,  (x + y) >  32767 ? 32767 : (x + y) < -32768 ? -32768 : x + y)
MMX_BINOP(mmx_psubsb,  8,  (x - y) >  127 ?  127 : (x - y) <  -128 ?  -128 : x - y)
MMX_BINOP(mmx_psubsw, 16,  (x - y) >  32767 ? 32767 : (x - y) < -32768 ? -32768 : x - y)
/* PMULLW keeps the low half of a 16x16 product, PMULHW the high half; the
   product is SIGNED in both (PMULHUW is the unsigned one and is separate). */
MMX_BINOP(mmx_pmullw, 16, (uint64_t)(x * y))
MMX_BINOP(mmx_pmulhw, 16, (uint64_t)((x * y) >> 16))
MMX_BINOP(mmx_pmulhuw, 16, (uint64_t)((ux * uy) >> 16))
/* Rounding average, which is why the +1 is there and not a detail. */
MMX_BINOP(mmx_pavgb,  8,  (ux + uy + 1) >> 1)
MMX_BINOP(mmx_pavgw, 16,  (ux + uy + 1) >> 1)
/* Compare-equal / greater-than produce an all-ones or all-zero MASK per lane. */
MMX_BINOP(mmx_pcmpeqb,  8,  ux == uy ? 0xFFULL : 0)
MMX_BINOP(mmx_pcmpeqw, 16,  ux == uy ? 0xFFFFULL : 0)
MMX_BINOP(mmx_pcmpeqd, 32,  ux == uy ? 0xFFFFFFFFULL : 0)
MMX_BINOP(mmx_pcmpgtb,  8,  x > y ? 0xFFULL : 0)
MMX_BINOP(mmx_pcmpgtw, 16,  x > y ? 0xFFFFULL : 0)
MMX_BINOP(mmx_pcmpgtd, 32,  x > y ? 0xFFFFFFFFULL : 0)
MMX_BINOP(mmx_pminub,  8,  ux < uy ? ux : uy)
MMX_BINOP(mmx_pmaxub,  8,  ux > uy ? ux : uy)
MMX_BINOP(mmx_pminsw, 16,  x < y ? (uint64_t)x : (uint64_t)y)
MMX_BINOP(mmx_pmaxsw, 16,  x > y ? (uint64_t)x : (uint64_t)y)

/* Shifts. The count is the WHOLE source operand, and a count at or past the
   lane width is defined: zero for a logical shift, the sign bit replicated for
   an arithmetic one. Getting that wrong shows up only on the rare large
   count, which is the worst kind of difference to chase. */
#define MMX_SHIFT(name, w, kind)                                        \
static inline uint64_t name(uint64_t a, uint64_t cnt)                   \
{                                                                       \
    uint64_t r = 0; int i;                                              \
    for (i = 0; i < MMX_LANES(w); i++) {                                \
        uint64_t u = mmx_lane_get(a, i, w);                             \
        int64_t  sv = mmx_lane_s(a, i, w);                              \
        uint64_t o;                                                     \
        if (kind == 2) o = (uint64_t)(cnt >= (uint64_t)(w) ? (sv < 0 ? -1 : 0) \
                                                          : (sv >> cnt));     \
        else if (kind == 1) o = cnt >= (uint64_t)(w) ? 0 : (u >> cnt);   \
        else               o = cnt >= (uint64_t)(w) ? 0 : (u << cnt);   \
        (void)sv;                                                       \
        r = mmx_lane_put(r, i, w, o);                                   \
    }                                                                   \
    return r;                                                           \
}
MMX_SHIFT(mmx_psllw, 16, 0)
MMX_SHIFT(mmx_pslld, 32, 0)
MMX_SHIFT(mmx_psrlw, 16, 1)
MMX_SHIFT(mmx_psrld, 32, 1)
MMX_SHIFT(mmx_psraw, 16, 2)
MMX_SHIFT(mmx_psrad, 32, 2)
static inline uint64_t mmx_psllq(uint64_t a, uint64_t c)
{ return c >= 64 ? 0 : (a << c); }
static inline uint64_t mmx_psrlq(uint64_t a, uint64_t c)
{ return c >= 64 ? 0 : (a >> c); }

/* Packs: the DESTINATION supplies the low half of the result and the source
   the high half, and both saturate. PACKUSWB saturates to UNSIGNED bytes even
   though its input is signed words -- which is exactly what makes it the last
   step of an IDCT, and exactly what an unsigned-in reading would get wrong. */
static inline uint64_t mmx_packuswb(uint64_t d, uint64_t s)
{
    uint64_t r = 0; int i;
    for (i = 0; i < 4; i++) {
        int64_t v = mmx_lane_s(d, i, 16);
        r = mmx_lane_put(r, i, 8, (uint64_t)(v < 0 ? 0 : v > 255 ? 255 : v));
    }
    for (i = 0; i < 4; i++) {
        int64_t v = mmx_lane_s(s, i, 16);
        r = mmx_lane_put(r, 4 + i, 8, (uint64_t)(v < 0 ? 0 : v > 255 ? 255 : v));
    }
    return r;
}
static inline uint64_t mmx_packsswb(uint64_t d, uint64_t s)
{
    uint64_t r = 0; int i;
    for (i = 0; i < 4; i++) {
        int64_t v = mmx_lane_s(d, i, 16);
        r = mmx_lane_put(r, i, 8, (uint64_t)(v < -128 ? -128 : v > 127 ? 127 : v));
    }
    for (i = 0; i < 4; i++) {
        int64_t v = mmx_lane_s(s, i, 16);
        r = mmx_lane_put(r, 4 + i, 8, (uint64_t)(v < -128 ? -128 : v > 127 ? 127 : v));
    }
    return r;
}
static inline uint64_t mmx_packssdw(uint64_t d, uint64_t s)
{
    uint64_t r = 0; int i;
    for (i = 0; i < 2; i++) {
        int64_t v = mmx_lane_s(d, i, 32);
        r = mmx_lane_put(r, i, 16, (uint64_t)(v < -32768 ? -32768 : v > 32767 ? 32767 : v));
    }
    for (i = 0; i < 2; i++) {
        int64_t v = mmx_lane_s(s, i, 32);
        r = mmx_lane_put(r, 2 + i, 16, (uint64_t)(v < -32768 ? -32768 : v > 32767 ? 32767 : v));
    }
    return r;
}

/* Interleave. LOW takes the low half of each operand, HIGH the high half, and
   the DESTINATION's lane comes first in every pair. */
#define MMX_UNPACK(name, w, high)                                       \
static inline uint64_t name(uint64_t d, uint64_t s)                     \
{                                                                       \
    uint64_t r = 0; int i, n = MMX_LANES(w) / 2, base = (high) ? n : 0; \
    for (i = 0; i < n; i++) {                                           \
        r = mmx_lane_put(r, 2 * i,     w, mmx_lane_get(d, base + i, w)); \
        r = mmx_lane_put(r, 2 * i + 1, w, mmx_lane_get(s, base + i, w)); \
    }                                                                   \
    return r;                                                           \
}
MMX_UNPACK(mmx_punpcklbw,  8, 0)
MMX_UNPACK(mmx_punpckhbw,  8, 1)
MMX_UNPACK(mmx_punpcklwd, 16, 0)
MMX_UNPACK(mmx_punpckhwd, 16, 1)
MMX_UNPACK(mmx_punpckldq, 32, 0)
MMX_UNPACK(mmx_punpckhdq, 32, 1)

/* PMADDWD: four signed 16x16 products, added in pairs into two dwords. */
static inline uint64_t mmx_pmaddwd(uint64_t a, uint64_t b)
{
    uint64_t r = 0; int i;
    for (i = 0; i < 2; i++) {
        int64_t lo = mmx_lane_s(a, 2 * i, 16)     * mmx_lane_s(b, 2 * i, 16);
        int64_t hi = mmx_lane_s(a, 2 * i + 1, 16) * mmx_lane_s(b, 2 * i + 1, 16);
        r = mmx_lane_put(r, i, 32, (uint64_t)(int64_t)(int32_t)(lo + hi));
    }
    return r;
}

/*
 * SSE single-precision, on the host's own SSE.
 *
 * This is the one place in this runtime where reaching for the host
 * instruction is MORE faithful than writing the operation out, and the reason
 * is that the host is an x86-64 and the guest instruction IS the host
 * instruction -- same encoding family, same IEEE-754 binary32, same MXCSR
 * defaults (round-to-nearest, no flush-to-zero), same NaN propagation. Writing
 * `a < b ? a : b` for MINPS would be a DIFFERENT operation: hardware MINPS
 * returns the SECOND operand when either input is a NaN and for -0.0 vs +0.0,
 * and a matrix normalise that divides by a zero length hits both. RCPPS and
 * RSQRTSS are worse than that -- they are approximations whose result is only
 * specified to a relative error, so "1.0f/x" is not a more accurate version of
 * them, it is a different function, and the game's own Newton-Raphson refine
 * steps are written around the hardware's error.
 *
 * So: the lane SHUFFLING and the MOVE semantics (which halves are written,
 * which are preserved, when the upper lanes are zeroed) are written out here
 * explicitly, because those are the parts a wrong reading gets wrong silently.
 * The arithmetic is the host's.
 *
 * The guest's 128-bit register is kept as two uint64_t so it can be memcpy'd
 * both ways without aliasing games; every helper below converts at its edges.
 */
#if defined(__x86_64__) || defined(__i386__)
#include <xmmintrin.h>
#else
/* Refuse, by name, rather than substituting scalar code that differs on NaNs,
   signed zero and the reciprocal approximations. */
#error "The SSE model needs a host with SSE (x86). On another host the \
approximate instructions (RCPPS/RCPSS/RSQRTSS) and the NaN/signed-zero \
behaviour of MINPS/MAXPS/CMPPS cannot be reproduced, and a scalar stand-in \
would be silently different rather than absent."
#endif

typedef struct { uint64_t q[2]; } X86Xmm;

static inline __m128 sse_ld(const uint64_t *x)
{
    __m128 v; memcpy(&v, x, 16); return v;
}
static inline void sse_st(uint64_t *x, __m128 v)
{
    memcpy(x, &v, 16);
}

/* One 32-bit lane, as bits. Lane 0 is the LOW dword -- the same numbering the
   manual's [127:96][95:64][63:32][31:0] diagrams use read right to left. */
static inline uint32_t sse_lane(const uint64_t *x, int i)
{
    return (uint32_t)(x[i >> 1] >> ((i & 1) * 32));
}
static inline void sse_lane_put(uint64_t *x, int i, uint32_t v)
{
    int sh = (i & 1) * 32;
    x[i >> 1] = (x[i >> 1] & ~(0xFFFFFFFFULL << sh)) | ((uint64_t)v << sh);
}

#define SSE_BINOP(name, ins)                                            \
static inline void name(uint64_t *d, const uint64_t *s)                 \
{ sse_st(d, ins(sse_ld(d), sse_ld(s))); }
SSE_BINOP(sse_addps, _mm_add_ps)
SSE_BINOP(sse_subps, _mm_sub_ps)
SSE_BINOP(sse_mulps, _mm_mul_ps)
SSE_BINOP(sse_divps, _mm_div_ps)
SSE_BINOP(sse_minps, _mm_min_ps)
SSE_BINOP(sse_maxps, _mm_max_ps)
SSE_BINOP(sse_andps, _mm_and_ps)
SSE_BINOP(sse_andnps, _mm_andnot_ps)   /* ~dst & src, in THAT order */
SSE_BINOP(sse_orps,  _mm_or_ps)
SSE_BINOP(sse_xorps, _mm_xor_ps)

/* The scalar forms touch lane 0 ONLY; lanes 1..3 of the destination are
   preserved, which is what makes MULSS usable on a register also holding a
   vector. _mm_*_ss has exactly that semantics. */
#define SSE_SCALAROP(name, ins)                                         \
static inline void name(uint64_t *d, const uint64_t *s)                 \
{ sse_st(d, ins(sse_ld(d), sse_ld(s))); }
SSE_SCALAROP(sse_addss, _mm_add_ss)
SSE_SCALAROP(sse_subss, _mm_sub_ss)
SSE_SCALAROP(sse_mulss, _mm_mul_ss)
SSE_SCALAROP(sse_divss, _mm_div_ss)
SSE_SCALAROP(sse_minss, _mm_min_ss)
SSE_SCALAROP(sse_maxss, _mm_max_ss)

static inline void sse_sqrtps(uint64_t *d, const uint64_t *s)
{ sse_st(d, _mm_sqrt_ps(sse_ld(s))); }
static inline void sse_rcpps(uint64_t *d, const uint64_t *s)
{ sse_st(d, _mm_rcp_ps(sse_ld(s))); }
static inline void sse_rsqrtps(uint64_t *d, const uint64_t *s)
{ sse_st(d, _mm_rsqrt_ps(sse_ld(s))); }
/* The scalar unary forms take lane 0 from the SOURCE and lanes 1..3 from the
   DESTINATION -- not from the source, which is the easy misreading. */
static inline void sse_sqrtss(uint64_t *d, const uint64_t *s)
{ sse_st(d, _mm_sqrt_ss(_mm_move_ss(sse_ld(d), sse_ld(s)))); }
static inline void sse_rcpss(uint64_t *d, const uint64_t *s)
{ sse_st(d, _mm_rcp_ss(_mm_move_ss(sse_ld(d), sse_ld(s)))); }
static inline void sse_rsqrtss(uint64_t *d, const uint64_t *s)
{ sse_st(d, _mm_rsqrt_ss(_mm_move_ss(sse_ld(d), sse_ld(s)))); }

/* CMPPS/CMPSS produce a MASK (all ones or all zeros per lane), not a boolean.
   The predicate is the instruction's imm8; Ghidra spells the common ones as
   separate mnemonics (CMPNEQPS), so the emitter passes the number. */
static inline void sse_cmpps(uint64_t *d, const uint64_t *s, int pred)
{
    __m128 a = sse_ld(d), b = sse_ld(s), r;
    switch (pred) {
    case 0: r = _mm_cmpeq_ps(a, b);   break;
    case 1: r = _mm_cmplt_ps(a, b);   break;
    case 2: r = _mm_cmple_ps(a, b);   break;
    case 3: r = _mm_cmpunord_ps(a, b); break;
    case 4: r = _mm_cmpneq_ps(a, b);  break;
    case 5: r = _mm_cmpnlt_ps(a, b);  break;
    case 6: r = _mm_cmpnle_ps(a, b);  break;
    default: r = _mm_cmpord_ps(a, b); break;
    }
    sse_st(d, r);
}
static inline void sse_cmpss(uint64_t *d, const uint64_t *s, int pred)
{
    __m128 a = sse_ld(d), b = sse_ld(s), r;
    switch (pred) {
    case 0: r = _mm_cmpeq_ss(a, b);   break;
    case 1: r = _mm_cmplt_ss(a, b);   break;
    case 2: r = _mm_cmple_ss(a, b);   break;
    case 3: r = _mm_cmpunord_ss(a, b); break;
    case 4: r = _mm_cmpneq_ss(a, b);  break;
    case 5: r = _mm_cmpnlt_ss(a, b);  break;
    case 6: r = _mm_cmpnle_ss(a, b);  break;
    default: r = _mm_cmpord_ss(a, b); break;
    }
    sse_st(d, r);
}

/* SHUFPS: the low two lanes of the result come from the DESTINATION and the
   high two from the SOURCE. Two selectors index each operand's own four
   lanes. */
static inline void sse_shufps(uint64_t *d, const uint64_t *s, unsigned imm)
{
    uint32_t r[4];
    r[0] = sse_lane(d, (int)( imm       & 3));
    r[1] = sse_lane(d, (int)((imm >> 2) & 3));
    r[2] = sse_lane(s, (int)((imm >> 4) & 3));
    r[3] = sse_lane(s, (int)((imm >> 6) & 3));
    d[0] = (uint64_t)r[0] | ((uint64_t)r[1] << 32);
    d[1] = (uint64_t)r[2] | ((uint64_t)r[3] << 32);
}

/* UNPCKLPS interleaves the LOW two lanes of each operand, UNPCKHPS the high
   two, destination lane first in every pair. */
static inline void sse_unpcklps(uint64_t *d, const uint64_t *s)
{
    uint32_t d0 = sse_lane(d, 0), d1 = sse_lane(d, 1);
    uint32_t s0 = sse_lane(s, 0), s1 = sse_lane(s, 1);
    d[0] = (uint64_t)d0 | ((uint64_t)s0 << 32);
    d[1] = (uint64_t)d1 | ((uint64_t)s1 << 32);
}
static inline void sse_unpckhps(uint64_t *d, const uint64_t *s)
{
    uint32_t d2 = sse_lane(d, 2), d3 = sse_lane(d, 3);
    uint32_t s2 = sse_lane(s, 2), s3 = sse_lane(s, 3);
    d[0] = (uint64_t)d2 | ((uint64_t)s2 << 32);
    d[1] = (uint64_t)d3 | ((uint64_t)s3 << 32);
}

/* MOVSS between two REGISTERS writes lane 0 and leaves 1..3 alone; the form
   that loads from MEMORY zeroes them. Two different instructions sharing one
   mnemonic, and the difference is a matrix column full of stale data. */
static inline void sse_movss_rr(uint64_t *d, const uint64_t *s)
{ sse_lane_put(d, 0, sse_lane(s, 0)); }
static inline void sse_movss_load(uint64_t *d, uint32_t bits)
{ d[0] = bits; d[1] = 0; }

static inline void sse_movhlps(uint64_t *d, const uint64_t *s) { d[0] = s[1]; }
static inline void sse_movlhps(uint64_t *d, const uint64_t *s) { d[1] = s[0]; }

/* MOVMSKPS: one bit per lane, taken from the SIGN bit. */
static inline uint32_t sse_movmskps(const uint64_t *s)
{
    return ((sse_lane(s, 0) >> 31) & 1u) | (((sse_lane(s, 1) >> 31) & 1u) << 1)
         | (((sse_lane(s, 2) >> 31) & 1u) << 2)
         | (((sse_lane(s, 3) >> 31) & 1u) << 3);
}

/*
 * COMISS / UCOMISS write the INTEGER flags, not a mask: ZF/PF/CF, with
 * OF/AF/SF cleared. Unordered (either operand NaN) sets all three of ZF, PF
 * and CF -- which is why the compiler tests PF first. The difference between
 * the two instructions is only which NaNs raise an exception, and exceptions
 * are masked here, so they compute the same flags.
 */
static inline void sse_comiss(CPU *C, const uint64_t *a, const uint64_t *b)
{
    float x, y; uint32_t xb = sse_lane(a, 0), yb = sse_lane(b, 0);
    uint32_t fl;
    memcpy(&x, &xb, 4); memcpy(&y, &yb, 4);
    /* Bit positions as FK_EXPLICIT stores them: CF 0, PF 2, ZF 6. */
    if (x != x || y != y)  fl = (1U << 6) | (1U << 2) | (1U << 0);
    else if (x > y)        fl = 0;
    else if (x < y)        fl = 1U << 0;
    else                   fl = 1U << 6;
    SETFLAGS(C, FK_EXPLICIT, fl, 0U, 0U, 4);
}

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
void x86_tail_dispatch(CPU *C, uint32_t target);
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
/* The body ends at a call to a function that never returns; getting past it
   means this port's implementation of that callee came back. */
void x86_after_noreturn(uint32_t fn_ep, const char *callee);

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
#define TAIL_DISPATCH(C, t) x86_tail_dispatch((C), (uint32_t)(t))

#endif /* X86RT_H */
