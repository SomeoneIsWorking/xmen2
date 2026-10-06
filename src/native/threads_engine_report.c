/*
 * threads_engine_report.c -- the ENGINE's own view of its threads, read out
 * of guest memory for the shutdown/heartbeat report.
 *
 * Split from threads.c: this is diagnostic introspection of libIGCore's
 * private igThreadManager layout (issue #61), not part of the scheduler that
 * owns the rest of that file. It touches no threads.c static -- only mapped
 * guest memory and the kernel32 TLS hooks -- so it lives on its own.
 */
#include "threads.h"
#include "x2_log.h"
#include "x86rt.h"
#include "x86rt_native.h"

#include <stdint.h>
#include <stdio.h>
#include <string.h>

/* kernel32 owns the TLS arrays; these are the hooks this report reads. */
uint32_t k32_tls_peek(int slot, uint32_t index);
int k32_tls_slot_count(void);

/*
 * The ENGINE's own view of its threads, read out of guest memory.
 *
 * Issue #61: shutdown faults in igThreadManager::userUnregister because
 * igPthreadThreadManager::getCallingThread returned NULL, and that function
 * returns NULL for two different reasons -- an empty list (an ordering
 * problem) or a list nothing matches (an identity problem, which this port's
 * one-host-thread coroutine model makes worth asking about). Those are
 * distinguishable by looking, and guessing between them is how issue #54 went
 * wrong, so this looks.
 *
 * Layout, read out of libIGCore rather than assumed: the singleton pointer
 * lives at 0x1015f438 in the module's LINKED image, the thread array object
 * hangs at manager+8, its count is array+8 and its element pointers array+0x10,
 * and each thread's stored id is thread+0x40 (getCallingThread at 0x10064700
 * compares exactly that against pthread_self).
 */
#define IGCORE_THREADMGR_PTR 0x1015f438u

/*
 * A guest pointer this may dereference, or 0.
 *
 * The first version of this report read mgr+8, then arr+0x10, then each
 * element, trusting the layout. On a run stopped mid-movie the manager held
 * something this walk could not follow and the report took a SIGSEGV -- inside
 * the SHUTDOWN report, which is the one place a diagnostic must not fault,
 * because it takes every other report down with it. A layout read out of a
 * disassembly is a hypothesis, and a diagnostic that bets the process on one
 * is not a diagnostic.
 *
 * So every read is bounds-checked first: the address must lie in the guest
 * heap or inside a mapped module. A pointer that does not is REPORTED, not
 * followed -- and that report is itself a finding, because it says the layout
 * is wrong rather than pretending the list is empty.
 */
static void tm_modules_dump(void) {
  X86Module *m;
  x2_log_info("          the ranges checked were the guest heap and these "
              "modules:\n");
  for (m = x86_modules(); m; m = m->next) {
    uint32_t b = m->base ? *m->base : 0u;
    x2_log_info("            %-20s base 0x%08x size 0x%08x%s\n",
                m->name ? m->name : "(unnamed)", b, m->size,
                b ? "" : "   <- never mapped, so nothing lands in it");
  }
}

/*
 * The pthread handle each guest coroutine holds, read out of Win32 TLS.
 *
 * The engine's vendored pthread_self is TlsGetValue on the key at 0x1015f4d8
 * (FUN_10075400 -> FUN_10075ff0 -> KERNEL32!TlsGetValue, confirmed by walking
 * libIGCore.dll's import table). So the handle getCallingThread compares
 * against is literally one of these words, and printing them per slot says
 * which coroutine the engine would consider the caller.
 *
 * The key is read from guest memory, so a build where it has not been
 * allocated yet says so rather than printing a column of zeroes that look like
 * an answer.
 */
#define IGCORE_PTHREAD_TLS_KEY 0x1015f4d8u

static int tm_readable(uint32_t a);

static void tm_tls_handles(X86Module *core) {
  uint32_t keyslot, keyobj, key;
  int slot, shown = 0;

  keyslot = *core->base + (IGCORE_PTHREAD_TLS_KEY - core->preferred);
  if (!tm_readable(keyslot) || !(keyobj = RD32(keyslot)) ||
      !tm_readable(keyobj)) {
    x2_log_info(
        "          pthread TLS key: not allocated yet (slot 0x%08x), so "
        "no coroutine has a pthread handle to compare.\n",
        keyslot);
    return;
  }
  key = RD32(keyobj); /* FUN_10075ff0 loads [arg] then TlsGetValue */
  x2_log_info(
      "          pthread handles by guest-thread slot (TLS index %u, the "
      "value getCallingThread compares):\n",
      key);
  for (slot = 0; slot < k32_tls_slot_count(); slot++) {
    uint32_t h = k32_tls_peek(slot, key);
    if (!h)
      continue;
    x2_log_info("            slot %-2d handle 0x%08x\n", slot, h);
    shown++;
  }
  if (!shown)
    x2_log_info(
        "            NONE of the %d slots holds a handle -- every "
        "coroutine would allocate a fresh one on its next "
        "pthread_self(), and none of them can match a thread registered "
        "earlier.\n",
        k32_tls_slot_count());
}

