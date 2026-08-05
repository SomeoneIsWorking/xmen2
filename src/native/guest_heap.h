/* A heap whose addresses fit in a guest pointer. See guest_heap.c for why the
   host's malloc cannot be used. */
#ifndef GUEST_HEAP_H
#define GUEST_HEAP_H

#include <stdint.h>

int      guest_heap_init(uint32_t base, uint32_t size);
uint32_t guest_malloc(uint32_t n);
void     guest_free(uint32_t p);
uint32_t guest_realloc(uint32_t p, uint32_t n);
void     guest_heap_stats(uint32_t *used, uint32_t *free_, uint32_t *blocks);

#endif /* GUEST_HEAP_H */
