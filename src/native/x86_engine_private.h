/*
 * x86_engine_private.h -- what the engine's selftest needs and nothing else
 * does.
 *
 * A separate header rather than a wider public one: forgetting a run's
 * counters is a thing exactly one caller may do, and only because its own work
 * is not part of the measurement it would otherwise pollute.
 */
#ifndef X2_X86_ENGINE_PRIVATE_H
#define X2_X86_ENGINE_PRIVATE_H

/*
 * Where a function this engine runs returns TO.
 *
 * A host-initiated guest call needs a real guest return address so translated
 * RET can unwind to this title boundary without executing unrelated bytes.
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

/*
 * The engine's own work is done; everything from here is the game.
 *
 * Called once, by the selftest, when it passes. It does two things that both
 * belong to that one moment:
 *
 *   - Zeroes the run counters. The selftest executes real guest instructions
 *     through the real entry point, so its work lands in the same counters the
 *     game's does. Subtracting its call and its instructions was not enough --
 *     the nesting high-water mark stayed at 1, and the report then read "0
 *     calls, deepest nesting 1", which is not a state that can happen.
 *   - Arms the per-thread TEB check. A thread running GAME code must have one,
 *     and the engine refuses a call from a thread that does not. The selftest
 *     itself runs on the startup thread before the TEB exists and executes a
 *     program with no FS-relative access at all, so the invariant is about
 *     what comes after it, not about every entry.
 */
void x2_engine_enter_service(void);

/* One host thunk / override crossing happened. A run counter for the shutdown
   report; kept in x86_engine.c with the rest of g_engine. */
void x2_engine_note_callout(void);

int x2_engine_jump_selftest(unsigned int page, unsigned int stack);

#endif
