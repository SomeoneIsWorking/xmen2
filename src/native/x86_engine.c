#include "x86_engine.h"

#include "guest_memory.h"
#include "x86_engine_internal.h"
#include "x86rt.h"
#include "x86rt_native.h"
#include "x86_hotep.h"

#include "cpu.h"
#include "engine.h"
#include "exec.h"
#include "x87.h"

#include <stdio.h>
#include <setjmp.h>
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
   rather than a hang.

   The PROGRAM's entry point is the exception, and the only one: main does not
   return until the process exits, so counting its instructions against a cap
   measures how long the game has been played. Every call it makes is still
   capped. Whether that outermost call is making progress is a question the
   heartbeat and the frame counters answer, and they answer it continuously
   rather than once at 200 million. */
#define ENGINE_STEP_CAP 200000000ULL

static struct {
    X86pEngine selected;
    X86pMem mem;
    int ready;
    int in_service; /* the selftest has passed; what runs now is the game */
    unsigned long calls;
    unsigned long taken;
    unsigned long long insns;
    unsigned long callouts;
    unsigned long deepest;
    unsigned long depth;
    uint32_t program_entry;          /* 0 until the program itself is entered */
    unsigned long setjmps;
    unsigned long longjmps;
} g_engine;

/*
 * The engine's own call stack, for a fault report.
 *
 * A host backtrace stops at x2_engine_call: everything below it is one C loop,
 * so a fault inside interpreted code reads as "somewhere in the engine" and
 * names no guest function at all. The first bisect run proved that -- a
 * SIGSEGV at 0x3 with a [REGS] line naming an import thunk, which was the last
 * body to cross the boundary and had nothing to do with it.
 *
 * A POINTER to each frame's live CPU rather than a copy of its EIP: the value
 * is then always current without a store per interpreted instruction.
 */
#define ENGINE_FRAMES 64
static struct {
    uint32_t entry;
    const X86pCpu *cpu;
} g_frame[ENGINE_FRAMES];

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
    const unsigned available = x86p_engine_bit(kX86pEngineInterpreter);
    const char *request = getenv("X2_ENGINE");
    /* The substrate is NOT offered, and the default is the interpreter. There
       is no translated corpus in this binary any more, so "substrate" would
       name an arm with nothing behind it -- and an engine that resolves to
       something that cannot run a single instruction is worse than a refusal,
       because it fails later and somewhere else. */
    if (!x86p_engine_resolve(request, kX86pEngineInterpreter, available,
                             &g_engine.selected, reason, reason_len))
        return 0;
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

void x2_engine_program_entry(uint32_t addr)
{
    g_engine.program_entry = addr;
}

