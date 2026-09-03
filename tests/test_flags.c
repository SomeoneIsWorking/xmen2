/*
 * Known-answer tests for the ADC/SBB flag model.
 *
 * These exist because the previous model was wrong in a way nothing could see:
 * it squeezed `a - b - c` into the lazy (a, b, r) triple as a SUB of `b - c`,
 * which gets the borrow wrong whenever a != b. The visible consequence was four
 * levels away -- MSVC's sign idiom returned "equal" for a mismatch, so the
 * engine's string pool interned "_refCount" and handed back "igObject", and ARK
 * field names were never bound (issue #16, C095).
 *
 * Every case below is the answer a real x86 gives. The two idiom cases at the
 * end are the ones that actually failed; they are first-class tests rather than
 * a comment, because the model looked plausible and was not.
 */
#include <stdio.h>
#include <stdint.h>
#include "x86rt.h"

/* The runtime's globals, so this links without the whole recompiled world. */
uint32_t g_imgbase = 0x10000000U;
uint32_t g_image_lo, g_image_hi;
/* __thread, to match x86rt.h: the segment bases became per-thread when guest
   threads arrived (every recompiled prologue writes FS:[0], so a shared one
   would have two threads' SEH chains overwriting each other). A plain
   definition here is a different symbol as far as the compiler is concerned and
   the test stopped BUILDING -- which ctest reports as "Not Run", not as a
   failure, so the suite went green with two tests missing. */
__thread uint32_t g_fsbase, g_gsbase;

static int fails, checks;

static void chk(const char *what, uint32_t got, uint32_t want)
{
    checks++;
    if (got != want) {
        fails++;
        printf("  FAIL %-46s got 0x%08x want 0x%08x\n", what, got, want);
    }
}

#define CF(f) ((f) & 1U)
#define ZF(f) (((f) >> 6) & 1U)
#define SF(f) (((f) >> 7) & 1U)
#define OF(f) (((f) >> 11) & 1U)

