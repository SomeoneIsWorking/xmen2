/* The shipping computeMatrix_Dx override, driven at its CPU/guest-memory ABI. */
#define _GNU_SOURCE
#include "guest_memory.h"
#include "gpu_matrix.h"
#include "ui_transform.h"
#include "x86rt.h"

#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <sys/mman.h>

int native_stubs_registered(const char *module, uint32_t linked_ep);

#define GUEST_PAGE   0x71000000u
#define GUEST_STACK  (GUEST_PAGE + 0x100u)
#define OUTPUT_REF   (GUEST_PAGE + 0x200u)
#define OUTPUT_MATRIX (GUEST_PAGE + 0x300u)

enum { TEST_PROJECTION = 0, TEST_WORLD = 1, TEST_VIEW = 14 };

static int failures;
static int matrix_readable = 1;
static int mutate_super_context;
static int pre_super_publish;
static float super_diagonal;

static void check(int condition, const char *what)
{
    if (!condition) {
        printf("  FAIL  %s\n", what);
        failures++;
    } else {
        printf("  pass  %s\n", what);
    }
}

int x86_peek32(uint32_t addr, uint32_t *out)
{
    if (addr < GUEST_PAGE || addr + 4u > GUEST_PAGE + 0x1000u) return 0;
    if (!matrix_readable && addr >= OUTPUT_MATRIX &&
        addr < OUTPUT_MATRIX + 16u * sizeof(float))
        return 0;
    *out = *(const uint32_t *)guest_memory_const_pointer(addr);
    return 1;
}

void fn_libIGGfx_1003ec10(CPU *C)
{
    float ignored[16];
    unsigned i;

    /* The wrapper must have invalidated the selector before it enters the
       original body, even when the previous set was complete. */
    pre_super_publish = x2_ui_transform_current(C->ecx, ignored);

    WR32(OUTPUT_REF, OUTPUT_MATRIX);
    for (i = 0; i < 16; i++)
        *(float *)guest_memory_pointer(OUTPUT_MATRIX + i * sizeof(float)) =
            i % 5u == 0 ? super_diagonal : 0.0f;
    if (mutate_super_context) C->ecx = 0xeeeeeeeeu;
}

static void capture(CPU *cpu, uint32_t context, uint32_t selector,
                    float diagonal)
{
    memset(cpu, 0, sizeof *cpu);
    WR32(GUEST_STACK, 0xfeedfaceu);
    WR32(GUEST_STACK + 4u, selector);
    WR32(GUEST_STACK + 8u, OUTPUT_REF);
    cpu->esp = GUEST_STACK;
    cpu->ecx = context;
    super_diagonal = diagonal;
    pre_super_publish = -1;
    x2_ui_transform_compute_matrix(cpu);
}

static int diagonal_is(const float matrix[16], float diagonal)
{
    unsigned i;
    for (i = 0; i < 16; i++) {
        float want = i % 5u == 0 ? diagonal : 0.0f;
        if (matrix[i] != want) return 0;
    }
    return 1;
}

int main(void)
{
    static const uint32_t context_a = 0x11111111u;
    static const uint32_t context_b = 0x22222222u;
    CPU cpu;
    float mvp[16];
    if (guest_memory_init() != 0 ||
        guest_memory_map_fixed(GUEST_PAGE, 0x1000u,
                               PROT_READ | PROT_WRITE) != 0) {
        fprintf(stderr, "test_ui_transform: could not map the guest page at "
                        "0x%08x\n", GUEST_PAGE);
        return 1;
    }

    check(native_stubs_registered("libIGGfx.dll", 0x1003ec10u),
          "the override registers the RE'd computeMatrix_Dx entry");

    capture(&cpu, context_a, TEST_PROJECTION, 2.0f);
    capture(&cpu, context_a, TEST_WORLD, 3.0f);
    capture(&cpu, context_a, TEST_VIEW, 5.0f);
    check(x2_ui_transform_current(context_a, mvp) &&
          diagonal_is(mvp, 30.0f),
          "one context publishes its complete world/view/projection product");

    matrix_readable = 0;
    capture(&cpu, context_a, TEST_PROJECTION, 7.0f);
    check(pre_super_publish == 0,
          "a selector is invalid before the original compute body runs");
    check(!x2_ui_transform_current(context_a, mvp),
          "an unreadable replacement cannot leave the stale selector valid");
    matrix_readable = 1;

    capture(&cpu, context_a, TEST_PROJECTION, 7.0f);
    check(x2_ui_transform_current(context_a, mvp) &&
          diagonal_is(mvp, 105.0f),
          "a readable replacement restores the same context's complete set");

    mutate_super_context = 1;
    capture(&cpu, context_b, TEST_PROJECTION, 11.0f);
    mutate_super_context = 0;
    check(!x2_ui_transform_current(context_a, mvp) &&
          !x2_ui_transform_current(context_b, mvp),
          "changing visual context clears every matrix from the prior one");

    capture(&cpu, context_b, TEST_WORLD, 13.0f);
    capture(&cpu, context_b, TEST_VIEW, 17.0f);
    check(x2_ui_transform_current(context_b, mvp) &&
          diagonal_is(mvp, 2431.0f),
          "the context key is captured before the super-call mutates ECX");

    capture(&cpu, context_a, 99u, 1.0f);
    check(!x2_ui_transform_current(context_b, mvp),
          "even an untracked selector on another context prevents mixing");

    printf("  the report reads:\n");
    x2_ui_transform_report();
    printf("\ntest_ui_transform: %d failure(s)\n", failures);
    return failures ? 1 : 0;
}
