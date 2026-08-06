/*
 * Known-answer tests for the guest printf format walker (src/native/crt.c).
 *
 * The walker exists because a va_list cannot be handed to libc from here: on
 * x86-32 cdecl the argument list IS a region of guest stack, so each directive
 * has to be pulled out by hand. That makes it a small parser, and a small
 * parser that is only ever exercised by the game is one whose bugs show up as
 * a wrong filename or a wrong key three subsystems away.
 *
 * Guest memory is host memory under the identity mapping, so a plain array
 * stands in for the guest stack here and the walker is fed exactly what it
 * would see in a real call.
 */
#include <stdio.h>
#include <stdint.h>
#include <string.h>

int guest_vformat(char *out, size_t cap, const char *fmt, uint32_t va);

/* The runtime globals the header pulls in. */
uint32_t g_imgbase = 0x10000000U;
uint32_t g_image_lo, g_image_hi;
uint32_t g_fsbase, g_gsbase;
int x86_allow_fallback;

static int fails, checks;

static void eq(const char *what, const char *got, const char *want)
{
    checks++;
    if (strcmp(got, want) != 0) {
        fails++;
        printf("  FAIL %-40s got \"%s\" want \"%s\"\n", what, got, want);
    }
}

/* A stand-in guest argument list: 4-byte slots, doubles taking two. */
static uint32_t slots[32];
static int nslots;
static void push_d(uint32_t v) { slots[nslots++] = v; }
static void push_f(double d)
{
    union { double d; uint32_t u[2]; } u;
    u.d = d;
    slots[nslots++] = u.u[0];
    slots[nslots++] = u.u[1];
}
static uint32_t va(void) { return (uint32_t)(uintptr_t)slots; }

int main(void)
{
    char out[256];

    nslots = 0; push_d(42);
    guest_vformat(out, sizeof out, "n=%d", va());
    eq("plain %d", out, "n=42");

    nslots = 0; push_d(0xDEADBEEF);
    guest_vformat(out, sizeof out, "%08X", va());
    eq("width+zero pad %08X", out, "DEADBEEF");

    nslots = 0; push_d((uint32_t)(uintptr_t)"hello");
    guest_vformat(out, sizeof out, "[%s]", va());
    eq("%s from a guest pointer", out, "[hello]");

    nslots = 0; push_d(0);
    guest_vformat(out, sizeof out, "[%s]", va());
    eq("%s with a NULL pointer", out, "[(null)]");

    nslots = 0; push_f(1.5);
    guest_vformat(out, sizeof out, "%.2f", va());
    eq("%f consumes EIGHT bytes", out, "1.50");

    /* The ordering case: a double in the middle must not desynchronise the
       arguments after it. This is the failure a 4-byte-per-arg walker gives. */
    nslots = 0; push_d(1); push_f(2.5); push_d(3);
    guest_vformat(out, sizeof out, "%d %.1f %d", va());
    eq("int, double, int stay in step", out, "1 2.5 3");

    nslots = 0; push_d('x');
    guest_vformat(out, sizeof out, "<%c>", va());
    eq("%c", out, "<x>");

    nslots = 0; push_d(7);
    guest_vformat(out, sizeof out, "100%% of %d", va());
    eq("%% literal", out, "100% of 7");

    nslots = 0; push_d(5); push_d(42);
    guest_vformat(out, sizeof out, "%*d", va());
    eq("star width consumes an argument", out, "   42");

    nslots = 0;
    { union { uint64_t u; uint32_t p[2]; } q; q.u = 0x1122334455667788ULL;
      push_d(q.p[0]); push_d(q.p[1]); }
    guest_vformat(out, sizeof out, "%I64x", va());
    eq("MSVC I64 spelling", out, "1122334455667788");

    /* Truncation: writes at most cap, always NUL-terminates, and reports the
       length it WOULD have needed. */
    nslots = 0; push_d((uint32_t)(uintptr_t)"abcdefghij");
    {
        char small[5];
        int n = guest_vformat(small, sizeof small, "%s", va());
        checks++;
        if (n != 10 || strcmp(small, "abcd") != 0) {
            fails++;
            printf("  FAIL truncation: n=%d buf=\"%s\" want n=10 buf=\"abcd\"\n",
                   n, small);
        }
    }

    printf("vformat: %d of %d check(s) FAILED\n", fails, checks);
    if (!fails)
        printf("Established: %%d/%%x/%%s/%%c/%%f/%%%%, star width, MSVC's I64, a "
               "NULL %%s, truncation, and that a double consumes two argument "
               "slots so the arguments after it stay in step.\n");
    return fails != 0;
}

/*
 * Stubs for the parts of the runtime crt.c references but this test never
 * reaches. They ABORT rather than returning a value: if the walker ever
 * depends on one of them, the test must fail loudly rather than pass on a
 * fabricated answer.
 */
#include <stdlib.h>
struct CPU;
void x87_fault(const char *what)
{ fprintf(stderr, "test_vformat: x87_fault(%s) reached\n", what); abort(); }
void x86_guest_call(struct CPU *C, uint32_t t)
{ (void)C; (void)t; fprintf(stderr, "test_vformat: x86_guest_call reached\n"); abort(); }
uint32_t x86_guest_addr_of(void *p)
{ (void)p; fprintf(stderr, "test_vformat: x86_guest_addr_of reached\n"); abort(); }
const char *x86_native_name_at(uint32_t a)
{ (void)a; fprintf(stderr, "test_vformat: x86_native_name_at reached\n"); abort(); }