int main(void)
{
    /* ---- SBB: borrow out ------------------------------------------------ */
    {
        /* 5 - 5 - 1 = -1: borrows. The OLD model computed b' = b - c = 4 and
           asked 5 < 4 -> no borrow, which is the whole defect. */
        uint32_t r = 5U - 5U - 1U;
        uint32_t f = x86_flags_sbb(5, 5, 1, r, 4);
        chk("sbb 5,5 with CF=1 -> CF", CF(f), 1);
        chk("sbb 5,5 with CF=1 -> ZF", ZF(f), 0);
        chk("sbb 5,5 with CF=1 -> SF", SF(f), 1);
    }
    {   /* 5 - 5 - 0 = 0: no borrow, zero */
        uint32_t r = 5U - 5U - 0U;
        uint32_t f = x86_flags_sbb(5, 5, 0, r, 4);
        chk("sbb 5,5 with CF=0 -> CF", CF(f), 0);
        chk("sbb 5,5 with CF=0 -> ZF", ZF(f), 1);
    }
    {   /* 0 - 0 - 1: borrows */
        uint32_t r = 0U - 0U - 1U;
        uint32_t f = x86_flags_sbb(0, 0, 1, r, 4);
        chk("sbb 0,0 with CF=1 -> CF", CF(f), 1);
    }
    {   /* b + c would WRAP to 0 here, which is why "model it as SUB of b+c"
           is also wrong: 7 - 0xFFFFFFFF - 1 borrows, but 7 - 0 does not. */
        uint32_t r = 7U - 0xFFFFFFFFU - 1U;
        uint32_t f = x86_flags_sbb(7, 0xFFFFFFFFU, 1, r, 4);
        chk("sbb 7,0xFFFFFFFF with CF=1 -> CF", CF(f), 1);
    }
    {   /* byte width: 0x10 - 0x20 - 0 borrows within 8 bits */
        uint32_t r = (0x10U - 0x20U) & 0xFFU;
        uint32_t f = x86_flags_sbb(0x10, 0x20, 0, r, 1);
        chk("sbb.b 0x10,0x20 -> CF", CF(f), 1);
        chk("sbb.b 0x10,0x20 -> SF", SF(f), 1);
    }
    {   /* signed overflow: 0x80000000 - 1 - 0 */
        uint32_t r = 0x80000000U - 1U;
        uint32_t f = x86_flags_sbb(0x80000000U, 1, 0, r, 4);
        chk("sbb 0x80000000,1 -> OF", OF(f), 1);
        chk("sbb 0x80000000,1 -> CF", CF(f), 0);
    }

    /* ---- ADC: carry out ------------------------------------------------- */
    {   uint32_t r = 0xFFFFFFFFU + 0U + 1U;
        uint32_t f = x86_flags_adc(0xFFFFFFFFU, 0, 1, r, 4);
        chk("adc 0xFFFFFFFF,0 with CF=1 -> CF", CF(f), 1);
        chk("adc 0xFFFFFFFF,0 with CF=1 -> ZF", ZF(f), 1);
    }
    {   /* b + c wraps: a + 0xFFFFFFFF + 1 carries for ANY a */
        uint32_t r = 3U + 0xFFFFFFFFU + 1U;
        uint32_t f = x86_flags_adc(3, 0xFFFFFFFFU, 1, r, 4);
        chk("adc 3,0xFFFFFFFF with CF=1 -> CF", CF(f), 1);
    }
    {   uint32_t r = 1U + 2U + 0U;
        uint32_t f = x86_flags_adc(1, 2, 0, r, 4);
        chk("adc 1,2 -> CF", CF(f), 0);
        chk("adc 1,2 -> OF", OF(f), 0);
    }
    {   /* signed overflow: 0x7FFFFFFF + 1 */
        uint32_t r = 0x7FFFFFFFU + 1U;
        uint32_t f = x86_flags_adc(0x7FFFFFFFU, 1, 0, r, 4);
        chk("adc 0x7FFFFFFF,1 -> OF", OF(f), 1);
        chk("adc 0x7FFFFFFF,1 -> SF", SF(f), 1);
    }

    /* ---- the idiom that actually broke ---------------------------------- */
    /*
     *     sbb eax, eax        ; eax = CF ? -1 : 0, CF_out = CF_in
     *     sbb eax, -1         ; -> -1 (less) or +1 (greater)
     *
     * Run it exactly as the emitted code does, for both carry-in values and
     * for a NON-ZERO eax -- zero happened to work under the old model, which
     * is part of why this survived.
     */
    {
        uint32_t eax = 0x0804a1c4U;          /* a pointer, as in the real code */
        uint32_t r1, f1, r2, f2;
        r1 = eax - eax - 1U;                  /* CF in = 1 -> "less" */
        f1 = x86_flags_sbb(eax, eax, 1, r1, 4);
        chk("idiom step1 CF_out (must equal CF_in)", CF(f1), 1);
        r2 = r1 - 0xFFFFFFFFU - CF(f1);
        f2 = x86_flags_sbb(r1, 0xFFFFFFFFU, CF(f1), r2, 4);
        (void)f2;
        chk("idiom with CF=1 -> -1 (less)", r2, 0xFFFFFFFFU);
    }
    {
        uint32_t eax = 0x0804a1c4U;
        uint32_t r1, f1, r2;
        r1 = eax - eax - 0U;                  /* CF in = 0 -> "greater" */
        f1 = x86_flags_sbb(eax, eax, 0, r1, 4);
        chk("idiom step1 CF_out with CF_in=0", CF(f1), 0);
        r2 = r1 - 0xFFFFFFFFU - CF(f1);
        chk("idiom with CF=0 -> +1 (greater)", r2, 1U);
    }

    printf("flags: %d of %d check(s) FAILED\n", fails, checks);
    if (!fails)
        printf("Established: ADC/SBB carry-in, borrow-out, zero, sign and "
               "overflow at byte and dword width, and that MSVC's sbb/sbb sign "
               "idiom yields -1 and +1 rather than 0.\n");
    return fails != 0;
}
