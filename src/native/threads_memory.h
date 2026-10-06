#ifndef X2_THREADS_MEMORY_H
#define X2_THREADS_MEMORY_H

/*
 * A guest thread's guest memory: its stack and its TIB, both out of the guest
 * arena. threads.c creates and reaps the records; this owns what each one
 * holds in the arena, so taking it and giving it back are one pair.
 */

#include "threads_internal.h"

#include <stdint.h>

/* Give `t` a stack of `stack_bytes` rounded up to a page (0: the default) and
   a TIB. 0, holding nothing and having said why, when the arena is short. */
int guest_thread_memory_alloc(GuestThread *t, uint32_t stack_bytes);

/* Return both; safe on a record that holds either or neither. */
void guest_thread_memory_free(GuestThread *t);

#endif
