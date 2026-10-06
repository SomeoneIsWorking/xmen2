/*
 * WHICH x87 ARITHMETIC THIS RUN PERFORMS, and how much of it an inline
 * encoding-level path could take.
 *
 * Issue #162 measured x87 at roughly a third of the browser's guest worker and
 * then established that the arithmetic itself is near its floor: two separate
 * attempts to make a multiply faster in C moved the frame rate by nothing.
 * What is left to try is removing the call and the plumbing around it by
 * emitting the operation into the block, and that is worth building only for
 * an operation this route actually performs a lot of.
 *
 * The census answers that, off by default and armed with `x87.census=1`. It is
 * one census shared by every guest thread: the counters are advisory totals,
 * not a ledger, and the guest lock serialises the overwhelming majority of the
 * increments anyway.
 */
#ifndef X2_X86_ENGINE_X87_CENSUS_H
#define X2_X86_ENGINE_X87_CENSUS_H

struct X86pCpu;

/* Gives `cpu`'s x87 unit the shared census, or leaves it alone when disarmed.
   Called from cpu_reset, so every guest thread is covered by construction. */
void x86_engine_x87_census_attach(struct X86pCpu *cpu);

/* One line per operation with its share, or the reason there is nothing to
   report. `tag` prefixes each line, as the heartbeat's "[HB] " does. */
void x86_engine_x87_census_report(const char *tag);

#endif
