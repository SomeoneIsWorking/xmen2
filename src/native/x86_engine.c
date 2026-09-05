#include "x86_engine.h"

#include "guest_memory.h"
#include "x86_engine_dispatch.h"
#include "x86_engine_intercept.h"
#include "x86_engine_jit_diag.h"
#include "x86_engine_private.h"
#include "x86_guest_call_stack.h"
#include "x86_hotep.h"
#include "x86rt.h"
#include "x86rt_native.h"

#include "cpu.h"
#include "jit_engine.h"
#include "jit_x64.h"
#include "threads.h"
#include "x87.h"

#include <lucent/cvar_c.h>
#include <lucent/log_c.h>

#include <setjmp.h>
#include <stdatomic.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>

/*
 * The guest arena, as x86port sees it.
 * guest_memory reserves the 32-bit space at one host base and maps pages
 * into it; bounds checking is the host's, taking SIGSEGV on unmapped pages.
 */
#define ENGINE_MEM_SIZE 0xFFFFFFFFu

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
  X86pJitEngine *jit;
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
  if (!x86p_jit_available()) {
    snprintf(reason, reason_len,
             "the product requires x86port guest execution on this host; no "
             "supported JIT backend is available");
    return 0;
  }
  if (!map_return_page(reason, reason_len))
    return 0;
  g_engine.mem.host = (uint8_t *)g_guest_memory_base;
  g_engine.mem.lo = 0;
  g_engine.mem.size = ENGINE_MEM_SIZE;
  g_engine.jit = x86p_jit_engine_create(&g_engine.mem, 64u << 20, 65536u,
                                        reason, reason_len);
  if (!g_engine.jit)
    return 0;
  x86p_jit_engine_set_intercept(g_engine.jit, x86_engine_jit_intercept, NULL);
  if (lucent_cvar_flag("jit.inline_dispatch", 1))
    x86p_jit_engine_set_dispatch(g_engine.jit, x86_engine_jit_dispatch, NULL);
  x86p_jit_engine_set_boundary(g_engine.jit, x86_engine_jit_boundary, NULL);
  if (!x86_engine_jit_diag_configure(g_engine.jit, reason, reason_len)) {
    x86p_jit_engine_destroy(g_engine.jit);
    g_engine.jit = NULL;
    return 0;
  }
  g_engine.ready = 1;
  lucent_log_info(
      "engine",
      "runtime JIT ready; guest arena %s, return trampoline at 0x%08x",
      g_guest_memory_base ? "relocated" : "at the host's own addresses",
      ENGINE_RETURN_ADDR);
  return 1;
}

int x2_engine_active(void) { return g_engine.ready; }

const char *x2_engine_name(void) { return "jit"; }

/* ---- the run loop ------------------------------------------------------ */

/* The heartbeat requests a snapshot; only the guest-lock owner reads JIT
 * state. A pending request is reported by the heartbeat if no boundary runs. */
static atomic_int g_live_requested = 1;
int x2_engine_request_live_report(void) {
  return atomic_exchange_explicit(&g_live_requested, 1, memory_order_relaxed);
}
static void report_live_if_requested(void) {
  if (!atomic_load_explicit(&g_live_requested, memory_order_relaxed) ||
      !atomic_exchange_explicit(&g_live_requested, 0, memory_order_relaxed))
    return;
  X86pJitEngineStats js = {0};
  if (g_engine.jit)
    x86p_jit_engine_stats(g_engine.jit, &js);
  lucent_log_info(
      "engine",
      "[HB] JIT: %llu blocks entered, %llu translated (%llu instructions); "
      "%lu native hand-backs; %llu refusals of %llu translation attempts; "
      "%llu cache flushes, %llu bytes code; product fallback unavailable",
      (unsigned long long)js.blocks_entered,
      (unsigned long long)js.blocks_translated,
      (unsigned long long)js.guest_insns_translated, g_engine.callouts,
      (unsigned long long)js.translate_refusals,
      (unsigned long long)(js.blocks_translated + js.translate_refusals),
      (unsigned long long)js.cache_flushes,
      (unsigned long long)js.code_bytes_used);
}

static const char *named(uint32_t addr) {
  const char *n = x86_native_name_at(addr);
  return n ? n : "unnamed";
}

static void refuse(uint32_t entry, uint32_t eip, const char *what) {
  lucent_log_error("engine", "%s; entry point 0x%08x (%s), at 0x%08x (%s)",
                   what, entry, named(entry), eip, named(eip));
  x86_diag_dump();
  abort();
}

void x2_engine_program_entry(uint32_t addr) { g_engine.program_entry = addr; }

void x2_engine_note_callout(void) { g_engine.callouts++; }

