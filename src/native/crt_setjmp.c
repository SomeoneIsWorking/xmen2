#include "x2_log.h"
/*
 * crt_setjmp.c -- the guest's setjmp and longjmp.
 *
 * Its own file because it is the one CRT area with a HOST-side invariant: a
 * jmp_buf is only usable while the host frame that recorded it is alive, and
 * the whole design here is about which of the three ways a setjmp can be taken
 * can honour that. See the note below.
 */
#include "crt_internal.h"
#include "guest_heap.h"
#include "guest_memory.h"
#include "x86rt.h"
#include "x86rt_native.h"

#include <setjmp.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* ---- setjmp / longjmp --------------------------------------------------
 *
 * The host setjmp is NOT taken here. The guest execution boundary records it
 * inline (see x86rt.h), because a host longjmp resumes into a frame
 * that has to still be alive and an import stub's frame is dead the moment it
 * returns. This file owns the two halves the guest runtime calls, and the
 * longjmp import that jumps back into them.
 *
 * The guest's jmp_buf pointer is the key: _setjmp3's first argument. Its
 * CONTENTS are ours -- nothing in the guest reads MSVC's layout, and writing a
 * fake one would claim a fidelity this does not have -- but its ADDRESS is how
 * longjmp finds the matching host buffer.
 */
/*
 * A slot is taken per DISTINCT jmp_buf address and was never given back, which
 * is not a table that is too small -- it is a table that leaks. The game calls
 * one function per resource load that allocates an object, takes a setjmp on a
 * jmp_buf INSIDE that object, and frees it again; sixteen loads exhausted the
 * table with sixteen buffers whose frames had all long since returned (they
 * were all recorded at the same guest ESP, which is what says they were
 * sequential and not nested).
 *
 * So slots are RECLAIMED, and the rule has to be one that cannot free a buffer
 * the guest may still jump to. Exactly ONE fact is definitive: a jmp_buf in the
 * guest heap whose block is no longer allocated cannot be jumped to, because
 * the object holding it is gone.
 *
 * There WAS a second rule -- "a jmp_buf below the current guest stack pointer
 * is in popped space" -- and the real run REFUTED it: it reclaimed the buffer
 * at 0x700ff638 taken by FUN_006460d1, and the guest then longjmp'd to it from
 * 0x0064608d, inside that same function. A stack address below ESP is not
 * evidence that the frame owning it has returned; the reasoning was plausible
 * and wrong, and it only showed up because the rule was run against the game
 * rather than against the case that was built to match it.
 *
 * Anything not provably dead is kept. If every slot is live the table GROWS
 * rather than refusing, because a legitimately deep nest is not an error -- but
 * it says so, once, because silent growth is how a leak comes back unnoticed.
 */
#define JMP_SLOTS 16
#define JMP_SLOTS_MAX 4096

typedef struct {
  uint32_t env;
  CPU regs;    /* the guest register file at the setjmp */
  jmp_buf buf; /* the host frame to resume into */
  uint32_t caller;
  int used;
  int resumable; /* 0 for a buffer recorded by the stub below */
} JmpSlot;

static JmpSlot *g_jmp;
static int g_jmp_cap;
static unsigned long g_jmp_reclaimed, g_jmp_taken;

/* Which slot the in-flight longjmp is jumping through. Set immediately before
   the host longjmp and cleared by the setjmp side that catches it, so the
   resumed body restores the right register file even when a function holds
   more than one jmp_buf. */
static int g_jmp_active = -1;
static unsigned long g_jmp_resumes;

void x86_setjmp_report(void) {
  if (g_jmp_resumes)
    x2_log_error("crt: %lu longjmp(s) resumed into a guest-call frame.\n",
                 g_jmp_resumes);
  if (g_jmp_taken)
    x2_log_error("crt: %lu setjmp buffer(s) recorded, %lu slot(s) "
                 "reclaimed, %d slot(s) in the table.\n",
                 g_jmp_taken, g_jmp_reclaimed, g_jmp_cap);
}

int x86_setjmp_live(void) {
  int i, n = 0;
  for (i = 0; i < g_jmp_cap; i++)
    if (g_jmp[i].used)
      n++;
  return n;
}

/*
 * Give back every slot whose buffer provably cannot be jumped to any more.
 * Returns how many, so the caller can tell "reclaimed nothing" from "reclaimed
 * everything" -- they are the same table state afterwards but completely
 * different situations.
 */
int x86_setjmp_reclaim(void) {
  int i, n = 0;
  uint32_t base, size;
  for (i = 0; i < g_jmp_cap; i++) {
    uint32_t env = g_jmp[i].env;
    int dead;
    if (!g_jmp[i].used)
      continue;
    if (!env)
      dead = 1; /* nothing can name it */
    else if (guest_heap_contains(env, &base, &size))
      dead = !guest_heap_addr_is_live(env);
    else
      dead = 0; /* not provably anything */
    if (dead) {
      g_jmp[i].used = 0;
      n++;
    }
  }
  g_jmp_reclaimed += (unsigned long)n;
  return n;
}

