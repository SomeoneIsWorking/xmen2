#include "x86_engine.h"

#include "guest_memory.h"
#include "x86_engine_internal.h"
#include "x86_engine_take.h"
#include "x86rt.h"
#include "x86rt_native.h"

#include "cpu.h"
#include "engine.h"
#include "exec.h"
#include "x87.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>

/*
 * The guest arena, as x86port sees it.
 *
 * guest_memory reserves the whole 32-bit space at one host base and maps pages
 * into it, so the mapping x86port needs is the identity over that reservation.
 * The last byte is excluded because X86pMem measures a size in uint32_t and
 * 2^32 does not fit one; nothing maps 0xFFFFFFFF, and an access there would be
 * a wrap the bounds check must refuse anyway.
 *
 * BOUNDS CHECKING IS THE HOST'S, NOT x86port's. Every address inside the
 * reservation is "mapped" as far as X86pMem can tell, and an unmapped page is
 * PROT_NONE, so a stray access takes the port's existing SIGSEGV path
 * (src/x86fault.c) rather than being reported as a guest fault. That is the
 * same behaviour the substrate has today, which is the point: this changes
 * which engine executes an instruction, not what a bad address does.
 */
#define ENGINE_MEM_SIZE 0xFFFFFFFFu

/* A run that has executed this many instructions inside ONE call is not
   finishing. Reported with the entry point, so a runaway is a named function
   rather than a hang. */
#define ENGINE_STEP_CAP 200000000ULL

static struct {
    X86pEngine selected;
    X86pMem mem;
    int ready;
    unsigned long calls;
    unsigned long taken;
    unsigned long long insns;
    unsigned long callouts;
    unsigned long deepest;
    unsigned long depth;
} g_engine;

/* ---- state bridge ------------------------------------------------------ */

/*
 * The six arithmetic flags travel as a materialised EFLAGS word rather than as
 * a lazy tuple.
 *
 * Both models are lazy and neither can express the other's kinds: the
 * substrate has no AF at all, and x86port has three shift kinds where the
 * substrate has one. Translating kind-to-kind would be a second authority on
 * what a flag means, and a disagreement would be invisible -- a wrong CF
 * surfaces as a branch taken differently much later. Materialising is exact
 * for everything both models hold, and it is exact for AF and DF too at the
 * one place this is used: a Win32 call boundary, where the ABI requires DF
 * clear and nothing a compiler emits reads AF across a call.
 */
static void to_x86p(const CPU *C, X86pCpu *out)
{
    int i;
    memset(out, 0, sizeof *out);
    out->reg[kX86pEax] = C->eax;
    out->reg[kX86pEcx] = C->ecx;
    out->reg[kX86pEdx] = C->edx;
    out->reg[kX86pEbx] = C->ebx;
    out->reg[kX86pEsp] = C->esp;
    out->reg[kX86pEbp] = C->ebp;
    out->reg[kX86pEsi] = C->esi;
    out->reg[kX86pEdi] = C->edi;
    x86p_flags_set_explicit(&out->flags, x86_eflags(C));
    out->df = 0;
    x86p_x87_reset(&out->x87);
    out->x87.control = (uint16_t)C->fcw;
    /* C0-C3 only. TOP is merged in on read from the engine's own stack, and
       the exception flags are not modelled on the substrate side. */
    out->x87.status = (uint16_t)(C->fsw & 0x4700u);
    /* Deepest first, so ST(0) ends up on top of the engine's stack in the same
       order the substrate holds it. Anything below `depth` is not live. */
    for (i = C->depth - 1; i >= 0; i--)
        x86p_x87_push(&out->x87, C->st[(C->top + i) & 7]);
    for (i = 0; i < 8; i++) memcpy(out->xmm[i], C->xmm[i], 16);
}