int x2_engine_call(uint32_t addr, CPU *C) {
  X86pCpu *cpu = C;
  /*
   * volatile because this frame takes a host setjmp below, and a longjmp
   * back into it leaves every non-volatile local indeterminate. `cpu` points
   * to the caller-owned canonical machine state and is not rebuilt.
   */
  volatile uint32_t entry_esp, return_to;
  volatile uint32_t entry = addr;
  volatile unsigned long long steps = 0;

  if (!g_engine.ready)
    return 0;

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
  entry_esp = cpu->reg[kX86pEsp];
  return_to = RD32(entry_esp);
  cpu->eip = addr;
  X86GuestCallFrame call_frame;
  x86_guest_call_push(&call_frame, cpu, addr, return_to, entry_esp);

  for (;;) {
    report_live_if_requested();
    /*
     * GUEST setjmp, taken in the engine's own frame.
     *
     * The import stub cannot do this: it records the guest state and
     * RETURNS, so the host frame longjmp would resume into is gone before
     * it is needed, and it honestly marks the buffer unresumable. That is
     * what killed the whole-module take -- the exe reaches _setjmp3
     * through its IAT, so every setjmp inside guest code was
     * unresumable and the first longjmp had nothing to jump to.
     *
     * The runtime loop solves it by taking the host setjmp inline: it remains
     * a live host frame for as long as the guest function is running, exactly
     * matching the lifetime the guest's jmp_buf is supposed to have. Same
     * table, same
     * x86_setjmp_buf / x86_setjmp_done pair, same reclaim rules -- a
     * second mechanism here would be a second answer to "which buffers are
     * still live".
     */
    if (cpu->eip != entry && x86_setjmp3_thunk(cpu->eip)) {
      /* The jump-buffer owner restores its saved continuation. A local
       * in this loop is reused by later setjmps, even when volatile. */
      int rc;
      g_engine.setjmps++;
      rc = setjmp(*x86_setjmp_buf(C));
      x86_setjmp_done(C, rc);
      if (rc) {
        /* Arrived by longjmp. Every engine frame between the jump and
           this one is gone with the host frames they lived in, so the
           nesting count has to come back with them; leaving it would
           make the deepest-nesting figure a record of a stack that no
           longer existed. */
        x86_guest_call_restore(&call_frame);
        if (!g_engine.longjmps++)
          lucent_log_info("engine",
                          "a longjmp resumed into guest code (guest esp "
                          "0x%08x); reported once, total in shutdown report",
                          C->reg[kX86pEsp]);
      }
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
    if (cpu->eip == return_to && cpu->reg[kX86pEsp] >= entry_esp + 4u)
      break;
    if (cpu->eip == ENGINE_RETURN_ADDR)
      break;
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
      x86_engine_run_host_at(cpu);
      continue;
    }
    char why[192];
    why[0] = '\0';
    /* Slice the JIT and offer the guest lock up between slices, so a thread
       stuck in a libCriMovie playback loop cannot hold the one guest lock
       forever and starve the decoder's feeders (issue #57). No-op with no lock
       waiter. */
    uint64_t slice = guest_quantum_size();
    if (slice > 200000ULL)
      slice = 200000ULL;
    X86pJitRunStatus st =
        x86p_jit_engine_run(g_engine.jit, cpu, slice, why, sizeof why);
    if (st != kX86pRunIntercept && st != kX86pRunBudget)
      refuse(entry, cpu->eip, why[0] ? why : x86p_jit_run_status_name(st));
    guest_quantum();
    if (++steps > ENGINE_STEP_CAP && entry != g_engine.program_entry)
      refuse(entry, cpu->eip,
             "the call has not returned within the step cap -- it is "
             "not finishing");
  }

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
  if (g_engine.jit) {
    X86pJitEngineStats js;
    x86p_jit_engine_stats(g_engine.jit, &js);
    lucent_log_info(
        "engine",
        "JIT: %llu block(s) entered (%llu translated, %llu instructions), "
        "%llu refusal(s), %llu flush(es), %zu KiB code (%s)",
        (unsigned long long)js.blocks_entered,
        (unsigned long long)js.blocks_translated,
        (unsigned long long)js.guest_insns_translated,
        (unsigned long long)js.translate_refusals,
        (unsigned long long)js.cache_flushes,
        (size_t)(js.code_bytes_used / 1024), x86p_jit_engine_mechanism());
    {
      const X86pJitProfile *prof = x86p_jit_engine_profile(g_engine.jit);
      if (prof && x86p_jit_profile_distinct(prof) > 0u) {
        X86pJitProfileEntry top[40];
        uint32_t n = x86p_jit_profile_top(prof, top, 40u), i;
        uint64_t total = x86p_jit_profile_total_hits(prof);
        lucent_log_info(
            "engine",
            "JIT hot blocks: %u distinct, %llu entries total, %llu key(s) "
            "dropped (table full; tail under-counted), top %u follows",
            x86p_jit_profile_distinct(prof), (unsigned long long)total,
            (unsigned long long)x86p_jit_profile_dropped_keys(prof), n);
        for (i = 0; i < n; i++)
          lucent_log_info("engine", "%2u. 0x%08x %-40s %10llu %5.1f%%", i + 1u,
                          top[i].guest_eip, named(top[i].guest_eip),
                          (unsigned long long)top[i].entries,
                          total ? 100.0 * (double)top[i].entries / (double)total
                                : 0.0);
      }
    }
  }
}

void x2_engine_enter_service(void) {
  g_engine.in_service = 1;
  g_engine.calls = 0;
  g_engine.callouts = 0;
  x86_guest_call_reset_deepest();
  g_engine.setjmps = 0;
  g_engine.longjmps = 0;
}
