/*
 * The prompt-glyph detector, run against BOTH classes.
 *
 * Stage one of the renderer-side prompt feature is detection: sit on
 * FUN_005ee780 and count the strings that carry the port's prompt codepoints.
 * On the real tutorial run that detector answered ZERO -- 4,541 strings drew,
 * 2,273 of them carried some other non-ASCII wchar, and not one carried
 * 0x80..0x93. A zero from a detector that has never once produced a one is
 * not a measurement, so this test feeds it the case that MUST come out
 * positive, and the boundaries either side of the range.
 *
 * It drives x2_override_005ee780 itself rather than only the pure helper: the
 * shipping path is the override, and a test of the helper beside it would
 * leave the guest-register read (EDX carries the wide buffer) and the
 * unconditional super-call untested.
 */
#include "prompt_glyph_draw.h"
#include "pad_glyph_codes.h"
#include "x86rt.h"
#include "x86rt_native.h"

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>

int native_stubs_registered(const char *module, uint32_t linked_ep);
void x2_override_005ee780(CPU *C);

/* The guest's own address space is the low 4 GB, and RD16 is a raw deref of a
   32-bit address -- so the strings have to LIVE there. A page mapped at a
   fixed low address is the whole fake; nothing about the walk is simulated. */
#define GUEST_PAGE 0x70000000u

static int failures;
static uint32_t g_next;

static void fail(const char *what)
{
    printf("  FAIL  %s\n", what);
    failures++;
}

static void ok(const char *what) { printf("  pass  %s\n", what); }

/* Write a NUL-terminated wide string into guest memory, return its address. */
static uint32_t guest_wide(const uint16_t *codes, unsigned n)
{
    uint32_t at = g_next;
    unsigned i;
    for (i = 0; i < n; i++)
        *(uint16_t *)(uintptr_t)(at + i * 2u) = codes[i];
    *(uint16_t *)(uintptr_t)(at + n * 2u) = 0;
    g_next += (n + 1) * 2u;
    return at;
}

/* How many times the stock body was entered. The super-call is the promise
   stage one makes -- every string, unchanged -- so it is asserted, not
   assumed. */
static unsigned long g_super_calls;

void fn_XMen2_005ee780(CPU *C) { (void)C; g_super_calls++; }

/* Bind the argument the way the GUEST does, not the way the override happens
   to read it. FUN_005ee780 takes the wide string as its first STACK
   argument, so the test builds a real guest stack -- return address at ESP,
   string pointer at ESP+4. Handing it over in a register instead is what let
   the override read C->edx for a whole investigation while every run it
   measured reported a zero it could not have contradicted. */
#define GUEST_STACK_TOP (GUEST_PAGE + 0xf00u)
static uint32_t g_stack;

static void call_glyph_loop(CPU *cpu, uint32_t wide_string)
{
    memset(cpu, 0, sizeof *cpu);
    g_stack -= 8u;
    *(uint32_t *)(uintptr_t)(g_stack) = 0xdeadbeefu;   /* return address */
    *(uint32_t *)(uintptr_t)(g_stack + 4u) = wide_string;
    cpu->esp = g_stack;
    x2_override_005ee780(cpu);
}

static void expect(const uint16_t *codes, unsigned n, int want,
                   const char *what)
{
    uint32_t at = guest_wide(codes, n);
    int got = x2_string_has_prompt_glyph(at, 512u);
    if (got != want) {
        printf("  FAIL  %s: classifier said %d, expected %d\n",
               what, got, want);
        failures++;
    } else {
        ok(what);
    }
}