/* Named so the two halves of a setjmp/longjmp pair can be attributed. Without
   it the report says only that the guest unwound, which is the one thing the
   reader already knows. */
const char *x86_native_name_at(uint32_t addr);
void x86_diag_dump(void);

static void say_where(const char *what, uint32_t ret_addr) {
  const char *nm = x86_native_name_at(ret_addr);
  x2_log_error("    %s was called from 0x%08x%s%s\n", what, ret_addr,
               nm ? " -- " : " (in no guest body this host can name)",
               nm ? nm : "");
}

static void jmp_dump(const char *why, uint32_t env, uint32_t esp) {
  int i;
  /*
   * The table says WHAT it is holding, because the count on its own is the one
   * fact the reader already has. Which buffers, where they sit relative to the
   * current stack pointer, and who took each one is the difference between
   * "the guest really does have this many live frames" and "slots are never
   * reclaimed" -- and it was that difference this printed out.
   */
  x2_log_error("crt: %s. %d setjmp slot(s), all live; the guest ESP is now "
               "0x%08x and the buffer being recorded is at 0x%08x:\n",
               why, g_jmp_cap, esp, env);
  for (i = 0; i < g_jmp_cap && i < 32; i++) {
    const char *nm = x86_native_name_at(g_jmp[i].caller);
    x2_log_error("    [%2d] env 0x%08x  %-14s esp was 0x%08x  taken "
                 "from 0x%08x %s%s\n",
                 i, g_jmp[i].env,
                 g_jmp[i].env < esp ? "(popped stack)" : "(live or heap)",
                 g_jmp[i].regs.reg[kX86pEsp], g_jmp[i].caller,
                 nm ? nm : "(unnamed)",
                 g_jmp[i].resumable ? "" : "  [not resumable]");
  }
  if (g_jmp_cap > 32)
    x2_log_error("    ... and %d more.\n", g_jmp_cap - 32);
}

static int jmp_slot_for(uint32_t env, uint32_t esp) {
  int i, free_slot = -1, pass;

  if (!g_jmp) {
    g_jmp = (JmpSlot *)calloc(JMP_SLOTS, sizeof *g_jmp);
    if (!g_jmp) {
      x2_log_error("crt: out of memory\n");
      abort();
    }
    g_jmp_cap = JMP_SLOTS;
  }
  g_jmp_taken++;
  for (pass = 0; pass < 3; pass++) {
    for (i = 0; i < g_jmp_cap; i++) {
      if (g_jmp[i].used && g_jmp[i].env == env)
        return i;
      if (free_slot < 0 && !g_jmp[i].used)
        free_slot = i;
    }
    if (free_slot >= 0)
      return free_slot;
    /* Pass 0 found the table full: reclaim what provably cannot be jumped
       to, and only if that frees nothing is the table really too small. */
    if (pass == 0 && x86_setjmp_reclaim() > 0)
      continue;
    if (g_jmp_cap >= JMP_SLOTS_MAX) {
      jmp_dump("the setjmp table is at its maximum and nothing in it "
               "could be reclaimed",
               env, esp);
      abort();
    }
    {
      int cap = g_jmp_cap * 2;
      JmpSlot *p = (JmpSlot *)realloc(g_jmp, (size_t)cap * sizeof *p);
      if (!p) {
        x2_log_error("crt: out of memory\n");
        abort();
      }
      memset(p + g_jmp_cap, 0, (size_t)(cap - g_jmp_cap) * sizeof *p);
      g_jmp = p;
      /* Once, and by name: a table that grows silently is how a leak
         comes back without anyone noticing it came back. */
      if (g_jmp_cap == JMP_SLOTS)
        jmp_dump("every setjmp slot is still live, so the table is "
                 "GROWING (reported once)",
                 env, esp);
      g_jmp_cap = cap;
    }
  }
  x2_log_error("crt: the setjmp table could neither find, reclaim nor "
               "grow a slot. This is a bug in jmp_slot_for.\n");
  abort();
}

jmp_buf *x86_setjmp_buf(CPU *C) {
  /* ESP points at the return address the guest call just pushed, so
     _setjmp3's first argument -- the guest jmp_buf -- is the next word. */
  uint32_t env = RD32(C->reg[kX86pEsp] + 4u);
  int i = jmp_slot_for(env, C->reg[kX86pEsp]);

  g_jmp[i].env = env;
  g_jmp[i].regs = *C;
  g_jmp[i].caller = RD32(C->reg[kX86pEsp]);
  g_jmp[i].used = 1;
  g_jmp[i].resumable = 1;
  /* A marker rather than MSVC's layout: visible in a memory dump, and it
     cannot be mistaken for a real jmp_buf by anything reading one. */
  if (env)
    WR32(env, 0x53544F50u); /* "STOP" */
  return &g_jmp[i].buf;
}

