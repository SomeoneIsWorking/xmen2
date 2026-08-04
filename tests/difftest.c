/* Differential test: run each recompiled function and the ORIGINAL from the
 * shipped DLL on identical random inputs, and compare.
 *
 * This is the only thing that can show the recompiler is *correct*. Coverage
 * ("96% of functions translate") describes the translator, not whether the
 * emitted C computes what the x86 computed. Lazy flags, shift semantics and
 * operand widths all admit plausible-but-wrong code.
 *
 * Two hazards, both handled rather than avoided by cherry-picking easy cases:
 *
 * 1. ABI. The original is __thiscall and cleans its own arguments, but we do
 *    not know each function's argument count. So the call is made in inline asm
 *    with ESP saved and restored around it -- correct whether the callee does
 *    `ret`, `ret 4` or `ret 8`.
 *
 * 2. Invalid objects. Random bytes are not a valid Alchemy object, so a
 *    function that chases a pointer out of it will fault. A vectored exception
 *    handler catches that: if the ORIGINAL faults, the input is simply invalid
 *    and the trial is skipped; if the RECOMPILED code faults where the original
 *    did not, that is a failure. Skips are COUNTED AND PRINTED -- a case that
 *    skipped every trial must never read as a pass.
 *
 * Built as a 32-bit PE, run under Wine so the original DLL can be loaded.
 */
#include <windows.h>
#include <setjmp.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "x86rt.h"
#include "difftest_cases.h"

int x86_resolve_imports(void);

/* The two objects sit at the centre of two large regions filled with IDENTICAL
   bytes. Several of these functions index memory by their argument
   (`MOV AL,[EAX + ECX + 8]`), reading far outside the object; with bare
   buffers the two sides read different addresses and diverge for reasons that
   have nothing to do with the translation. Mirrored regions make any read
   within +/-REGION/2 of `this` see the same bytes on both sides. */
#define REGION     0x20000
#define OBJ_OFF    (REGION / 2)
#define ARG_MASK   0xFFFU    /* args are enum/index shaped; see C0xx limits */
#define OBJ_SIZE   0x200
#define STACK_SIZE 0x8000
#define TRIALS     30

static jmp_buf g_jmp;
static volatile int g_expect_fault;

static LONG CALLBACK fault_handler(EXCEPTION_POINTERS *ep)
{
    if (g_expect_fault &&
        ep->ExceptionRecord->ExceptionCode == EXCEPTION_ACCESS_VIOLATION) {
        longjmp(g_jmp, 1);
    }
    return EXCEPTION_CONTINUE_SEARCH;
}

/* An indirect call whose target came from a random object is not a defect --
   it means the input was invalid, exactly like a page fault. Skip the trial via
   the same path. Overrides the aborting default in the runtime. */
void x86_dispatch_miss(uint32_t target)
{
    (void)target;
    if (g_expect_fault) longjmp(g_jmp, 1);
    abort();
}

/* A random object yields a random vtable pointer, which can dispatch into an
   arbitrary recompiled function and recurse without bound -- that overflowed
   the real stack and killed a whole run after 106 cases, which no fault handler
   can recover from. Cap the nesting and treat exceeding it as an invalid input,
   the same as a page fault. */
int g_dispatch_depth;
#define MAX_DISPATCH_DEPTH 16

static uint32_t rng_state = 0x12345678U;   /* fixed seed: failures reproduce */
static uint32_t rnd(void)
{
    rng_state ^= rng_state << 13;
    rng_state ^= rng_state >> 17;
    rng_state ^= rng_state << 5;
    return rng_state;
}

/* EAX is seeded identically on both sides. Many of these are empty virtual
   methods that just `ret` and never set a return value; comparing whatever
   happened to be in EAX would report them all as mismatches (it did, for 15
   cases, before this). With a sentinel, a void function leaves the sentinel on
   both sides and the comparison additionally proves it did not clobber EAX. */
#define EAX_SENTINEL 0xA5A5F00DU

/* Call the original with ESP saved/restored, so an unknown callee-cleanup
   count cannot corrupt our stack. */
static uint32_t call_orig(void *fn, void *self, uint32_t arg)
{
    uint32_t ret;
    __asm__ __volatile__(
        "movl %%esp, %%esi\n\t"
        "pushl %[a]\n\t"
        "pushl %[a]\n\t"
        "movl  %[s], %%ecx\n\t"
        "movl  $0xA5A5F00D, %%eax\n\t"
        "call  *%[f]\n\t"
        "movl  %%esi, %%esp\n\t"
        : "=a"(ret)
        : [f] "r"(fn), [s] "r"(self), [a] "r"(arg)
        : "ecx", "edx", "esi", "memory");
    return ret;
}

