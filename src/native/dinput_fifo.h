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

#endif /* DINPUT_FIFO_H */
