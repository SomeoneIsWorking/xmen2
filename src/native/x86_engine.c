#include "x86_engine.h"

#include "guest_memory.h"
#include "override_leaf.h"
#include "platform_mman.h"
#include "x2_log.h"
#include "x86_engine_diagnostic.h"
#include "x86_engine_dispatch.h"
#include "x86_engine_intercept.h"
#include "x86_engine_jit_pool.h"
#include "x86_engine_private.h"
#include "x86_engine_report.h"
#include "x86_engine_x87_census.h"
#include "x86_engine_x87_precision.h"
#include "x86_guest_call_stack.h"
#include "x86_hotep.h"
#include "x86rt.h"
#include "x86rt_native.h"

#include "cpu.h"
#include "jit_engine.h"
#include "jit_x64.h"
#include "threads.h"
#include "x87.h"

#include <lucent/log_c.h>

#include <setjmp.h>
#include <stdatomic.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>

/*
 * Step cap for one call. The PROGRAM's entry point is exempt: main does not
 * return until exit(), so counting against a cap would kill long runs.
 */
#define ENGINE_STEP_CAP 200000000ULL

/* Set by the title thread owner; copied into x86port's canonical context at
   each guest-call entry. Guest instructions then use only these state fields.
 */
extern __thread uint32_t g_fsbase, g_gsbase;

static struct {
  X86pMem mem;
  X86EngineJitPool *jit;
  int ready;
  int in_service; /* the selftest has passed; what runs now is the game */
  unsigned long calls;
  unsigned long callouts;
  uint32_t program_entry; /* 0 until the program itself is entered */
  unsigned long setjmps;
  unsigned long longjmps;
} g_engine;

/*
 * The engine's own per-thread call stack, for a fault report and for the
 * intercept checks. x86_guest_call_stack.c owns it; a host backtrace stops at
 * x2_engine_call, so without this a fault inside translated guest code names no
 * guest function at all. The interception predicates handed to x86port's JIT
 * live in x86_engine_intercept.c.
 */

/* ---- selection and setup ---------------------------------------------- */

