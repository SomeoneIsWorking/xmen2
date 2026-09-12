/*
 * The per-import probe: which host imports an interval spent its time in.
 *
 * Its own file because it is its own thing -- a pair of tables, a recorder on
 * the dispatch path and a reader -- sharing nothing with the thunk table it
 * measures beyond the index.
 *
 * WHY COUNTS ARE NOT ENOUGH. The boundary ring collapses consecutive identical
 * crossings, so a hot import called in a loop reads as a handful of entries;
 * raw counts fixed that and named the imports behind a slow window. But counts
 * rank by frequency, and the expensive import is not always the frequent one:
 * a gameplay frame makes ~1,490 SetVertexShaderConstant calls and ~170 draws,
 * and only time says which of those the frame is actually spent in. So the
 * probe carries both, and ranks by time whenever the time was measured.
 *
 * The time half is armed with the hot-guest-body probe (see x86_hotep.h),
 * because it is the same clock_gettime pair per crossing. Unarmed, every
 * reported time is zero and the reader says so by ranking on calls instead --
 * a probe that never timed anything must not print a plausible ranking.
 */
#ifndef X86_THUNK_PROBE_H
#define X86_THUNK_PROBE_H

#include <stdint.h>

/* One dispatched import, with its exclusive host time (0 when unarmed). */
void x86_thunk_probe_note(uint32_t index, unsigned long long ns);

/* The probe's clock, for a dispatch path that bypasses the timed dispatcher
   and so must measure its own crossing (the import fastpath does). One owner
   for it, so two paths cannot end up on two clocks. */
unsigned long long x86_thunk_probe_clock_ns(void);

/*
 * A reader's own state, so a caller cannot size it wrong.
 *
 * The thunk table grows while the game runs -- every GetProcAddress and every
 * native callback claims a slot -- and the heartbeat reads from another
 * thread. A caller-allocated snapshot sized by the count at first use was
 * already too small by the next interval, and the write past its end corrupted
 * glibc's heap into an abort with nothing pointing back here. The capacity is
 * fixed for the life of the process, so this owns one allocation of it.
 */
typedef struct X86ThunkProbe X86ThunkProbe;

/* NULL when the allocation fails; the caller reports that and stops asking. */
X86ThunkProbe *x86_thunk_probe_create(void);
void x86_thunk_probe_destroy(X86ThunkProbe *probe);

/*
 * The top `cap` imports since this reader's previous call, descending.
 *
 * Writes module, symbol, call delta and exclusive-time delta for each, and
 * returns how many it wrote. `*by_time` says which key it ranked on, so the
 * caller can label the list rather than let a reader assume.
 */
unsigned int x86_thunk_probe_top(X86ThunkProbe *probe, const char **mod,
                                 const char **sym, unsigned long *calls,
                                 unsigned long long *ns, unsigned int cap,
                                 int *by_time);

#endif