static uint32_t call_recomp(void (*fn)(CPU *), void *self, uint32_t arg,
                            uint8_t *stack)
{
    CPU C;
    uint32_t sp;
    memset(&C, 0, sizeof C);
    C.eax = EAX_SENTINEL;
    sp = (uint32_t)(uintptr_t)(stack + STACK_SIZE - 0x80);
    *(uint32_t *)(uintptr_t)(sp + 0) = 0xDEADBEEFU;   /* return address */
    *(uint32_t *)(uintptr_t)(sp + 4) = arg;
    *(uint32_t *)(uintptr_t)(sp + 8) = arg;
    C.esp = sp;
    C.ecx = (uint32_t)(uintptr_t)self;                /* __thiscall this */
    fn(&C);
    return C.eax;
}

int main(void)
{
    HMODULE h;
    FILE *vf;
    uint8_t *stack, *reg_o, *reg_r;
    unsigned ci;
    int cases_ok = 0, cases_fail = 0, cases_novalue = 0, cases_nosym = 0;
    unsigned long total_trials = 0, total_mismatch = 0, total_skip = 0;
    const unsigned NCASES = sizeof CASES / sizeof CASES[0];

    /* Force the DLL off its preferred base by squatting on 0x10000000 first.
       Without this the loader grants the preferred base here and the test
       silently never exercises relocated absolute image references -- which is
       exactly the case that occurs inside the real game (C015). Set X2_NORELOC=1
       to run the un-relocated case instead. */
    if (!getenv("X2_NORELOC")) {
        void *squat = VirtualAlloc((LPVOID)0x10000000, 0x100000,
                                   MEM_RESERVE, PAGE_NOACCESS);
        if (!squat)
            printf("-- WARNING: could not reserve 0x10000000; the DLL may still "
                   "get its preferred base and the relocated case would go "
                   "UNTESTED\n");
    }

    h = LoadLibraryA("libIGDisplay_orig.dll");
    if (!h) {
        fprintf(stderr, "difftest: cannot load libIGDisplay_orig.dll (err %lu) "
                        "-- tested NOTHING\n", (unsigned long)GetLastError());
        return 2;
    }
    /* Rebase absolute image references onto wherever the DLL actually landed.
       Without this the test only happens to pass when the loader grants the
       preferred base -- which it does here but does NOT inside the game. */
    g_imgbase = (uint32_t)(uintptr_t)h;
    /* NEGATIVE CONTROL: pin g_imgbase to the preferred base while the DLL is
       actually relocated, reproducing the un-rebased code exactly. This MUST
       fail; if it passes, the test cannot see rebasing errors at all. */
    if (getenv("X2_BREAK_REBASE")) {
        g_imgbase = 0x10000000U;
        printf("-- NEGATIVE CONTROL: g_imgbase pinned to 0x10000000 while the "
               "DLL is elsewhere; this run MUST report failures\n");
    }
    printf("-- libIGDisplay_orig.dll at 0x%08x (preferred 0x10000000)%s\n",
           g_imgbase,
           g_imgbase == 0x10000000U ? "  [NOT relocated -- relocation untested]"
                                    : "  [RELOCATED -- rebasing exercised]");
    stack = (uint8_t *)VirtualAlloc(NULL, STACK_SIZE, MEM_COMMIT, PAGE_READWRITE);
    if (!stack) { fprintf(stderr, "difftest: no stack\n"); return 2; }
    reg_o = (uint8_t *)VirtualAlloc(NULL, REGION, MEM_COMMIT, PAGE_READWRITE);
    reg_r = (uint8_t *)VirtualAlloc(NULL, REGION, MEM_COMMIT, PAGE_READWRITE);
    if (!reg_o || !reg_r) { fprintf(stderr, "difftest: no regions\n"); return 2; }
    AddVectoredExceptionHandler(1, fault_handler);
    {   /* Imports must resolve for real: functions that call into libIGCore are
           now under test rather than excluded. */
        int bad = x86_resolve_imports();
        if (bad) { printf("-- %d imports unresolved; tested NOTHING\n", bad);
                   return 2; }
    }
    vf = fopen("verified.eps", "w");
    if (!vf) printf("-- WARNING: cannot write verified.eps\n");

    for (ci = 0; ci < NCASES; ci++) {
        void *orig = (void *)GetProcAddress(h, CASES[ci].mangled);
        unsigned mism = 0, ran = 0, skip = 0, t, k2;
        if (!orig) {
            printf("%-58s SYMBOL NOT FOUND -- tested nothing\n", CASES[ci].label);
            cases_nosym++;
            continue;
        }
        rng_state = 0x12345678U ^ (ci * 0x9E3779B9U);
        /* Fill the mirrored regions ONCE per case, not per trial: refilling
           them every trial dominated the runtime (392 cases x 120 trials x 1MB).
           Per trial only the object window is re-randomised; everything outside
           it stays identical between the two regions, which is all the mirror
           has to guarantee. */
        for (k2 = 0; k2 < REGION; k2 += 4) {
            uint32_t v = rnd();
            *(uint32_t *)(reg_o + k2) = v;
            *(uint32_t *)(reg_r + k2) = v;
        }
        for (t = 0; t < TRIALS; t++) {
            /* Two buffers with identical starting contents: functions that
               WRITE through `this` must produce identical memory, not just an
               identical return value. Comparing only EAX would pass a function
               that computes the right answer and corrupts the object. */
            uint8_t *obj_o = reg_o + OBJ_OFF, *obj_r = reg_r + OBJ_OFF;
            uint32_t arg, want, got;
            unsigned k;
            for (k = 0; k < OBJ_SIZE; k += 4) {
                uint32_t v = rnd();
                *(uint32_t *)(obj_o + k) = v;
                *(uint32_t *)(obj_r + k) = v;
            }
            arg = rnd() & ARG_MASK;

            g_expect_fault = 1;
            if (setjmp(g_jmp)) { g_expect_fault = 0; skip++; continue; }
            want = call_orig(orig, obj_o, arg);

            if (setjmp(g_jmp)) {
                /* original survived, recompiled did not: a real defect */
                g_expect_fault = 0;
                if (mism < 3)
                    printf("  FAULT in recompiled %s arg=0x%08x\n",
                           CASES[ci].label, arg);
                mism++;
                continue;
            }
            g_dispatch_depth = 0;
            got = call_recomp(CASES[ci].recomp, obj_r, arg, stack);
            g_expect_fault = 0;
            ran++;
            /* A returned pointer INTO the object differs only because the two
               buffers live at different addresses; normalise to an offset. */
            if (want - (uint32_t)(uintptr_t)obj_o < OBJ_SIZE &&
                got  - (uint32_t)(uintptr_t)obj_r < OBJ_SIZE) {
                want -= (uint32_t)(uintptr_t)obj_o;
                got  -= (uint32_t)(uintptr_t)obj_r;
            }
            if (((got ^ want) & CASES[ci].mask) != 0) {
                if (mism < 3)
                    printf("  MISMATCH %s arg=0x%08x orig=0x%08x recomp=0x%08x\n",
                           CASES[ci].label, arg, want, got);
                mism++;
            } else if (memcmp(reg_o, reg_r, REGION) != 0) {
                unsigned off = 0;
                while (off < REGION && reg_o[off] == reg_r[off]) off++;
                if (mism < 3)
                    printf("  MEMDIFF %s arg=0x%08x first differing byte at "
                           "this%+d: orig=%02x recomp=%02x\n",
                           CASES[ci].label, arg, (int)off - OBJ_OFF,
                           reg_o[off], reg_r[off]);
                mism++;
            }
        }
        total_trials += ran;
        total_mismatch += mism;
        total_skip += skip;
        if (mism) {
            printf("%-58s %5u ok %5u BAD  (%u skipped) FAIL\n",
                   CASES[ci].label, ran, mism, skip);
            cases_fail++;
        } else if (!ran) {
            /* every trial faulted in the original: this case proved nothing */
            printf("%-58s NO VALID INPUT (all %u trials faulted in the "
                   "ORIGINAL) -- proves nothing\n", CASES[ci].label, skip);
            cases_novalue++;
        } else {
            /* Emit the verified set so the DLL build can recompile exactly
               what has been proven, and nothing else. */
            if (vf) fprintf(vf, "0x%08x  %s\n", CASES[ci].ep, CASES[ci].label);
            cases_ok++;
        }
    }

    printf("\n-- %u cases: %d verified, %d FAILED, %d untestable with random "
           "objects, %d symbol missing\n",
           NCASES, cases_ok, cases_fail, cases_novalue, cases_nosym);
    printf("-- %lu compared trials, %lu mismatches, %lu skipped (original "
           "faulted on an invalid object)\n",
           total_trials, total_mismatch, total_skip);
    if (vf) fclose(vf);
    if (!total_trials) {
        printf("-- NOTHING WAS COMPARED: this run proves nothing\n");
        return 2;
    }
    return cases_fail ? 1 : 0;
}
