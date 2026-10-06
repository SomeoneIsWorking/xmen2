/*
 * The guest lock's voluntary release. See threads_yield.c for what a turn
 * guarantees and why the old unlock/yield/lock could not deliver one.
 */
#ifndef X2_THREADS_YIELD_H
#define X2_THREADS_YIELD_H

/* Give up the calling thread's turn: unlock, wait for another guest thread to
 * take it, relock. The caller must hold the guest lock at depth 1 and should
 * only call it when scheduler_has_waiter() says there is someone to hand to. */
void guest_yield_turn(void);

/* Record that THIS thread newly acquired the guest lock, at every point where
 * it changes hands -- including the re-acquisition inside a condition wait,
 * which never passes through guest_lock(). Releases any pending hand-off. */
void guest_yield_turn_taken(void);

/* Hand-offs that had to park, and the longest park, in ms. Printed live by
 * the heartbeat: a hand-off that never lands is the failure this mechanism
 * exists to prevent, and the bound is what keeps it survivable. */
void guest_yield_counts(unsigned long *handoffs, unsigned long *worst_ms,
                        unsigned long *parks_signalled,
                        unsigned long *parks_timed_out);

/*
 * Record how one timed park on the guest's condition variable ended: by its
 * own deadline, or by a signal (a spurious wake counts as signalled).
 * pthread_cond_timedwait's answer used to be discarded, so a park that slept
 * past its deadline could not be told from one that was woken late -- the
 * fork in issue #149 between "the lock never came to me" and "the wake-up
 * itself was slow". The waiter's own asked/slept pair is counted in
 * kernel32_wait.c; only the two columns together name it.
 */
void guest_yield_note_park(int timed_out);

#endif /* X2_THREADS_YIELD_H */
