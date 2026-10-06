/*
 * frame_limiter_wait.h -- how long the frame limiter may sleep instead of spin.
 *
 * XMen2.exe's frame function ends each frame in a busy-wait (0x00401ff0):
 *
 *   loop: CALL 0x0055b610          ; the timer singleton
 *         CALL [timer+0x28](0)     ; now, float seconds
 *         FST  [esp+0x14]          ; the limiter's last clock read
 *         FSUB [app+0x1c]          ; minus the time the frame began
 *         FCOMP [app+0x18]         ; against the minimum frame time
 *         JNP  loop                ; while elapsed < minimum
 *
 * Paced, every idle millisecond of a frame is spent in that loop, reading the
 * clock through two guest calls and a QueryPerformanceCounter. The wait is
 * owned at the loop's own first instruction, the CALL 0x0055b610 whose
 * override already runs there: it sleeps for all but a margin of what the
 * last clock read says is left, and then lets the loop continue unchanged. The
 * loop still reads the clock and still leaves on its own comparison, so the
 * frame ends when it did before -- the sleep only replaces spin iterations
 * that would have found the frame not yet over.
 */
#ifndef FRAME_LIMITER_WAIT_H
#define FRAME_LIMITER_WAIT_H

#include <stdint.h>

/* The last stretch is left to the loop's spin: a host sleep wakes late, and
   a late wake would lengthen the frame. Whole milliseconds used to be slept
   and a millisecond left, which spun ~2,700 clock reads a frame in Dead Zone
   (#141); a timed wait wakes within tens of microseconds on a desktop host. */
#define FRAME_LIMITER_SPIN_US 250u
/* Below this a sleep costs more in the hand-off than it saves in spin. */
#define FRAME_LIMITER_MIN_SLEEP_US 100u

/*
 * The microseconds the limiter may sleep before its next clock read: the
 * minimum frame time less the elapsed time its last read saw, less
 * FRAME_LIMITER_SPIN_US, and never more than the minimum frame time itself.
 * 0 when that leaves less than FRAME_LIMITER_MIN_SLEEP_US, or when a value is
 * not a number.
 */
uint32_t frame_limiter_sleep_us(float min_frame_s, float frame_start_s,
                                float last_read_s);

#endif /* FRAME_LIMITER_WAIT_H */