int x2_engine_call(uint32_t addr, CPU *C)
{
    X86pCpu cpu;
    X86pStepReport report;
    /*
     * volatile because this frame takes a host setjmp below, and a longjmp
     * back into it leaves every non-volatile local indeterminate. `cpu` is
     * exempt: it is rebuilt wholesale from C on the resume path.
     */
    volatile uint32_t entry_esp, return_to;
    volatile uint32_t entry = addr;
    volatile unsigned long long steps = 0;

    if (!g_engine.ready) return 0;

    /*
     * A thread with no TEB cannot run interpreted code correctly, and the
     * failure is not one anybody would recognise: FS-relative accesses become
     * absolute low addresses, which fault as null dereferences somewhere the
     * segment override is nowhere in sight.
     *
     * The substrate refuses this lazily, at the FS access itself
     * (x86_fs_check). The engine cannot -- the check would be inside x86port's
     * effective-address path, once per memory operand -- so it refuses at the
     * boundary instead. Every guest thread in this port is given a TEB by
     * threads.c, so a zero here is a broken thread, not a legitimate one.
     */
    if (g_engine.in_service && !g_fsbase) {
        fprintf(stderr,
                "\n*** engine: the call at 0x%08x (%s) is on a thread with no "
                "TEB (g_fsbase is 0), so every FS-relative access would read "
                "low memory instead.\n",
                addr, named(addr));
        x86_diag_dump();
        abort();
    }

    g_engine.calls++;
    /* The hot-body probe's span: this is where a guest body runs now. */
    if (x86_hotep_armed()) x86_probe_span_push();
    if (++g_engine.depth > g_engine.deepest) g_engine.deepest = g_engine.depth;
    if (g_engine.depth <= ENGINE_FRAMES) {
        g_frame[g_engine.depth - 1].entry = addr;
        g_frame[g_engine.depth - 1].cpu = &cpu;
    }

    x2_engine_to_x86p(C, &cpu);
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
         * GUEST setjmp, taken in the engine's own frame.
         *
         * The import stub cannot do this: it records the guest state and
         * RETURNS, so the host frame longjmp would resume into is gone before
         * it is needed, and it honestly marks the buffer unresumable. That is
         * what killed the whole-module take -- the exe reaches _setjmp3
         * through its IAT, so every setjmp inside interpreted code was
         * unresumable and the first longjmp had nothing to jump to.
         *
         * A generated body solves it by taking the host setjmp inline, and so
         * does this: the engine's run loop is a live host frame for as long as
         * the interpreted function is running, which is exactly the lifetime
         * the guest's jmp_buf is supposed to have. Same table, same
         * x86_setjmp_buf / x86_setjmp_done pair, same reclaim rules -- a
         * second mechanism here would be a second answer to "which buffers are
         * still live".
         */
        if (cpu.eip != entry && x86_setjmp3_thunk(cpu.eip)) {
            /* volatile: written before the setjmp and read after it, across a
               longjmp that makes every other local in this frame
               indeterminate. */
            volatile uint32_t resume = RD32(cpu.reg[kX86pEsp]);
            volatile unsigned long frame_depth = g_engine.depth;
            int rc;
            x2_engine_from_x86p(&cpu, C);
            g_engine.setjmps++;
            rc = setjmp(*x86_setjmp_buf(C));
            x86_setjmp_done(C, rc);
            if (rc) {
                /* Arrived by longjmp. Every engine frame between the jump and
                   this one is gone with the host frames they lived in, so the
                   nesting count has to come back with them; leaving it would
                   make the deepest-nesting figure a record of a stack that no
                   longer existed. */
                g_engine.depth = frame_depth;
                if (!g_engine.longjmps++)
                    fprintf(stderr,
                            "[ENGINE] a longjmp RESUMED into interpreted code "
                            "(guest esp 0x%08x). Reported once; the total is "
                            "in the shutdown report.\n",
                            C->esp);
            }
            x2_engine_to_x86p(C, &cpu);
            cpu.eip = resume;
            continue;
        }
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
        if (cpu.eip != entry && x86_native_body_at(cpu.eip)
            ) {
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
            x2_engine_from_x86p(&cpu, C);
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
            x2_engine_to_x86p(C, &cpu);
            /* The dispatched body emulated its own RET, so the guest ESP it
               returns with is already right. Only EIP is this loop's to
               restore. */
            cpu.eip = ret;
            continue;
        }
        status = x86p_step(&cpu, &g_engine.mem, &report);
        if (status != kX86pStepOk)
            refuse(entry, cpu.eip, x86p_step_status_name(status), &report);
        g_engine.insns++;
        if (++steps > ENGINE_STEP_CAP && entry != g_engine.program_entry)
            refuse(entry, cpu.eip,
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
                entry, named(entry), entry_esp, cpu.reg[kX86pEsp]);
        abort();
    }
    x2_engine_from_x86p(&cpu, C);
    if (g_engine.depth <= ENGINE_FRAMES) g_frame[g_engine.depth - 1].cpu = NULL;
    g_engine.depth--;
    if (x86_hotep_armed()) x86_probe_guest_body_end(entry);
    return 1;
}

void x2_engine_where(void)
{
    unsigned long i;
    if (!g_engine.ready) return;
    if (!g_engine.depth) {
        fprintf(stderr, "[ENGINE] no interpreted call is on the stack, so the "
                        "engine was not executing when this happened.\n");
        return;
    }
    fprintf(stderr, "[ENGINE] %lu interpreted call(s) on the stack, innermost "
                    "last:\n", g_engine.depth);
    for (i = 0; i < g_engine.depth && i < ENGINE_FRAMES; i++) {
        const X86pCpu *c = g_frame[i].cpu;
        fprintf(stderr, "[ENGINE]   0x%08x (%s)", g_frame[i].entry,
                named(g_frame[i].entry));
        if (c)
            fprintf(stderr, " at 0x%08x (%s), esp %08x", c->eip,
                    named(c->eip), c->reg[kX86pEsp]);
        fputc('\n', stderr);
    }
    if (g_engine.depth > ENGINE_FRAMES)
        fprintf(stderr, "[ENGINE]   ... %lu deeper frame(s) not recorded "
                        "(ENGINE_FRAMES is %d).\n",
                g_engine.depth - ENGINE_FRAMES, ENGINE_FRAMES);
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
            "%lu, %lu setjmp(s) taken and %lu longjmp(s) resumed.\n"
            "[ENGINE] A zero call count is a measurement only because the "
            "denominator is beside it: it means the substrate had a body for "
            "every address reached and nothing was taken from it.\n",
            x86p_engine_name(g_engine.selected), g_engine.calls, g_engine.taken,
            g_engine.calls - g_engine.taken, g_engine.insns, g_engine.callouts,
            g_engine.deepest, g_engine.setjmps, g_engine.longjmps);
}

void x2_engine_enter_service(void)
{
    g_engine.in_service = 1;
    g_engine.calls = 0;
    g_engine.taken = 0;
    g_engine.insns = 0;
    g_engine.callouts = 0;
    g_engine.deepest = 0;
    g_engine.setjmps = 0;
    g_engine.longjmps = 0;
}
