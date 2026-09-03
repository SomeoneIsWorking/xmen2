/* Shipping-path checks for the native CRT import surface. */
#include "guest_body.h"
#include "crt_selftest.h"

#include "guest_heap.h"
#include "x86rt.h"
#include "x86rt_native.h"

#include <stdio.h>

/* XMen2.exe's generated thunk for MSVCR71.dll!??_V@YAXPAX@Z. Calling the
 * guest address makes the generated dispatch table and its exact canonical
 * import identifier part of the test; calling crt.c directly would not catch
 * a misspelled implementation symbol. */
#define XMEN2_OPERATOR_DELETE_ARRAY_THUNK 0x00672538u

void imp_MSVCRT_malloc(CPU *C);
void imp_MSVCRT_free(CPU *C);
void imp_MSVCRT____V_YAXPAX_Z(CPU *C);

static uint32_t call_import(void (*fn)(CPU *), CPU *C, uint32_t stack_top,
                            uint32_t argument, int skip_body)
{
    uint32_t esp0;
    cpu_reset(C);
    C->esp = stack_top - 8u;
    WR32(C->esp, 0xDEADBEEFu);
    WR32(C->esp + 4u, argument);
    esp0 = C->esp;
    if (!skip_body) fn(C);
    return C->esp - esp0;
}

static void check_malloc_free(uint32_t stack_top, int skip_body,
                              CrtSelftestCheck check)
{
    CPU C;
    uint32_t delta;
    uint32_t pointer;

    delta = call_import(imp_MSVCRT_malloc, &C, stack_top, 64u, skip_body);
    pointer = C.eax;
    check("malloc(64) != 0", pointer != 0u, 1u);
    check("malloc cdecl esp delta", delta, 4u);
    if (pointer) {
        delta = call_import(imp_MSVCRT_free, &C, stack_top, pointer, skip_body);
        check("free cdecl esp delta", delta, 4u);
        if (guest_heap_addr_is_live(pointer)) guest_free(pointer);
    }
}

static void check_delete_array(uint32_t stack_top, int skip_body,
                               CrtSelftestCheck check)
{
    CPU C;
    uint32_t baseline_used, ignored_free, ignored_blocks;
    uint32_t after_used;
    uint32_t pointer;
    uint32_t esp0;
    int body_found = 0;

    guest_heap_stats(&baseline_used, &ignored_free, &ignored_blocks);
    pointer = guest_malloc(64u);
    cpu_reset(&C);
    C.esp = stack_top - 8u;
    WR32(C.esp, 0xDEADBEEFu);
    WR32(C.esp + 4u, pointer);
    esp0 = C.esp;

    if (!skip_body) {
        char why[256];
        body_found = x86_guest_body_try(&C, "XMen2.exe",
                                        XMEN2_OPERATOR_DELETE_ARRAY_THUNK,
                                        why, sizeof why);
        if (!body_found) printf("    (%s)\n", why);
    }
    guest_heap_stats(&after_used, &ignored_free, &ignored_blocks);

    /* The exe's own operator delete[] is a JMP through its IAT, so running it
       proves the whole route: guest thunk -> bound import slot -> host stub. */
    check("delete[] route runs from the exe's own thunk",
          (uint32_t)body_found, 1u);
    check("delete[] frees its guest allocation",
          (uint32_t)guest_heap_addr_is_live(pointer), 0u);
    check("delete[] cdecl esp delta", C.esp - esp0, 4u);
    check("delete[] restores heap usage", after_used, baseline_used);

    if (guest_heap_addr_is_live(pointer)) guest_free(pointer);
}

static void check_msvcrt_delete_array_alias(uint32_t stack_top, int skip_body,
                                            CrtSelftestCheck check)
{
    CPU C;
    uint32_t pointer = guest_malloc(64u);
    uint32_t delta = call_import(imp_MSVCRT____V_YAXPAX_Z, &C, stack_top,
                                 pointer, skip_body);

    check("MSVCRT delete[] alias frees", (uint32_t)guest_heap_addr_is_live(pointer),
          0u);
    check("MSVCRT delete[] cdecl esp delta", delta, 4u);
    if (guest_heap_addr_is_live(pointer)) guest_free(pointer);
}

void crt_selftest_run(uint32_t stack_top, int skip_body, CrtSelftestCheck check)
{
    printf("  native CRT import ABI and operator delete[] route\n");
    check_malloc_free(stack_top, skip_body, check);
    check_delete_array(stack_top, skip_body, check);
    check_msvcrt_delete_array_alias(stack_top, skip_body, check);
}
