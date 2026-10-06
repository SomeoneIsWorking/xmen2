#ifndef X2_CRT_STDIO_H
#define X2_CRT_STDIO_H

#include <stdint.h>
#include <stdio.h>

/*
 * The host stream behind a guest FILE*.
 *
 * A guest FILE* is either a small handle from this port's own table or a
 * pointer into the `_iob` array the guest reaches its standard streams
 * through; crt_stdio.c owns both. Neither is a host pointer, so every user
 * goes through here. Aborts on a handle that is neither -- a stream the guest
 * never opened is a broken guest, not a stream to invent.
 */
FILE *crt_file(uint32_t guest_handle);

/* The guest address of `_iob[0]`, allocated on first use. */
uint32_t crt_iob_base(void);

#endif /* X2_CRT_STDIO_H */
