#ifndef X2_HEARTBEAT_STALL_H
#define X2_HEARTBEAT_STALL_H

/*
 * What a frozen counter on the beat means, and the one ring dump that says
 * why.
 *
 * Separate from the beat loop because it owns a judgement rather than a
 * measurement: the loop reads counters, this decides what their standing
 * still is allowed to be called. The distinction is load-bearing. The beat
 * counts host-boundary crossings, and a guest spinning inside one compiled
 * block crosses nothing -- so "crossings unchanged" is stopped, blocked in
 * host code, or spinning, and this must not pick one of the three.
 */

/*
 * Report the beat's frozen counters and dump the boundary ring once when the
 * device has stopped presenting. Returns nonzero when the guest crossed no
 * host boundary at all, which is the caller's cue that its per-interval
 * deltas are all zero and not worth a line.
 */
int heartbeat_stall_observe(double t, double period, unsigned long cross,
                            int crossings_moved, int have_dev,
                            int presents_moved);

/* Forget the run's stall state; the reporter is a singleton per process. */
void heartbeat_stall_reset(void);

#endif /* X2_HEARTBEAT_STALL_H */
