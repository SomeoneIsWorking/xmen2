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

enum {
    FK_NONE = 0, FK_ADD, FK_SUB, FK_LOGIC, FK_INC, FK_DEC, FK_SHIFT
};

typedef struct CPU {
    uint32_t eax, ecx, edx, ebx, esp, ebp, esi, edi;
    /* lazy flag state */
    uint32_t f_a, f_b, f_r;
    int      f_kind;
    int      f_w;        /* operand width in bytes: 1, 2 or 4 */
} CPU;

/* Runtime base of the ORIGINAL module. Absolute references into the module's
   own image are emitted relative to this, because the DLL is relocated inside
   the game (observed at 0x001C0000 rather than its preferred 0x10000000) and a
   hardcoded address would then read unrelated, still-mapped memory. */
extern uint32_t g_imgbase;
#define G_IMGBASE (g_imgbase)

/* ---- memory: guest address == host address (see header comment) ---- */
#define RD8(a)      (*(volatile uint8_t  *)(uintptr_t)(a))
#define RD16(a)     (*(volatile uint16_t *)(uintptr_t)(a))
#define RD32(a)     (*(volatile uint32_t *)(uintptr_t)(a))
#define WR8(a, v)   (*(volatile uint8_t  *)(uintptr_t)(a) = (uint8_t)(v))
#define WR16(a, v)  (*(volatile uint16_t *)(uintptr_t)(a) = (uint16_t)(v))
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

/* ---- indirect dispatch ----
 * Virtual calls and function pointers resolve at runtime. An address with no
 * recompiled body is a hard error, never a silent no-op: continuing past an
 * unresolved call produces a plausible-looking run that is wrong.
 */
void x86_dispatch(CPU *C, uint32_t target);

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