void x86_setjmp_done(CPU *C, int rc) {
  uint32_t resume;
  if (rc) {
    /*
     * Arrived by longjmp. The register file is restored from the snapshot
     * -- including ESP, which is what makes the guest's stack the one it
     * had at the setjmp rather than the deeper one it was unwound from.
     *
     * Which snapshot: the one longjmp jumped through, which it recorded in
     * g_jmp_active. Searching by anything else would be guessing, and two
     * setjmps in the same function would make the guess wrong.
     */
    if (g_jmp_active < 0) {
      x2_log_error("crt: a setjmp resumed with rc=%d but no longjmp "
                   "recorded which buffer it came through. The guest "
                   "register file cannot be restored.\n",
                   rc);
      abort();
    }
    resume = g_jmp[g_jmp_active].caller;
    *C = g_jmp[g_jmp_active].regs;
    g_jmp_active = -1;
    /*
     * Said once. The mechanism either works or the run dies somewhere
     * unrelated, and those two look identical without a line saying the
     * resume happened -- which is exactly how a run that got further for
     * an unrelated reason would be read as this working.
     */
    if (!g_jmp_resumes++)
      x2_log_error("crt: longjmp RESUMED into a guest-call frame "
                   "(rc=%d, guest esp restored to 0x%08x). Reported "
                   "once; the total is printed at exit.\n",
                   rc, C->reg[kX86pEsp]);
  }
  if (!rc)
    resume = RD32(C->reg[kX86pEsp]);
  C->eip = resume;
  C->reg[kX86pEax] = (uint32_t)rc;
  C->reg[kX86pEsp] += 4u; /* __cdecl: the return address */
}

/*
 * Is this address the guest's _setjmp3, reached through its import thunk?
 *
 * For the EXECUTION ENGINE, which cannot use the stub below. A stub records
 * the guest state and returns, so its host frame is gone before longjmp needs
 * it -- which is why the stub marks the buffer unresumable. The title call
 * loop has a host frame that remains live while x86port executes guest code,
 * so it takes the host setjmp itself and must recognise the thunk to do it.
 *
 * Both module spellings answer yes: the exe imports MSVCR71.dll and the DLLs
 * import MSVCRT.dll, and the symbol is the same function either way.
 */
int x86_setjmp3_thunk(uint32_t addr) {
  static uint32_t s_addr1 = 0, s_addr2 = 0;
  static int s_cached = 0;
  if (!x86_is_thunk(addr))
    return 0;
  if (!s_cached) {
    s_addr1 = x86_native_thunk("MSVCR71.DLL", "_setjmp3");
    s_addr2 = x86_native_thunk("MSVCRT.DLL", "_setjmp3");
    s_cached = 1;
  }
  return (s_addr1 && addr == s_addr1) || (s_addr2 && addr == s_addr2);
}

/*
 * The stub form, for a call that reaches _setjmp3 through the IAT rather than
 * through the inline setjmp boundary. It can record the guest state but NOT a
 * resumable host frame, so it marks the buffer unresumable and longjmp says so
 * by name instead of jumping into a dead frame.
 *
 * The engine never gets here: x86_setjmp3_thunk above routes it away first.
 */
void imp_MSVCR71__setjmp3(CPU *C) {
  uint32_t env = A(0);
  int i = jmp_slot_for(env, C->reg[kX86pEsp]);

  g_jmp[i].env = env;
  g_jmp[i].regs = *C;
  g_jmp[i].caller = RD32(C->reg[kX86pEsp]);
  g_jmp[i].used = 1;
  g_jmp[i].resumable = 0;
  if (env)
    WR32(env, 0x53544F50u);
  ret_c(C, 0);
}

void imp_MSVCR71_longjmp(CPU *C) {
  uint32_t env = A(0), value = A(1);
  int i, slot = -1;

  /* g_jmp_cap, not JMP_SLOTS: the table doubles, and scanning the initial
     16 of a grown table reports "never recorded" for a buffer that is
     sitting in slot 17 -- a resumable longjmp turned into an abort by the
     search, not by the guest. */
  for (i = 0; i < g_jmp_cap; i++)
    if (g_jmp[i].used && g_jmp[i].env == env)
      slot = i;

  if (slot >= 0 && g_jmp[slot].resumable) {
    /* The guest-call frame that took this setjmp is still on the host stack;
       jumping unwinds every frame between here and it. value 0 must arrive
       as 1, exactly as longjmp specifies. */
    g_jmp_active = slot;
    longjmp(g_jmp[slot].buf, value ? (int)value : 1);
  }

  x2_log_error(
      "\n*** longjmp(0x%08x, %u) -- the guest is unwinding to a setjmp "
      "this host cannot resume.\n"
      "    That buffer %s\n",
      env, value,
      slot < 0 ? "was NEVER recorded, so the guest may be unwinding to a "
                 "frame that never ran."
               : "was recorded by the IMPORT STUB, not by the title call "
                 "loop, so no host frame was captured with it.\n"
                 "    That means the call reached _setjmp3 indirectly, or "
                 "the call did not cross the inline setjmp boundary\n"
                 "    that owns the live host frame.");
  say_where("longjmp", RD32(C->reg[kX86pEsp]));
  if (slot >= 0)
    say_where("the setjmp it unwinds to", g_jmp[slot].caller);
  x86_diag_dump();
  crt_unimpl("longjmp", "no resumable host frame was captured for that "
                        "jmp_buf; see the comment in crt.c");
}
