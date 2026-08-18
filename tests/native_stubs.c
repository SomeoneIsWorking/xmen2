/*
 * Runtime symbols the STANDALONE native tests need.
 *
 * The override tests link one subsystem file (pad_glyphs.c, xbox_defaults.c,
 * crt.c, ...) on its own, without src/native/x86rt_native.c -- that file pulls
 * in the whole dispatcher, the module tables and the PE loader, which is the
 * opposite of testing one subsystem. So the handful of runtime symbols those
 * subsystems reference are defined here instead.
 *
 * These are stubs, not fakes with behaviour: the write watch is a diagnostic
 * that the tests do not arm, so firing it means the test tripped a diagnostic
 * it never asked for -- it says so and aborts rather than counting silently.
 * x86_register_override records, so a test can assert that the constructor
 * registered what it claims to.
 */
#include "x86rt_native.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* Sampling profiler / guest-watch state the subsystems reference. Never armed
   by a unit test. */
volatile uint32_t g_sample_ep;
uint32_t g_guest_watch_addr;

/* X2_WRITE_WATCH. Unarmed here, so WR32's compare never matches and fire is
   unreachable -- if it is reached, the test's own memory writes have tripped a
   diagnostic and any result after that is suspect. */
volatile uint32_t x2_write_watch_addr;

void x2_write_watch_fire(uint32_t a, uint32_t v)
{
    fprintf(stderr, "native_stubs: the write watch fired at 0x%08x = 0x%08x, "
                    "but no test arms it. Something wrote to the watch "
                    "address, which means this stub is out of step with the "
                    "runtime.\n", a, v);
    abort();
}

/* Override registration. The tests call the override function directly, so
   they do not need dispatch -- but the subsystems register from constructors,
   which run before main. Recorded so a test can check the registration
   happened and named the right module and entry point. */
#define STUB_MAX_OVERRIDES 32
static struct { const char *module; uint32_t ep; } g_reg[STUB_MAX_OVERRIDES];
static int g_nreg;

void x86_register_override(const char *module, uint32_t linked_ep,
                           x86_override_fn fn)
{
    (void)fn;
    if (g_nreg == STUB_MAX_OVERRIDES) {
        fprintf(stderr, "native_stubs: more than %d registration(s); raise "
                        "STUB_MAX_OVERRIDES rather than dropping one.\n",
                STUB_MAX_OVERRIDES);
        abort();
    }
    g_reg[g_nreg].module = module;
    g_reg[g_nreg].ep = linked_ep;
    g_nreg++;
}

int x86_override_count(void) { return g_nreg; }

/* Did the constructor register this (module, entry point)? Reports what it DID
   see when the answer is no, so a miss names the registrations that exist
   rather than just failing. */
int native_stubs_registered(const char *module, uint32_t linked_ep)
{
    int i;
    for (i = 0; i < g_nreg; i++)
        if (g_reg[i].ep == linked_ep && !strcmp(g_reg[i].module, module))
            return 1;
    fprintf(stderr, "native_stubs: %s 0x%08x was NOT registered; the %d "
                    "registration(s) seen were:\n", module, linked_ep, g_nreg);
    for (i = 0; i < g_nreg; i++)
        fprintf(stderr, "    %s 0x%08x\n", g_reg[i].module, g_reg[i].ep);
    return 0;
}
