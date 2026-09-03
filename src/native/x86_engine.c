#include "x86_engine.h"

#include "guest_memory.h"
#include "x86_engine_internal.h"
#include "x86_engine_frames.h"
#include "x86_engine_jit_diag.h"
#include "x86_hotep.h"
#include "x86rt.h"
#include "x86rt_native.h"

#include "cpu.h"
#include "engine.h"
#include "exec.h"
#include "jit_engine.h"
#include "jit_x64.h"
#include "x87.h"
#include "threads.h"

#include <lucent/cvar_c.h>

#include <setjmp.h>
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

static struct {
  X86pEngine selected;
  X86pMem mem;
  X86pJitEngine *jit;
  int ready;
  int in_service; /* the selftest has passed; what runs now is the game */
  unsigned long calls;
  unsigned long taken;
  unsigned long long insns;
  unsigned long callouts;
  uint32_t program_entry; /* 0 until the program itself is entered */
  unsigned long setjmps;
  unsigned long longjmps;
} g_engine;

/*
 * The decode cache, one for the whole engine.
 *
 * Its entries validate themselves against the guest bytes on every hit (see
 * x86port's decode_cache.h), so a stale entry cannot execute; the interpreter
 * path that uses it is rarely entered by two guest threads at once, and the
 * only cost if it is would be a torn write that the next hit's validation
 * rejects.
 */
static X86pDecodeCache g_decode_cache;

/*
 * The engine's own per-thread call stack, for a fault report and for the
 * intercept checks. x86_engine_frames.c owns it; a host backtrace stops at
 * x2_engine_call, so without this a fault inside interpreted code names no
 * guest function at all.
 */

static int jit_intercept(const X86pCpu *cpu, void *user) {
  (void)user;
  const uint32_t eip = cpu->eip;
  if (__builtin_expect((uint32_t)(eip - 0x00080000u) < 0x50000u, 0)) {
    if (x86_is_thunk(eip) || eip == ENGINE_RETURN_ADDR)
      return 1;
  }
  {
    const EngineFrame *f = engine_frame_top();
    if (f) {
      if (eip == f->return_to && cpu->reg[kX86pEsp] >= f->entry_esp + 4u)
        return 1;
      if (eip != f->entry && x86_native_body_at(eip))
        return 1;
    }
  }
  return 0;
}

/*
 * The pure-EIP subset of jit_intercept, given to the translator so a block is
 * never emitted THROUGH an interception point reached by fall-through rather
 * than by a call. The stack-relative return check is omitted: a RET already
 * ends a block, so the translator never needs it.
 */
static int jit_boundary(uint32_t eip, void *user) {
  (void)user;
  return x86_is_thunk(eip) || eip == ENGINE_RETURN_ADDR ||
         x86_native_body_at(eip) || x86_setjmp3_thunk(eip);
}

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
  unsigned available = x86p_engine_bit(kX86pEngineInterpreter);
  X86pEngine default_engine = kX86pEngineInterpreter;
  if (x86p_jit_available()) {
    available |= x86p_engine_bit(kX86pEngineJit);
    default_engine = kX86pEngineJit;
  }
  /* Layered: compiled default "jit" < x2native-runtime.conf < X2_ENGINE env <
     --set engine=. Resolved once by x2_runtime_config_init before main's
     engine setup; x86p_engine_resolve still maps the string to an engine and
     reports an unavailable request. */
  const char *request = lucent_cvar_text("engine");
  if (!x86p_engine_resolve(request, default_engine, available,
                           &g_engine.selected, reason, reason_len))
    return 0;
  if (!map_return_page(reason, reason_len))
    return 0;
  g_engine.mem.host = (uint8_t *)g_guest_memory_base;
  g_engine.mem.lo = 0;
  g_engine.mem.size = ENGINE_MEM_SIZE;
  if (g_engine.selected == kX86pEngineJit) {
    g_engine.jit = x86p_jit_engine_create(&g_engine.mem, 64u << 20, 65536u,
                                          reason, reason_len);
    if (!g_engine.jit)
      return 0;
    x86p_jit_engine_set_intercept(g_engine.jit, jit_intercept, NULL);
    x86p_jit_engine_set_boundary(g_engine.jit, jit_boundary, NULL);
    x86_engine_jit_diag_configure(g_engine.jit);
  }
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

static const char *named(uint32_t addr) {
  const char *n = x86_native_name_at(addr);
  return n ? n : "unnamed";
}

static void refuse(uint32_t entry, uint32_t eip, const char *what,
                   const X86pStepReport *r) {
  fprintf(stderr,
          "\n*** engine: %s\n"
          "    entry point 0x%08x (%s)\n"
          "    at          0x%08x (%s)\n",
          what, entry, named(entry), eip, named(eip));
  if (r)
    fprintf(stderr, "    instruction %s, %u byte(s)\n", r->mnemonic, r->length);
  if (r && r->status == kX86pStepMemoryFault)
    fprintf(stderr, "    faulting address 0x%08x\n", r->fault_addr);
  x86_diag_dump();
  abort();
}

void x2_engine_program_entry(uint32_t addr) { g_engine.program_entry = addr; }

