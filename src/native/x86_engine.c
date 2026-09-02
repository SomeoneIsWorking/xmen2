#include "x86_engine.h"

#include "guest_memory.h"
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

/*
 * Where a function this engine runs returns TO.
 *
 * The substrate's CPU has no EIP because its bodies are C functions: a call
 * returns when the C function returns. An interpreted function returns by
 * popping an address and jumping to it, so it needs a real address to return
 * to -- one that stops the interpreter instead of executing something.
 *
 * A page of INT3 in the low range this port reserves for host-owned objects.
 * That range is claimed at fixed addresses, so this one is stated beside them
 * rather than found by a search that would take a page one of them wants:
 *
 *   0x00080000   this page, the engine's return trampoline
 *   0x00090000   the unbound-import poison page (64 KB, PROT_NONE)
 *   0x000A0000   the thread information block
 *   0x000B0000   the data arena
 *   0x000C0000   the import thunks
 *
 * Two of those were found by taking them: this page sat at 0x000B0000 and the
 * data arena refused, then at 0x00090000 and poison_init refused. Neither
 * shared silently, which is the behaviour that made the collision a one-line
 * fix instead of a corruption hunt.
 *
 * INT3 rather than an unmapped address because the whole reservation is inside
 * X86pMem: a fetch from an unmapped page would segfault the host rather than
 * report a fault this could recognise. INT3 is a MODELLED instruction with a
 * named outcome, so a return and a guest that really did execute an INT3 stay
 * distinguishable.
 */
#define ENGINE_RETURN_PAGE 0x00080000u
#define ENGINE_RETURN_ADDR ENGINE_RETURN_PAGE

/* A run that has executed this many instructions inside ONE call is not
   finishing. Reported with the entry point, so a runaway is a named function
   rather than a hang. */
#define ENGINE_STEP_CAP 200000000ULL

static struct {
    X86pEngine selected;
    X86pMem mem;
    int ready;
    unsigned long calls;
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
    if (g_engine.selected == kX86pEngineSubstrate) return 1;
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
    return 1;
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
    uint32_t entry_esp;
    unsigned long long steps = 0;

    if (!g_engine.ready) return 0;

    g_engine.calls++;
    if (++g_engine.depth > g_engine.deepest) g_engine.deepest = g_engine.depth;

    to_x86p(C, &cpu);
    /* Push the return address the guest's own RET will pop. The substrate's
       x86_guest_call does exactly this, for the same reason: dispatching
       without it leaks guest stack, upward, silently. */
    cpu.reg[kX86pEsp] -= 4u;
    WR32(cpu.reg[kX86pEsp], ENGINE_RETURN_ADDR);
    entry_esp = cpu.reg[kX86pEsp];
    cpu.eip = addr;

    for (;;) {
        X86pStepStatus status;
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
        if (cpu.eip != addr && x86_native_body_at(cpu.eip)) {
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
            if (!x86_native_call_at(target, C))
                refuse(addr, target,
                       "a body x86_native_body_at claimed exists could not be "
                       "called -- the two lookups disagree",
                       NULL);
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

void x2_engine_report(void)
{
    if (!g_engine.ready) {
        fprintf(stderr,
                "[ENGINE] not selected (%s); the substrate ran everything.\n",
                x86p_engine_name(g_engine.selected));
        return;
    }
    fprintf(stderr,
            "[ENGINE] %s: %lu call(s) entered, %llu guest instruction(s) "
            "executed, %lu handed back to the dispatcher, deepest nesting "
            "%lu.\n"
            "[ENGINE] A zero call count is a measurement only because the "
            "denominator is beside it: it means the substrate had a body for "
            "every address reached.\n",
            x86p_engine_name(g_engine.selected), g_engine.calls, g_engine.insns,
            g_engine.callouts, g_engine.deepest);
}

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

    if (!g_engine.ready) return 1; /* nothing selected: nothing to prove */

    if (guest_memory_map_fixed(SELFTEST_PAGE, 0x1000u,
                               PROT_READ | PROT_WRITE) != 0) {
        fprintf(stderr, "[ENGINE] selftest: could not map its own page at "
                        "0x%08x -- the engine is UNVERIFIED for this run.\n",
                SELFTEST_PAGE);
        return 0;
    }
    memcpy(guest_memory_pointer(SELFTEST_PAGE), program, sizeof program);

    /* A stack inside the same page, above the program. The engine pushes a
       return address onto it and the program pushes one word of its own. */
    stack = SELFTEST_PAGE + 0x800u;

    memset(&cpu, 0, sizeof cpu);
    cpu.esp = stack;
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
    /* The selftest's own work is not a measurement of the game, so the run
       counters start from zero AFTER it. Subtracting its call and its six
       instructions was not enough -- the nesting high-water mark stayed at 1,
       so the report said "0 calls, deepest nesting 1", which is not a state
       that can happen. */
    g_engine.calls = 0;
    g_engine.insns = 0;
    g_engine.callouts = 0;
    g_engine.deepest = 0;
    return 1;
}