static void from_x86p(const X86pCpu *in, CPU *C)
{
    int depth, i;
    C->eax = in->reg[kX86pEax];
    C->ecx = in->reg[kX86pEcx];
    C->edx = in->reg[kX86pEdx];
    C->ebx = in->reg[kX86pEbx];
    C->esp = in->reg[kX86pEsp];
    C->ebp = in->reg[kX86pEbp];
    C->esi = in->reg[kX86pEsi];
    C->edi = in->reg[kX86pEdi];
    SETFLAGS(C, FK_EXPLICIT, x86p_eflags(&in->flags), 0u, 0u, 4);
    C->fcw = in->x87.control;
    C->fsw = (uint32_t)(x86p_x87_status(&in->x87) & 0x4700u);
    depth = x86p_x87_depth(&in->x87);
    C->top = 0;
    C->depth = 0;
    for (i = depth - 1; i >= 0; i--) {
        long double v = 0.0L;
        x86p_x87_get(&in->x87, i, &v);
        x87_push(C, v);
    }
    for (i = 0; i < 8; i++) memcpy(C->xmm[i], in->xmm[i], 16);
}

/* ---- selection and setup ---------------------------------------------- */

static int map_return_page(char *reason, unsigned reason_len)
{
    void *host;
    if (guest_memory_map_fixed(ENGINE_RETURN_PAGE, 0x1000u,
                               PROT_READ | PROT_WRITE) != 0) {
        snprintf(reason, reason_len,
                 "the engine's return page at 0x%08x is already mapped -- "
                 "something else claimed a range this dispatcher owns",
                 ENGINE_RETURN_PAGE);
        return 0;
    }
    host = guest_memory_pointer(ENGINE_RETURN_PAGE);
    memset(host, 0xCC, 0x1000u); /* INT3, every byte */
    return 1;
}

int x2_engine_init(char *reason, unsigned reason_len)
{
    /*
     * The JIT arm is DELIBERATELY not offered here. x86port has it, and it is
     * verified there; what it does not have is a way to ask this port whether
     * a call target is a statically recompiled body, so a translated block
     * would call into host code with a guest EIP. Offering it and downgrading
     * would make "the JIT is selected" unobservable, which is the exact
     * failure engine.h exists to prevent -- so it is left out of the
     * availability mask, and asking for it is refused by name.
     */
    const unsigned available = x86p_engine_bit(kX86pEngineSubstrate) |
                               x86p_engine_bit(kX86pEngineInterpreter);
    const char *request = getenv("X2_ENGINE");
    if (!x86p_engine_resolve(request, X86P_ENGINE_DEFAULT, available,
                             &g_engine.selected, reason, reason_len))
        return 0;
    /* Still asked on the substrate arm, because the answer there is a
       REFUSAL: X2_ENGINE_TAKE with no engine to hand the bodies to would
       otherwise run the whole game on the substrate while looking like a
       measurement of the engine. */
    if (g_engine.selected == kX86pEngineSubstrate)
        return x2_take_init(0, reason, reason_len);
    if (!map_return_page(reason, reason_len)) return 0;
    /* Not guest_memory_pointer(0): it answers NULL for address 0 by design,
       and the arena base is exactly what X86pMem wants. */
    g_engine.mem.host = (uint8_t *)g_guest_memory_base;
    g_engine.mem.lo = 0;
    g_engine.mem.size = ENGINE_MEM_SIZE;
    g_engine.ready = 1;
    fprintf(stderr,
            "[ENGINE] %s selected; guest arena %s, return trampoline at "
            "0x%08x\n",
            x86p_engine_name(g_engine.selected),
            g_guest_memory_base ? "relocated" : "at the host's own addresses",
            ENGINE_RETURN_ADDR);
    return x2_take_init(1, reason, reason_len);
}

int x2_engine_active(void) { return g_engine.ready; }

const char *x2_engine_name(void) { return x86p_engine_name(g_engine.selected); }

/* ---- the run loop ------------------------------------------------------ */

static const char *named(uint32_t addr)
{
    const char *n = x86_native_name_at(addr);
    return n ? n : "unnamed";
}

