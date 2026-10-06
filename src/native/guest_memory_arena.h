/*
 * guest_memory_arena.h -- the host span that holds the guest's address space.
 *
 * guest_memory.c owns the pages inside the space; this owns where the space
 * is: a program-memory window in the browser, a reserved and rebased arena on
 * hosts that will not lend the low 4 GB, or the host's own addresses. It also
 * owns the no-access guard above a whole-space span, which lets the JIT drop
 * its per-access bounds check (GuestMemoryWindow.guard_above).
 */
#ifndef X2_GUEST_MEMORY_ARENA_H
#define X2_GUEST_MEMORY_ARENA_H

#include "guest_layout.h"
#include "guest_memory.h"

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#if GUEST_ARENA_WINDOW
/* Only what the layout places, because every byte of the window is real
   program memory rather than reserved host address space. */
#define GUEST_SPACE_SIZE ((uint64_t)GUEST_LAYOUT_LIMIT)
#else
#define GUEST_SPACE_SIZE (UINT64_C(1) << 32)
#endif
/*
 * Whether the host refuses to hand this process the low 4 GB one-to-one, so
 * the guest space must be reserved up front and every address rebased into it.
 * Apple Silicon refuses those addresses outright; on Android the loader and
 * ART already occupy them, and a fixed map of the guest heap at 0x71000000
 * fails with the arena having nowhere to live.
 *
 * Such a host must also never munmap inside the arena -- it drops protection
 * instead, so that nothing else can claim the hole it would leave. This is a
 * separate question from the host page size: both Apple and Android can use
 * a 16 KiB granule, while a 4 KiB host needs no protection grouping.
 */
/* X2_GUEST_ARENA_RESERVED forces the answer either way. It exists for the
 * sanitizers: AddressSanitizer's own shadow lives at low addresses and
 * collides with an identity-mapped guest at 0x00400000, so a desktop build
 * that wants ASan has to take the rebased path -- the same path Apple and
 * Android take in production, not a debug-only variant of it. */
#if defined(X2_GUEST_ARENA_RESERVED)
#define GUEST_ARENA_RESERVED X2_GUEST_ARENA_RESERVED
#elif (defined(__APPLE__) && defined(__aarch64__)) || defined(__ANDROID__)
#define GUEST_ARENA_RESERVED 1
#else
#define GUEST_ARENA_RESERVED 0
#endif

typedef struct GuestArena {
  uintptr_t base; /* host address of guest address 0 */
  uint32_t
      host_page_size;   /* the host's protection granule; reserved arena only */
  uint32_t guard_above; /* bytes from base + 4 GB that fault; 0 for none */
} GuestArena;

/* Obtain the span. 0 on success; -1 with errno set and the reason logged. */
int guest_arena_acquire(GuestArena *arena);

#ifdef __cplusplus
}
#endif

#endif
