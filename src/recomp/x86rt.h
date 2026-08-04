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

#include <stdint.h>
#include <intrin.h>

enum {
    FK_NONE = 0, FK_ADD, FK_SUB, FK_LOGIC, FK_INC, FK_DEC, FK_SHIFT
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
} CPU;

/* Runtime base of the ORIGINAL module. Absolute references into the module's
   own image are emitted relative to this, because the DLL is relocated inside
   the game (observed at 0x001C0000 rather than its preferred 0x10000000) and a
   hardcoded address would then read unrelated, still-mapped memory. */
extern uint32_t g_imgbase;
#define G_IMGBASE (g_imgbase)

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

static inline int x86_msb(uint32_t v, int w)
{
    return (int)((v >> (w * 8 - 1)) & 1U);
}

static inline int FLAG_Z(const CPU *C)
{
    return (C->f_r & x86_mask(C->f_w)) == 0;
}

static inline int FLAG_S(const CPU *C)
{
    return x86_msb(C->f_r, C->f_w);
}

static inline int FLAG_C(const CPU *C)
{
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
    uint32_t v = C->f_r & 0xFFU;
    v ^= v >> 4; v ^= v >> 2; v ^= v >> 1;
    return (int)(~v & 1U);
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
# define X86_ENTER_FN(a) (x86_hist[x86_hist_n++ & (X86_HIST - 1)] = (a))
void x86_dump_history(void);
#else
# define X86_ENTER_FN(a) ((void)0)
# define x86_dump_history() ((void)0)
#endif
/* call/jump into the region with no identified function; aborts by address */
void x86_call_unknown(CPU *C, uint32_t target);

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

/* real code -> recompiled body; returns EAX */
uint32_t x86_enter(uint32_t ep, uint32_t guest_esp, uint32_t ecx);
/* recompiled body -> real code, running on the guest stack */
void x86_call_host(CPU *C, void *fn, const char *what);
#define DISPATCH(C, t) x86_dispatch((C), (uint32_t)(t))

#endif /* X86RT_H */
