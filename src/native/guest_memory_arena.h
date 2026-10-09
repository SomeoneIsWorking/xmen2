/*
 * guest_memory_arena.h -- the host span that holds the guest's address space.
 *
 * guest_memory.cpp owns the pages inside the space; this owns where the space
 * is: a program-memory window in the browser, a reserved and rebased arena on
 * hosts that will not lend the low 4 GB, or the host's own addresses. It also
 * owns the no-access guard above a whole-space span, which lets the JIT drop
 * its per-access bounds check (GuestMemoryWindow.guard_above).
 */
#pragma once

#include "guest_layout.h"
#include "guest_memory.h"

#include <cstdint>

namespace x2::native {

#if GUEST_ARENA_WINDOW
/* Only what the layout places, because every byte of the window is real
   program memory rather than reserved host address space. */
inline constexpr uint64_t GUEST_SPACE_SIZE = (uint64_t)GUEST_LAYOUT_LIMIT;
#else
inline constexpr uint64_t GUEST_SPACE_SIZE = UINT64_C(1) << 32;
#endif

struct GuestArena {
  uintptr_t base; /* host address of guest address 0 */
  uint32_t
      host_page_size;   /* the host's protection granule; reserved arena only */
  uint32_t guard_above; /* bytes from base + 4 GB that fault; 0 for none */
};

/* Obtain the span. 0 on success; -1 with errno set and the reason logged. */
int guest_arena_acquire(GuestArena *arena);

} // namespace x2::native
