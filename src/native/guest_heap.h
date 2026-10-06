/* A heap whose addresses fit in a guest pointer. See guest_heap.c for why the
   host's malloc cannot be used. */
#ifndef GUEST_HEAP_H
#define GUEST_HEAP_H

#include <stdint.h>

int guest_heap_init(uint32_t base, uint32_t size);
uint32_t guest_malloc(uint32_t n);
void guest_free(uint32_t p);

/* How much of the arena was used, and how close the run came to the end of it.
   Printed at exit: a run that peaked at 99% and one that peaked at 4% behave
   identically until the first fails, and the difference matters. */
void guest_heap_report(void);
uint32_t guest_realloc(uint32_t p, uint32_t n);
uint32_t guest_heap_base(void);
int guest_heap_contains(uint32_t a, uint32_t *base, uint32_t *size);

/* Is `a` inside a block that is still allocated? 0 for a freed block AND for an
   address outside the arena; conservative the safe way (see guest_heap.c). */
int guest_heap_addr_is_live(uint32_t a);
void guest_heap_stats(uint32_t *used, uint32_t *free_, uint32_t *blocks);

#endif /* GUEST_HEAP_H */