static void refuse(uint32_t entry, uint32_t eip, const char *what,
                   const X86pStepReport *r)
{
    fprintf(stderr,
            "\n*** engine: %s\n"
            "    entry point 0x%08x (%s)\n"
            "    at          0x%08x (%s)\n",
            what, entry, named(entry), eip, named(eip));
    if (r)
        fprintf(stderr, "    instruction %s, %u byte(s)\n", r->mnemonic,
                r->length);
    if (r && r->status == kX86pStepMemoryFault)
        fprintf(stderr, "    faulting address 0x%08x\n", r->fault_addr);
    x86_diag_dump();
    abort();
}

int x2_engine_call(uint32_t addr, CPU *C)
{
    X86pCpu cpu;
    X86pStepReport report;
    uint32_t entry_esp, return_to;
    unsigned long long steps = 0;

    if (!g_engine.ready) return 0;

    g_engine.calls++;
    if (++g_engine.depth > g_engine.deepest) g_engine.deepest = g_engine.depth;

    to_x86p(C, &cpu);
    /*
     * THE RETURN ADDRESS IS ALREADY ON THE STACK. Nothing is pushed here.
     *
     * Every caller of this -- x86_dispatch from a recompiled body, and
     * x86_guest_call_args from host code, which writes 0xDEADBEEF there
     * explicitly -- has already pushed the word the callee's RET will pop.
     * This used to push a SECOND one, which is a return address the guest
     * never pops: the function returned to the trampoline correctly, the
     * engine's own stack check passed because its frame balanced, and the
     * caller's return address was still sitting there. It cost 4 bytes of
     * guest stack per taken call and x86_guest_call caught it on the first
     * one -- but only once a body was TAKEN, because until then the seam had
     * never run a function the game called.
     */
    entry_esp = cpu.reg[kX86pEsp];
    return_to = RD32(entry_esp);
    cpu.eip = addr;

    for (;;) {
        X86pStepStatus status;
        /*
         * Left when control reaches the caller's return address with the stack
         * unwound past it. Both halves are needed: the address alone would
         * also match a CALL to it from deeper inside (where ESP is lower), and
         * the stack alone says nothing about where control went.
         *
         * The trampoline page is still mapped and still full of INT3, for the
         * case x86_guest_call_args creates: its 0xDEADBEEF is not a mapped
         * address, so a function that returns somewhere unexpected must land
         * on something that reports rather than on whatever is there.
         */
        if (cpu.eip == return_to && cpu.reg[kX86pEsp] >= entry_esp + 4u) break;
        if (cpu.eip == ENGINE_RETURN_ADDR) break;
        /*
         * A target this dispatcher owns is HOST code -- an import thunk, a
         * native override, or a statically recompiled body -- and walking into
         * it would interpret host memory as x86-32. Hand it back, then resume
         * where its RET would have gone.
         *
         * Checked at every instruction rather than only after a CALL: a guest
         * function is reached by a tail JMP as readily as by a CALL, and an
         * engine that only looked after calls would walk into the body reached
         * the other way. The lookup is x86_native_body_at, which is
         * x86_native_call_at's own lookup with none of its side effects.
         *
         * Not at the ENTRY point, though. Arriving here normally means there
         * was no body -- but the selftest below enters one deliberately, to
         * run the same function both ways and compare, and an entry that
         * handed itself straight back would make that measurement impossible
         * while looking like it worked.
         */
        if (cpu.eip != addr && x86_native_body_at(cpu.eip)
            && !x2_take_has(cpu.eip)) {
            const uint32_t target = cpu.eip;
            /*
             * The return address is read HERE, before the body runs. After it,
             * ESP is past that word by however many argument bytes the callee
             * popped -- a __stdcall body pops its own and a __cdecl body pops
             * none -- so reading it back relative to the returned ESP would
             * name the return address for cdecl and a stack argument for
             * everything else.
             */
            const uint32_t ret = RD32(cpu.reg[kX86pEsp]);
            from_x86p(&cpu, C);
            g_engine.callouts++;
            /*
             * x86_dispatch, not x86_native_call_at.
             *
             * A recompiled body may end in a TAIL JUMP, which it reports by
             * leaving C->tail_target set and returning; only x86_dispatch's
             * loop drains that. Calling the body directly ran the function and
             * silently dropped whatever it jumped to -- a bug this seam had
             * from the day it landed, and one nothing would have found until a
             * taken body called a tail-jumping one and returned to a caller
             * whose work was half done.
             *
             * The lookup above already said there IS a body here, so a "no
             * recompiled body" abort out of x86_dispatch means the two lookups
             * disagree, which is the same fact the removed refusal named.
             */
            x86_dispatch(C, target);
            to_x86p(C, &cpu);
            /* The dispatched body emulated its own RET, so the guest ESP it
               returns with is already right. Only EIP is this loop's to
               restore. */
            cpu.eip = ret;
            continue;
        }
        status = x86p_step(&cpu, &g_engine.mem, &report);
        if (status != kX86pStepOk)
            refuse(addr, cpu.eip, x86p_step_status_name(status), &report);
        g_engine.insns++;
        if (++steps > ENGINE_STEP_CAP)
            refuse(addr, cpu.eip,
                   "the call has not returned within the step cap -- it is "
                   "not finishing",
                   &report);
    }

    /*
     * The guest stack must be at least back past the return address this
     * pushed. Below it means the function returned having popped LESS than its
     * own return address, which shifts every later frame and surfaces as
     * corruption somewhere unrelated -- the same failure the substrate's stack
     * check exists to catch, so it is caught here too rather than trusted.
     */
    if (cpu.reg[kX86pEsp] < entry_esp + 4u) {
        fprintf(stderr,
                "\n*** engine: the call at 0x%08x (%s) returned with the guest "
                "stack below its own return address\n"
                "    entry esp 0x%08x, return esp 0x%08x\n",
                addr, named(addr), entry_esp, cpu.reg[kX86pEsp]);
        abort();
    }
    from_x86p(&cpu, C);
    g_engine.depth--;
    return 1;
}