static int tm_readable(uint32_t a) {
  uint32_t v;
  /*
   * "Is it MAPPED", not "is it in a range I know about".
   *
   * The range version of this refused a perfectly good pointer: the engine's
   * igThreadManager lives at 0x00a8a098, which is past XMen2.exe's
   * SizeOfImage (0x006744c6, confirmed against the PE) and outside the guest
   * heap, because the engine allocates it from its OWN pool. A range check
   * called that unreadable and the report said the layout must be wrong,
   * which was a wrong conclusion drawn from a correct measurement of the
   * wrong thing. process_vm_readv answers the question actually being asked
   * and still cannot fault.
   */
  if (a < 0x1000u)
    return 0;
  return x86_peek32(a, &v);
}

void guest_engine_thread_report(void) {
  X86Module *m;
  uint32_t slot, mgr, arr, n, elems, i;

  /* Modules register with their FILE name -- "libIGCore.dll", not
     "libIGCore". Matching the bare stem found nothing and said so, which
     read as "the module is not linked" on a build that links it. Compare
     the way the loader does: case-insensitively, extension and all. */
  for (m = x86_modules(); m; m = m->next)
    if (m->name && !strcasecmp(m->name, "libIGCore.dll"))
      break;
  if (!m || !m->base) {
    int n = 0;
    X86Module *k;
    for (k = x86_modules(); k; k = k->next)
      n++;
    x2_log_info("  engine threads: no module named libIGCore.dll among the %d "
                "linked, so the engine's thread manager could not be read AT "
                "ALL. The names present are:",
                n);
    for (k = x86_modules(); k; k = k->next)
      x2_log_info(" %s", k->name ? k->name : "(unnamed)");
    x2_log_info("\n");
    return;
  }
  /*
   * X86Module::base is a POINTER TO the guest base, not the base.
   *
   * Every other user in this codebase writes `*m->base`; this one wrote
   * `(uintptr_t)m->base` and so read the ADDRESS OF the module's base storage.
   * That is a host address, which is why the
   * report announced a module "mapped at 0x563f50c5b1c8, above 4 GB" -- a
   * true statement about the wrong quantity -- and why an earlier run
   * produced a plausible-looking 0x72080600 that was equally meaningless.
   * The two crashes before that came from the same mistake reaching RD32.
   */
  {
    uint32_t imgbase = m->base ? *m->base : 0u;
    if (!imgbase) {
      x2_log_info("  engine threads: libIGCore.dll is registered but has no "
                  "image base, so the host never mapped it. NOTHING was "
                  "read.\n");
      return;
    }
    slot = imgbase + (IGCORE_THREADMGR_PTR - m->preferred);
  }
  if (!tm_readable(slot)) {
    x2_log_info(
        "  engine threads: the singleton slot computes to 0x%08x, which "
        "is not readable guest memory (libIGCore.dll at 0x%08x, linked "
        "for 0x%08x). NOTHING was read.\n",
        slot, *m->base, m->preferred);
    return;
  }
  mgr = RD32(slot);
  if (!mgr) {
    x2_log_info("  engine threads: the igThreadManager singleton is NULL "
                "(slot 0x%08x). Either it was never registered, or "
                "userUnregister already cleared it -- it writes 0 there on its "
                "way out.\n",
                slot);
    return;
  }
  if (!tm_readable(mgr) || !tm_readable(mgr + 8u)) {
    x2_log_info("  engine threads: the singleton at slot 0x%08x holds 0x%08x, "
                "which is in NEITHER the guest heap NOR any mapped module. Not "
                "followed. Either the manager is not what this diagnostic "
                "thinks it is, or the run caught it mid-teardown.\n",
                slot, mgr);
    tm_modules_dump();
    return;
  }
  arr = RD32(mgr + 8u);
  if (!arr) {
    x2_log_info("  engine threads: the manager at 0x%08x holds a NULL thread "
                "array, so getCallingThread cannot match ANYTHING.\n",
                mgr);
    return;
  }
  if (!tm_readable(arr) || !tm_readable(arr + 0x10u)) {
    x2_log_info("  engine threads: manager 0x%08x names a thread array at "
                "0x%08x, which is not readable guest memory. Not followed.\n",
                mgr, arr);
    return;
  }
  n = RD32(arr + 8u);
  elems = RD32(arr + 0x10u);
  /* Printed at zero too, with the addresses that produced it: "the list is
     empty" and "the list was never read" must not look the same. */
  x2_log_info("  engine threads: igThreadManager 0x%08x, array 0x%08x, %u "
              "thread(s) registered%s\n",
              mgr, arr, n,
              n ? ":"
                : " -- EMPTY, which is one of issue #61's two candidates");
  if (n > 64u) {
    x2_log_info("          count %u is not credible; refusing to walk it.\n",
                n);
    return;
  }
  if (n && !tm_readable(elems)) {
    x2_log_info("          the element array at 0x%08x is not readable; the "
                "count above stands but the entries cannot be listed.\n",
                elems);
    return;
  }
  for (i = 0; i < n && elems; i++) {
    uint32_t t;
    if (!tm_readable(elems + i * 4u))
      break;
    t = RD32(elems + i * 4u);
    if (!tm_readable(t) || !tm_readable(t + 0x40u)) {
      x2_log_info("          [%u] thread 0x%08x -- not readable, not "
                  "followed\n",
                  i, t);
      continue;
    }
    x2_log_info("          [%u] thread 0x%08x  id 0x%08x  refcount %u\n", i, t,
                RD32(t + 0x40u), RD32(t + 4u));
  }
  tm_tls_handles(m);
}
