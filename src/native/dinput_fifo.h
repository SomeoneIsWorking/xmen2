#ifndef DINPUT_FIFO_H
#define DINPUT_FIFO_H

#include <stdint.h>

/*
 * Drain X2_INPUT_FIFO and press whatever key names arrived.
 *
 * `now` is the caller's clock in seconds; only differences matter. Called on
 * every keyboard poll, whether or not the FIFO is configured -- with it unset
 * this is one getenv and a return.
 */
void dinput_fifo_apply(uint32_t out, uint32_t size, double now);

/*
 * Press `name` for `hold` seconds (0 = the default hold), from the channel
 * named by `via` ("fifo", "control"). ONE table serves every channel: the game
 * has one keyboard, and two tables would fight over the same DirectInput byte
 * with neither able to report it.
 *
 * Returns 1 if the key is now held, 0 with a reason in `why` if not -- an
 * unmapped name, or every slot already down. It never silently does nothing.
 */
int dinput_inject_press(const char *name, double now, double hold,
                        const char *via, char *why, int whyn);

#endif /* DINPUT_FIFO_H */