/*
 * The engine entered because the substrate was made to DECLINE, not because it
 * had nothing.
 *
 * A separate entry point rather than a flag, so the report can keep the two
 * apart: "the corpus could not translate this" and "we took this deliberately
 * to measure it" are different facts about a run, and a single call counter
 * covering both would make a take set that never fired indistinguishable from
 * a corpus with holes.
 */
int x2_engine_call_taken(uint32_t addr, CPU *C)
{
    if (!x2_engine_call(addr, C)) return 0;
    g_engine.taken++;
    return 1;
}

void x2_engine_report(void)
{
    if (!g_engine.ready) {
        fprintf(stderr,
                "[ENGINE] not selected (%s); the substrate ran everything.\n",
                x86p_engine_name(g_engine.selected));
        return;
    }

    fprintf(stderr,
            "[ENGINE] %s: %lu call(s) entered (%lu of them TAKEN from the "
            "substrate, %lu because it had no body), %llu guest instruction(s) "
            "executed, %lu handed back to the dispatcher, deepest nesting "
            "%lu.\n"
            "[ENGINE] A zero call count is a measurement only because the "
            "denominator is beside it: it means the substrate had a body for "
            "every address reached and nothing was taken from it.\n",
            x86p_engine_name(g_engine.selected), g_engine.calls, g_engine.taken,
            g_engine.calls - g_engine.taken, g_engine.insns, g_engine.callouts,
            g_engine.deepest);
    x2_take_report();
}

void x2_engine_forget_run(void)
{
    g_engine.calls = 0;
    g_engine.taken = 0;
    g_engine.insns = 0;
    g_engine.callouts = 0;
    g_engine.deepest = 0;
}
