#include "x86_engine.h"

#include "x86_engine_internal.h"
#include "x86_engine_take.h"
#include "guest_memory.h"
#include "x86rt.h"
#include "x86rt_native.h"

#include "cpu.h"
#include "x87.h"

#include <stdio.h>
#include <string.h>
#include <sys/mman.h>

/* ---- selftest ---------------------------------------------------------- */

/*
 * PROVE IT FIRES, IN THE SHIPPING ARTEFACT.
 *
 * The seam this engine sits on is a MISS: `x86_dispatch_one` reaches it only
 * when the statically recompiled corpus has no body for an address. On a run
 * that never hits one, x2_engine_report prints zeros -- and zeros from an
 * engine that works and zeros from an engine whose bridge is broken are the
 * same two lines. Nothing downstream could tell them apart.
 *
 * So the engine executes a program of its own before the game starts. It is a
 * real guest program, written into guest memory, entered through the same
 * x2_engine_call the dispatcher uses, and checked for the register, flag and
 * stack state it must produce. A failure here stops the run: an engine that
 * cannot run six instructions correctly must not be handed a function.
 */

#define SELFTEST_PAGE 0x00070000u

static int check(const char *what, uint32_t got, uint32_t want, int *failures)
{
    if (got == want) return 1;
    fprintf(stderr, "[ENGINE] selftest: %s is 0x%08x, expected 0x%08x\n", what,
            got, want);
    (*failures)++;
    return 0;
}

int x2_engine_selftest(void)
{
    /*
     *   mov  eax, 0x0000002A
     *   add  eax, 8              -> 0x32
     *   push eax
     *   pop  ebx
     *   xor  ebx, 0x000000FF     -> 0xCD, and CF/OF cleared by a logic op
     *   ret
     *
     * Chosen so every part of the bridge is load-bearing: two registers that
     * are not the accumulator, a PUSH/POP pair that moves ESP down and back
     * (so a stack the bridge mishandled would not balance), and a flag-writing
     * operation whose result is read back through the substrate's own model
     * rather than x86port's.
     */
    static const uint8_t program[] = {0xB8, 0x2A, 0x00, 0x00, 0x00,
                                      0x83, 0xC0, 0x08,
                                      0x50,
                                      0x5B,
                                      0x81, 0xF3, 0xFF, 0x00, 0x00, 0x00,
                                      0xC3};
    CPU cpu;
    uint32_t stack;
    int failures = 0;

    if (!x2_engine_active()) return 1; /* nothing selected: nothing to prove */

    if (guest_memory_map_fixed(SELFTEST_PAGE, 0x1000u,
                               PROT_READ | PROT_WRITE) != 0) {
        fprintf(stderr, "[ENGINE] selftest: could not map its own page at "
                        "0x%08x -- the engine is UNVERIFIED for this run.\n",
                SELFTEST_PAGE);
        return 0;
    }
    memcpy(guest_memory_pointer(SELFTEST_PAGE), program, sizeof program);

    /* A stack inside the same page, above the program.
       The CALLER pushes the return address -- that is the contract every real
       caller of x2_engine_call meets (x86_guest_call_args writes 0xDEADBEEF
       there; a recompiled body's CALL wrote a real one) -- so the selftest
       meets it too, rather than being the one entry that does not. */
    stack = SELFTEST_PAGE + 0x800u;
    WR32(stack - 4u, ENGINE_RETURN_ADDR);

    memset(&cpu, 0, sizeof cpu);
    cpu.esp = stack - 4u;
    cpu.fcw = X87_CW_INIT;
    cpu.eax = 0xDEADBEEFu;
    cpu.ebx = 0xDEADBEEFu;

    if (!x2_engine_call(SELFTEST_PAGE, &cpu)) {
        fprintf(stderr, "[ENGINE] selftest: x2_engine_call declined its own "
                        "program while an engine is selected.\n");
        return 0;
    }

    check("eax", cpu.eax, 0x32u, &failures);
    check("ebx", cpu.ebx, 0xCDu, &failures);
    check("esp", cpu.esp, stack, &failures);
    /* The flags XOR left, read back through the SUBSTRATE's accessors: this is
       the direction the bridge is used in when a dispatched call returns, so
       it is the direction that has to be checked. 0xCD has five set bits, so
       PF is clear; the result is neither zero nor negative at 32 bits; a logic
       operation clears CF and OF. */
    check("ZF", (uint32_t)FLAG_Z(&cpu), 0u, &failures);
    check("SF", (uint32_t)FLAG_S(&cpu), 0u, &failures);
    check("PF", (uint32_t)FLAG_P(&cpu), 0u, &failures);
    check("CF", (uint32_t)FLAG_C(&cpu), 0u, &failures);
    check("OF", (uint32_t)FLAG_O(&cpu), 0u, &failures);

    /*
     * The call-out predicate, against BOTH answers.
     *
     * x86_native_body_at decides whether the engine hands an address back to
     * the dispatcher, and a predicate that has only ever been asked about
     * addresses it says no to is indistinguishable from `return 0` -- it would
     * let the interpreter walk into a recompiled body and decode host memory
     * as x86-32. The positive case is a resolved override, whose mapped entry
     * point is a body by construction; the negative is this selftest's own
     * page, which is guest memory and nothing else.
     */
    {
        const uint32_t body = x86_override_mapped_ep(0);
        if (!body) {
            fprintf(stderr, "[ENGINE] selftest: no resolved override to ask "
                            "x86_native_body_at about, so its POSITIVE answer "
                            "is unchecked this run.\n");
            failures++;
        } else {
            check("body_at(an override)", (uint32_t)x86_native_body_at(body), 1u,
                  &failures);
        }
        check("body_at(guest data)",
              (uint32_t)x86_native_body_at(SELFTEST_PAGE), 0u, &failures);
    }

    guest_memory_release(SELFTEST_PAGE, 0x1000u);

    if (failures) {
        fprintf(stderr, "[ENGINE] selftest FAILED with %d mismatch(es). The "
                        "engine is not safe to dispatch to.\n", failures);
        return 0;
    }
    fprintf(stderr, "[ENGINE] selftest passed: 6 guest instructions executed "
                    "through the bridge, 10 checks, and the call-out predicate "
                    "answered both ways.\n");
    /*
     * The take set is checked HERE and not at init for the same reason the
     * predicate above is: an address is only classifiable once the modules are
     * mapped and the overrides are resolved. A named address that turns out to
     * be a thunk, an override, or nothing at all stops the run rather than
     * being dropped -- the resulting measurement would be of a different set.
     */
    if (!x2_take_validate()) return 0;
    /* The selftest's own work is not a measurement of the game, and the
       invariants that hold for the game do not hold for it. */
    x2_engine_enter_service();
    return 1;
}