static int map_return_page(char *reason, unsigned reason_len) {
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

int x2_engine_init(char *reason, unsigned reason_len) {
  x86_engine_diagnostic_install();
  if (!x86p_jit_available()) {
    snprintf(reason, reason_len,
             "the product requires x86port guest execution on this host; no "
             "supported JIT backend is available");
    return 0;
  }
  if (!map_return_page(reason, reason_len))
    return 0;
  guest_memory_set_remap_observer(x2_engine_invalidate_memory);
  {
    /* The page table the memory owner keeps is handed to x86port as-is, so the
       two spellings of "readable" and "writable" have to be the same bits. */
    const GuestMemoryWindow window = guest_memory_window();
    _Static_assert(PROT_READ == kX86pMemRead && PROT_WRITE == kX86pMemWrite,
                   "guest_memory's page permissions are x86port's");
    g_engine.mem.host = window.host;
    g_engine.mem.lo = 0;
    g_engine.mem.size = window.size;
    g_engine.mem.perms = window.perms;
    g_engine.mem.page_shift = window.page_shift;
    g_engine.mem.guard_above = window.guard_above;
  }
  g_engine.jit = x86_engine_jit_pool_create(&g_engine.mem, reason, reason_len);
  if (!g_engine.jit)
    return 0;
  g_engine.ready = 1;
  lucent_log_info(
      "engine",
      "runtime JIT ready; guest arena %s, %s, return trampoline at 0x%08x",
      g_engine.mem.perms    ? "a window with its own page permissions"
      : g_guest_memory_base ? "relocated"
                            : "at the host's own addresses",
      g_engine.mem.guard_above ? "accesses unchecked under a guard page at 4 GB"
                               : "every access bounds-checked",
      ENGINE_RETURN_ADDR);
  return 1;
}

void x2_engine_invalidate_memory(uint32_t address, uint32_t size) {
  if (!g_engine.jit || !size)
    return;
  char reason[160] = {0};
  if (!x86_engine_jit_pool_invalidate(g_engine.jit, address, size, reason,
                                      sizeof reason)) {
    x2_log_error("engine: failed to invalidate guest mapping: %s\n", reason);
    abort();
  }
}

void x2_engine_detach_thread(void) {
  if (g_engine.jit)
    x86_engine_jit_pool_detach_current(g_engine.jit);
}

int x2_engine_active(void) { return g_engine.ready; }

const char *x2_engine_name(void) { return "jit"; }

/* ---- the run loop ------------------------------------------------------ */

static const char *named(uint32_t addr) {
  const char *n = x86_native_name_at(addr);
  return n ? n : "unnamed";
}

static void refuse(uint32_t entry, const CPU *cpu, const char *what) {
  uint32_t stack[16] = {0};
  int stack_readable =
      guest_memory_is_readable(cpu->reg[kX86pEsp], sizeof stack);
  if (stack_readable)
    guest_memory_read(cpu->reg[kX86pEsp], stack, sizeof stack);
  lucent_log_error("engine", "%s; entry point 0x%08x (%s), at 0x%08x (%s)",
                   what, entry, named(entry), cpu->eip, named(cpu->eip));
  lucent_log_error("engine",
                   "guest registers: eax=%08x ecx=%08x edx=%08x ebx=%08x "
                   "esp=%08x ebp=%08x esi=%08x edi=%08x",
                   cpu->reg[kX86pEax], cpu->reg[kX86pEcx], cpu->reg[kX86pEdx],
                   cpu->reg[kX86pEbx], cpu->reg[kX86pEsp], cpu->reg[kX86pEbp],
                   cpu->reg[kX86pEsi], cpu->reg[kX86pEdi]);
  lucent_log_error("engine",
                   "guest stack at %08x (readable=%d), 16 words: "
                   "%08x %08x %08x %08x %08x %08x %08x %08x "
                   "%08x %08x %08x %08x %08x %08x %08x %08x",
                   cpu->reg[kX86pEsp], stack_readable, stack[0], stack[1],
                   stack[2], stack[3], stack[4], stack[5], stack[6], stack[7],
                   stack[8], stack[9], stack[10], stack[11], stack[12],
                   stack[13], stack[14], stack[15]);
  x86_diag_dump();
  abort();
}

void x2_engine_program_entry(uint32_t addr) { g_engine.program_entry = addr; }

void x2_engine_note_callout(void) { g_engine.callouts++; }

/* One guest call, as the loop that runs it needs it. */
typedef struct EngineRun {
  X86pCpu *cpu;
  uint32_t entry;
  uint32_t entry_esp;
  uint32_t return_to;
  unsigned long long steps;
  X86GuestCallFrame *frame;
} EngineRun;

typedef enum EngineRunOutcome {
  kEngineRunReturned, /* control reached the caller's return address */
  kEngineRunSetjmp    /* the guest reached _setjmp3 */
} EngineRunOutcome;

/*
 * Run the call until it returns or the guest reaches _setjmp3, whose host
 * setjmp only run_call's frame may take.
 *
 * THIS FUNCTION HOLDS NO setjmp, and that is why it is separate. Emscripten's
 * setjmp support routes every call out of a function that holds one through a
 * JavaScript invoke wrapper, and with the loop inside x2_engine_call that was
 * the JIT run, the host bodies and every check below, several crossings into
 * JavaScript per slice of every guest call. run_call keeps even the one
 * crossing per call out of the common case: only a call whose guest reaches
 * _setjmp3 enters the frame that holds one.
 */
static EngineRunOutcome run_guest(volatile EngineRun *run) {
  X86pCpu *cpu = run->cpu;
  const uint32_t entry = run->entry;
  X86GuestCallFrame *call_frame = run->frame;
  X86pJitEngine *jit;
  for (;;) {
    x86_engine_report_live_if_requested(g_engine.jit, g_engine.callouts);
    if (cpu->eip != entry && x86_setjmp3_thunk(cpu->eip))
      return kEngineRunSetjmp;
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
    if (cpu->eip == run->return_to && cpu->reg[kX86pEsp] >= run->entry_esp + 4u)
      return kEngineRunReturned;
    if (cpu->eip == ENGINE_RETURN_ADDR)
      return kEngineRunReturned;
    /*
     * A target this dispatcher owns is HOST code -- an import thunk, a
     * native override, or another native callout -- and walking into it would
     * execute host memory as x86-32. Hand it back, then resume
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
    if (x86_engine_host_body_at(cpu->eip, entry)) {
      x86_engine_run_host_at(cpu, call_frame);
      continue;
    }
    /* The runtime's refusals carry their denominators -- how many modules were
       live, of how many, published and released. At 192 that sentence was cut
       off exactly where the numbers start. */
    char why[512];
    why[0] = '\0';
    jit = x86_engine_jit_pool_current(g_engine.jit, why, sizeof why);
    if (!jit)
      refuse(entry, cpu, why);
    /* Slice the JIT and offer the guest lock up between slices, so a thread
       stuck in a libCriMovie playback loop cannot hold the one guest lock
       forever and starve the decoder's feeders (issue #57). No-op with no lock
       waiter. */
    uint64_t slice = guest_quantum_size();
    if (slice > 200000ULL)
      slice = 200000ULL;
    X86pJitRunStatus st =
        x86p_jit_engine_run(jit, cpu, call_frame, slice, why, sizeof why);
    x86_engine_jit_pool_publish_stats(g_engine.jit, jit);
    if (st != kX86pRunIntercept && st != kX86pRunBudget)
      refuse(entry, cpu, why[0] ? why : x86p_jit_run_status_name(st));
    guest_quantum();
    if (++run->steps > ENGINE_STEP_CAP && entry != g_engine.program_entry)
      refuse(entry, cpu,
             "the call has not returned within the step cap -- it is "
             "not finishing");
  }
}

/*
 * The call's run, with the GUEST setjmp taken in this frame.
 *
 * The import stub cannot do this: it records the guest state and RETURNS, so
 * the host frame longjmp would resume into is gone before it is needed, and it
 * honestly marks the buffer unresumable. That is what killed the whole-module
 * take -- the exe reaches _setjmp3 through its IAT, so every setjmp inside
 * guest code was unresumable and the first longjmp had nothing to jump to.
 *
 * This frame is live for as long as the guest function runs, exactly the
 * lifetime the guest's jmp_buf is supposed to have, and run_guest continues the
 * call beneath it. Same table, same x86_setjmp_buf / x86_setjmp_done pair,
 * same reclaim rules -- a second mechanism here would be a second answer to
 * "which buffers are still live".
 */
__attribute__((noinline)) static void run_setjmps(volatile EngineRun *run) {
  X86pCpu *cpu = run->cpu;
  do {
    /* The jump-buffer owner restores its saved continuation. */
    int rc;
    g_engine.setjmps++;
    rc = setjmp(*x86_setjmp_buf(cpu));
    x86_setjmp_done(cpu, rc);
    if (rc) {
      /* Arrived by longjmp. Every engine frame between the jump and this
         one is gone with the host frames they lived in, so the nesting count
         has to come back with them; leaving it would make the
         deepest-nesting figure a record of a stack that no longer existed. */
      x86_guest_call_restore(run->frame);
      if (!g_engine.longjmps++)
        lucent_log_info("engine",
                        "a longjmp resumed into guest code (guest esp "
                        "0x%08x); reported once, total in shutdown report",
                        cpu->reg[kX86pEsp]);
    }
  } while (run_guest(run) == kEngineRunSetjmp);
}

/* noinline above keeps the setjmp out of this frame and x2_engine_call's. */
static void run_call(volatile EngineRun *run) {
  if (run_guest(run) == kEngineRunSetjmp)
    run_setjmps(run);
}

int x2_engine_call(uint32_t addr, CPU *C) {
  return x2_engine_resume(addr, C, C->reg[kX86pEsp]);
}

int x2_engine_resume(uint32_t addr, CPU *C, uint32_t frame_esp) {
  X86pCpu *cpu = C;
  x86_override_leaf_forbid("called guest code");

  if (!g_engine.ready)
    return 0;
  /*
   * Every CPU that runs guest code passes through here, and one that does not
   * performs no x87 arithmetic, so this is where the x87 unit's policy is
   * attached: the optional census and the arithmetic's precision. Putting them
   * in cpu_reset would have been one line
   * fewer and would have given a header included almost everywhere a link
   * dependency on the engine.
   */
  x86_engine_x87_census_attach(cpu);
  x86_engine_x87_precision_attach(cpu);

  /*
   * A thread with no TEB cannot run guest code correctly, and the
   * failure is not one anybody would recognise: FS-relative accesses become
   * absolute low addresses, which fault as null dereferences somewhere the
   * segment override is nowhere in sight.
   *
   * A per-access check would sit inside x86port's
   * effective-address path, once per memory operand -- so it refuses at the
   * boundary instead. Every guest thread in this port is given a TEB by
   * threads.c, so a zero here is a broken thread, not a legitimate one.
   */
  if (g_engine.in_service && !g_fsbase) {
    lucent_log_error("engine",
                     "call at 0x%08x (%s) is on a thread with no TEB "
                     "(g_fsbase is 0), so every FS-relative access would "
                     "read low memory instead",
                     addr, named(addr));
    x86_diag_dump();
    abort();
  }

  g_engine.calls++;
  /* The hot-body probe's span: this is where a guest body runs now. */
  if (x86_hotep_armed())
    x86_probe_span_push();
  cpu->fs_base = g_fsbase;
  cpu->gs_base = g_gsbase;
  const uint32_t entry = addr;
  const uint32_t entry_esp = frame_esp;
  const uint32_t return_to = RD32(entry_esp);
  cpu->eip = addr;
  X86GuestCallFrame call_frame;
  x86_guest_call_push(&call_frame, cpu, addr, return_to, entry_esp);
  /* volatile: run_call takes a host setjmp, and a longjmp back into it
     leaves a non-volatile object that changed since indeterminate. */
  volatile EngineRun run = {cpu, entry, entry_esp, return_to, 0u, &call_frame};
  run_call(&run);

  /*
   * The guest stack must be at least back past the return address this
   * pushed. Below it means the function returned having popped LESS than its
   * own return address, which shifts every later frame and surfaces as
   * corruption somewhere unrelated, so it is caught here rather than trusted.
   */
  if (cpu->reg[kX86pEsp] < entry_esp + 4u) {
    lucent_log_error("engine",
                     "call at 0x%08x (%s) returned with the guest stack below "
                     "its own return address: entry esp 0x%08x, return esp "
                     "0x%08x",
                     entry, named(entry), entry_esp, cpu->reg[kX86pEsp]);
    abort();
  }
  x86_guest_call_pop(&call_frame);
  if (x86_hotep_armed())
    x86_probe_guest_body_end(entry);
  return 1;
}

void x2_engine_where(void) {
  if (!g_engine.ready)
    return;
  {
    unsigned long depth = x86_guest_call_depth();
    const X86GuestCallFrame *f;
    if (!depth) {
      lucent_log_info("engine", "no translated guest call is on the stack; "
                                "the runtime was not executing");
      return;
    }
    lucent_log_info("engine",
                    "%lu translated guest call(s) on the stack, innermost last",
                    depth);
    for (f = x86_guest_call_top(); f != NULL; f = f->previous) {
      const X86pCpu *c = f->cpu;
      if (c)
        lucent_log_info(
            "engine", "frame 0x%08x (%s), at 0x%08x (%s), esp 0x%08x", f->entry,
            named(f->entry), c->eip, named(c->eip), c->reg[kX86pEsp]);
      else
        lucent_log_info("engine", "frame 0x%08x (%s), CPU state unavailable",
                        f->entry, named(f->entry));
    }
  }
  /* A frame's eip is where its CPU copy last stood, which for a fault inside
     translation is the address being translated, not the block that went
     there. The main thread's engine remembers the last block it entered. */
  {
    uint32_t last = x86p_jit_engine_last_block_entry(
        x86_engine_jit_pool_primary(g_engine.jit));
    lucent_log_info("engine",
                    "the main thread's engine last entered block 0x%08x (%s)",
                    last, named(last));
  }
}

void x2_engine_report(void) {
  if (!g_engine.ready) {
    lucent_log_warn("engine", "runtime JIT was not initialized");
    return;
  }

  lucent_log_info("engine",
                  "runtime JIT: %lu call(s) entered, %lu handed back to the "
                  "dispatcher, deepest nesting %lu, %lu setjmp(s), %lu "
                  "longjmp(s); zero calls means the runtime boundary was not "
                  "reached",
                  g_engine.calls, g_engine.callouts, x86_guest_call_deepest(),
                  g_engine.setjmps, g_engine.longjmps);
  x86_engine_report_jit_totals(g_engine.jit);
}

void x2_engine_enter_service(void) {
  g_engine.in_service = 1;
  g_engine.calls = 0;
  g_engine.callouts = 0;
  x86_guest_call_reset_deepest();
  g_engine.setjmps = 0;
  g_engine.longjmps = 0;
}