int x2_engine_call(uint32_t addr, CPU *C) {
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

  if (!g_engine.ready)
    return 0;

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
  if (x86_hotep_armed())
    x86_probe_span_push();
  x2_engine_to_x86p(C, &cpu);
  entry_esp = cpu.reg[kX86pEsp];
  return_to = RD32(entry_esp);
  cpu.eip = addr;
  engine_frame_push(addr, return_to, entry_esp, &cpu);

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
      volatile unsigned long frame_depth = engine_frame_depth();
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
        engine_frame_restore_depth(frame_depth);
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
    if (cpu.eip == return_to && cpu.reg[kX86pEsp] >= entry_esp + 4u)
      break;
    if (cpu.eip == ENGINE_RETURN_ADDR)
      break;
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
    if (x86_is_thunk(cpu.eip) ||
        (cpu.eip != entry && x86_native_body_at(cpu.eip))) {
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
      x2_engine_callout_from_x86p(&cpu, C);
      g_engine.callouts++;
      x86_dispatch(C, target);
      x2_engine_callout_to_x86p(C, &cpu);
      /* The dispatched body emulated its own RET, so the guest ESP it
         returns with is already right. Only EIP is this loop's to
         restore. */
      cpu.eip = ret;
      continue;
    }
    if (g_engine.selected == kX86pEngineJit) {
      char why[192];
      why[0] = '\0';
      /* Slice the JIT and offer the guest lock up between slices: JITted code
         never reaches the X86_ENTER_FN preemption point, so a thread stuck in
         a libCriMovie playback loop would hold the one guest lock forever and
         starve the decoder's feeders (issue #57). No-op with no lock waiter. */
      uint64_t slice = guest_quantum_size();
      if (slice > 200000ULL)
        slice = 200000ULL;
      X86pJitRunStatus st =
          x86p_jit_engine_run(g_engine.jit, &cpu, slice, why, sizeof why);
      if (st != kX86pRunIntercept && st != kX86pRunBudget)
        refuse(entry, cpu.eip, why[0] ? why : x86p_jit_run_status_name(st), NULL);
      guest_quantum();
    } else {
      status = x86p_step_cached(&cpu, &g_engine.mem, &g_decode_cache, &report);
      if (status != kX86pStepOk)
        refuse(entry, cpu.eip, x86p_step_status_name(status), &report);
    }
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
  engine_frame_pop();
  if (x86_hotep_armed())
    x86_probe_guest_body_end(entry);
  return 1;
}

void x2_engine_where(void) {
  if (!g_engine.ready)
    return;
  {
    unsigned long depth = engine_frame_depth(), i;
    const EngineFrame *f;
    if (!depth) {
      fprintf(stderr, "[ENGINE] no interpreted call is on the stack, so the "
                      "engine was not executing when this happened.\n");
      return;
    }
    fprintf(stderr,
            "[ENGINE] %lu interpreted call(s) on the stack, innermost "
            "last:\n",
            depth);
    for (i = 0; (f = engine_frame_at(i)) != NULL; i++) {
      const X86pCpu *c = f->cpu;
      fprintf(stderr, "[ENGINE]   0x%08x (%s)", f->entry, named(f->entry));
      if (c)
        fprintf(stderr, " at 0x%08x (%s), esp %08x", c->eip, named(c->eip),
                c->reg[kX86pEsp]);
      fputc('\n', stderr);
    }
    if (depth > ENGINE_FRAMES_MAX)
      fprintf(stderr,
              "[ENGINE]   ... %lu deeper frame(s) not recorded "
              "(ENGINE_FRAMES_MAX is %d).\n",
              depth - ENGINE_FRAMES_MAX, ENGINE_FRAMES_MAX);
  }
}

void x2_engine_report(void) {
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
          engine_frame_deepest(), g_engine.setjmps, g_engine.longjmps);
  if (g_engine.jit) {
    X86pJitEngineStats js;
    x86p_jit_engine_stats(g_engine.jit, &js);
    fprintf(stderr,
            "[ENGINE] jit: %llu block(s) entered (%llu translated, %llu insns, "
            "%llu via helper), %llu fallback step(s), %llu refusal(s), "
            "%llu flush(es), %zu KB code (%s)\n",
            (unsigned long long)js.blocks_entered,
            (unsigned long long)js.blocks_translated,
            (unsigned long long)js.guest_insns_translated,
            (unsigned long long)js.guest_insns_via_helper,
            (unsigned long long)js.fallback_steps,
            (unsigned long long)js.translate_refusals,
            (unsigned long long)js.cache_flushes,
            (size_t)(js.code_bytes_used / 1024),
            x86p_jit_engine_mechanism());
    x86_engine_jit_diag_report(&js);
  }
  {
    unsigned long long look = x86p_decode_cache_lookups(&g_decode_cache);
    fprintf(stderr,
            "[ENGINE] decode cache: %llu lookup(s) -- %llu hit, %llu cold, "
            "%llu displaced by another address, %llu found the guest bytes "
            "REWRITTEN (%.1f%% hit)\n",
            look, g_decode_cache.hits, g_decode_cache.cold,
            g_decode_cache.collisions, g_decode_cache.rewritten,
            look ? 100.0 * (double)g_decode_cache.hits / (double)look : 0.0);
  }
}

void x2_engine_enter_service(void) {
  g_engine.in_service = 1;
  g_engine.calls = 0;
  g_engine.taken = 0;
  g_engine.insns = 0;
  g_engine.callouts = 0;
  engine_frame_reset_deepest();
  g_engine.setjmps = 0;
  g_engine.longjmps = 0;
  /* The startup decode work is not the game's, and leaving it folded in
     would make the cache look better than it is during play. */
  x86p_decode_cache_reset(&g_decode_cache);
}
