/*
 * x86_engine_internal.h -- what the engine's selftest needs and nothing else
 * does.
 *
 * A separate header rather than a wider public one: forgetting a run's
 * counters is a thing exactly one caller may do, and only because its own work
 * is not part of the measurement it would otherwise pollute.
 */
#ifndef X2_X86_ENGINE_INTERNAL_H
#define X2_X86_ENGINE_INTERNAL_H

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

/*
 * Zero the run counters.
 *
 * The selftest executes real guest instructions through the real entry point,
 * so its work lands in the same counters the game's does. Subtracting its call
 * and its instructions was not enough -- the nesting high-water mark stayed at
 * 1, and the report then read "0 calls, deepest nesting 1", which is not a
 * state that can happen. Everything resets, together.
 */
void x2_engine_forget_run(void);

#endif
