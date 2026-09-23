/* guest_memory_arena.c -- see guest_memory_arena.h. */
#include "guest_memory_arena.h"

#include "platform_mman.h"
#include "x2_log.h"

#include <errno.h>
#include <stdlib.h>
#include <string.h>

#if GUEST_ARENA_WINDOW
int guest_arena_acquire(GuestArena *arena) {
  /*
   * calloc, not malloc: the window is the guest's whole address space and the
   * game reads memory it has committed but not written. The allocator hands
   * back fresh program memory that is already zero, so this costs address
   * space rather than the 2.5 GB of pages it describes -- only the pages the
   * guest touches ever become real.
   */
  void *window = calloc((size_t)GUEST_SPACE_SIZE, 1u);
  if (!window) {
    x2_log_error("guest_memory: cannot reserve the %llu MB guest window; the "
                 "packed layout in guest_layout.h needs it contiguous\n",
                 (unsigned long long)(GUEST_SPACE_SIZE >> 20));
    errno = ENOMEM;
    return -1;
  }
  arena->base = (uintptr_t)window;
  arena->host_page_size = 0;
  arena->guard_above = 0;
  return 0;
}
#elif GUEST_ARENA_RESERVED
int guest_arena_acquire(GuestArena *arena) {
  long host_page_size = x2_page_size();
  if (host_page_size < GUEST_PAGE_SIZE ||
      (unsigned long)host_page_size > UINT32_MAX ||
      ((unsigned long)host_page_size & ((unsigned long)host_page_size - 1u))) {
    x2_log_error("guest_memory: unsupported host page size %ld; expected a "
                 "power-of-two multiple of the 4096-byte guest page\n",
                 host_page_size);
    errno = EINVAL;
    return -1;
  }
  /* The guard page above the space is part of the one reservation, so it is
     there whatever else the host has mapped. */
  void *base = x2_map_anonymous(
      NULL, (size_t)GUEST_SPACE_SIZE + (size_t)host_page_size, PROT_NONE);
  if (base == X2_MAP_FAILED) {
    x2_log_error("guest_memory: cannot reserve the 4 GB guest arena: %s\n",
                 strerror(errno));
    return -1;
  }
  arena->base = (uintptr_t)base;
  arena->host_page_size = (uint32_t)host_page_size;
  arena->guard_above = (uint32_t)host_page_size;
  x2_log_error("guest_memory: reserved guest arena 0x%llx..0x%llx\n",
               (unsigned long long)arena->base,
               (unsigned long long)(arena->base + GUEST_SPACE_SIZE));
  return 0;
}
#else
/*
 * A no-access page at 4 GB, just above the identity-mapped space. The only
 * guest access that can leave the space is one overrunning its top by less
 * than its width, and with this page there it faults in the host like any
 * unmapped guest page. Returns the guard's size, or 0 when the host already
 * has something at 4 GB -- the JIT then keeps the check, which is correct and
 * slower, so the run says so.
 */
static uint32_t map_guard_above(void) {
  const size_t page = (size_t)x2_page_size();
  void *const want = (void *)(uintptr_t)GUEST_SPACE_SIZE;
  void *const guard = x2_map_anonymous(want, page, PROT_NONE);
  if (guard == want)
    return (uint32_t)page;
  if (guard != X2_MAP_FAILED)
    x2_unmap(guard, page);
  x2_log_info("guest_memory: no guard page could be placed at 4 GB; every "
              "translated guest access keeps its bounds check\n");
  return 0;
}

int guest_arena_acquire(GuestArena *arena) {
  arena->base = 0;
  arena->host_page_size = 0;
  arena->guard_above = map_guard_above();
  return 0;
}
#endif
