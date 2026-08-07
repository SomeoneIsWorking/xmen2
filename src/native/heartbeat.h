/*
 * A liveness heartbeat for a run that does not return.
 *
 * Every other instrument here speaks at the END of a run: the reached set, the
 * boundary ring, the allocator reports, the argument watch. That is fine for a
 * run that crashes or exits, and useless for one that keeps going -- which is
 * what the game does the moment it reaches its main loop. Issue #35 read
 * exactly like a hang for a whole session: the log stopped, and the only
 * evidence available afterwards was the ring, whose last entries happened to
 * be the frame timer. "Spinning on the clock" and "running frames" produce the
 * SAME silence.
 *
 * So this prints one line every few seconds, from a host thread that the guest
 * cannot starve, and it prints whether or not anything is happening. The
 * zero-delta cases are spelled out in words, because that is the whole point:
 *
 *   crossings unchanged  -> the guest executed nothing; it is blocked inside
 *                           host code, not looping
 *   presents unchanged   -> the loop runs but never reaches Present
 *   draws unchanged      -> frames are presented with nothing drawn in them
 *
 * X2_HEARTBEAT=<seconds>   period; 0 disables it. Default 5.
 */
#ifndef X2_HEARTBEAT_H
#define X2_HEARTBEAT_H

/* Starts the thread. Announces itself (or that it is disabled) on stderr, so a
   run with no [HB] lines cannot be mistaken for a run that produced none. */
void heartbeat_start(void);

#endif /* X2_HEARTBEAT_H */