int main(void)
{
    void *page;

    /* The pack gate caches on first read, so it is set before anything calls
       into the subsystem. Without it the override is inert by design and the
       whole test would pass while measuring nothing. */
    setenv("X2_PROMPT_GLYPHS", "1", 1);

    page = mmap((void *)(uintptr_t)GUEST_PAGE, 0x1000,
                PROT_READ | PROT_WRITE,
                MAP_PRIVATE | MAP_ANONYMOUS | MAP_FIXED_NOREPLACE, -1, 0);
    if (page != (void *)(uintptr_t)GUEST_PAGE) {
        fprintf(stderr, "test_prompt_glyph_draw: could not map the guest page "
                        "at 0x%08x; RD16 dereferences that address directly, "
                        "so there is no test without it.\n", GUEST_PAGE);
        return 1;
    }
    g_next = GUEST_PAGE;
    g_stack = GUEST_STACK_TOP;

    if (!native_stubs_registered("XMen2.exe", 0x005ee780))
        fail("the constructor did not register the glyph-loop override");
    else
        ok("the constructor registers XMen2.exe 0x005ee780");

    {
        /* THE POSITIVE the real run never produced: a composed keycap label,
           exactly the shape prompt_labels.c builds -- cap left, the binding
           letter, cap right. */
        static const uint16_t label[] = {
            X2_KEYCAP_GLYPH_LEFT, 'E', X2_KEYCAP_GLYPH_RIGHT,
        };
        expect(label, 3, 1, "a composed keycap label is DETECTED");
    }
    {
        static const uint16_t pad[] = { 'P', 'r', 'e', 's', 's', ' ',
                                        X2_PAD_GLYPH_FACE_A };
        expect(pad, 7, 1, "a pad face-button label is DETECTED");
    }
    {
        static const uint16_t lo[] = { X2_PROMPT_GLYPH_FIRST };
        static const uint16_t hi[] = { X2_PROMPT_GLYPH_LAST };
        expect(lo, 1, 1, "the first codepoint of the range is detected");
        expect(hi, 1, 1, "the last codepoint of the range is detected");
    }
    {
        /* And the NEGATIVES, either side of the range and from the real run.
           0x00bd is the copyright glyph on the legal screen, 0x01f2/0x9d28 are
           the engine's own above-256 control words -- all three arrived at the
           glyph loop in the tutorial run and none is ours. */
        static const uint16_t below[] = { X2_PROMPT_GLYPH_FIRST - 1 };
        static const uint16_t above[] = { X2_PROMPT_GLYPH_LAST + 1 };
        static const uint16_t plain[] = { 'N', 'E', 'W', ' ', 'G', 'A', 'M',
                                          'E' };
        static const uint16_t seen[] = { 0x9d28, 0x01f2, 0x08e2 };
        static const uint16_t legal[] = { 'C', 'o', 'p', 'y', 'r', 'i', 'g',
                                          'h', 't', ' ', 0x00bd };
        expect(below, 1, 0, "0x7f, just below the range, is not ours");
        expect(above, 1, 0, "0x94, just above the range, is not ours");
        expect(plain, 8, 0, "ordinary menu text is not ours");
        expect(seen, 3, 0, "the engine's above-256 control words are not ours");
        expect(legal, 11, 0, "the legal screen's copyright glyph is not ours");
    }
    {
        /* A NUL inside the buffer ends the string: bytes past it belong to
           whatever the engine put there and must not be classified. */
        uint32_t at = g_next;
        *(uint16_t *)(uintptr_t)(at + 0) = 'A';
        *(uint16_t *)(uintptr_t)(at + 2) = 0;
        *(uint16_t *)(uintptr_t)(at + 4) = X2_PAD_GLYPH_FACE_A;
        *(uint16_t *)(uintptr_t)(at + 6) = 0;
        g_next += 8;
        if (x2_string_has_prompt_glyph(at, 512u))
            fail("the walk read past the string's own NUL");
        else
            ok("the walk stops at the string's NUL");
    }
    if (x2_string_has_prompt_glyph(0, 512u))
        fail("a null string pointer was classified as carrying a prompt");
    else
        ok("a null string pointer is not classified as a prompt");

    {
        /* The shipping path: the override itself, reading EDX as the guest
           does, on one prompt string and one ordinary one. Both must
           super-call -- stage one changes no pixels. */
        static const uint16_t label[] = {
            X2_KEYCAP_GLYPH_LEFT, 'E', X2_KEYCAP_GLYPH_RIGHT,
        };
        static const uint16_t plain[] = { 'O', 'K' };
        CPU cpu;
        unsigned long before = g_super_calls;

        call_glyph_loop(&cpu, guest_wide(label, 3));
        call_glyph_loop(&cpu, guest_wide(plain, 2));

        if (g_super_calls != before + 2)
            fail("the override did not super-call for every string");
        else
            ok("the override super-calls for every string, prompt or not");
    }

    /* The report itself runs, so a change that breaks its format is caught
       here rather than in a run log nobody diffs. */
    printf("  the report reads:\n");
    x2_prompt_draw_report();

    printf("\ntest_prompt_glyph_draw: %d failure(s)\n", failures);
    return failures ? 1 : 0;
}
