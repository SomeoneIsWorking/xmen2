#ifndef GUEST_MEMORY_H
#define GUEST_MEMORY_H

#include <stddef.h>
#include <stdint.h>
#include <string.h>

#include "guest_layout.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Translate the 32-bit address visible to the guest program into a host
   pointer.  On hosts that can map the low 4 GB this base remains zero. */
extern uintptr_t g_guest_memory_base;

/*
 * The whole guest space as one borrowed span, for the execution owner to hand
 * to the guest CPU. `perms` holds PROT_READ|PROT_WRITE per page, with zero for
 * a page that has no access at all; it is NULL on a host whose own VM enforces
 * permissions, which is the same question GUEST_ARENA_WINDOW below answers.
 */
typedef struct GuestMemoryWindow {
  uint8_t *host;
  uint32_t size;
  const uint8_t *perms;
  uint32_t page_shift;
} GuestMemoryWindow;

GuestMemoryWindow guest_memory_window(void);

/*
 * Told when different bytes appear at a guest range, so whoever has translated
 * code from the old ones can throw it away. Registered by the execution owner
 * rather than called by name, because memory does not depend on execution --
 * and a mapping made before anything is registered needs no notification,
 * since nothing has been translated yet.
 *
 * A protection change counts, even though generated code consults the page
 * table on every access and so needs no telling for the access check itself:
 * a decommit followed by a recommit is how the guest replaces the contents of
 * a span, and code translated from what was there before must go with it.
 */
typedef void (*GuestMemoryRemapObserver)(uint32_t address, uint32_t size);
void guest_memory_set_remap_observer(GuestMemoryRemapObserver observer);

/*
 * Why a notification was sent, and how many of each have been. The execution
 * owner's own counters say how much translated code an invalidation threw
 * away; these say which of this owner's three operations asked for it, which
 * is the half needed to decide whether a notification was worth sending. A
 * cause that never fires reads as zero rather than as absent, so "the guest
 * never did this" and "nobody counted it" stay apart.
 */
typedef enum GuestMemoryRemapCause {
  kGuestRemapMap,
  kGuestRemapProtect,
  kGuestRemapRelease,
  kGuestRemapCauseCount
} GuestMemoryRemapCause;

typedef struct GuestMemoryRemapCounts {
  uint64_t calls[kGuestRemapCauseCount];
  uint64_t pages[kGuestRemapCauseCount];
} GuestMemoryRemapCounts;

GuestMemoryRemapCounts guest_memory_remap_counts(void);
const char *guest_memory_remap_cause_name(GuestMemoryRemapCause cause);

/*
 * Whether the guest space is a window inside this program's own memory rather
 * than host address space with its own protection. The browser has no VM to
 * ask; X2_GUEST_ARENA_WINDOW forces the answer so the same owner can be run,
 * and falsified, on a desktop host.
 */
#if defined(X2_GUEST_ARENA_WINDOW)
#define GUEST_ARENA_WINDOW X2_GUEST_ARENA_WINDOW
#elif defined(__EMSCRIPTEN__)
#define GUEST_ARENA_WINDOW 1
#else
#define GUEST_ARENA_WINDOW 0
#endif

#if GUEST_ARENA_WINDOW
/*
 * The browser's guest space is one window inside the program's own memory, so
 * an address past it reaches the program instead of faulting the way a host VM
 * would. One compare keeps a bad guest pointer a reported abort rather than
 * silent damage to something unrelated; permissions stay the generated code's
 * business, exactly as they are the host VM's on every other target.
 */
void guest_memory_outside_window(uint32_t address);

static inline void *guest_memory_pointer(uint32_t address) {
  if (address >= GUEST_LAYOUT_LIMIT) {
    guest_memory_outside_window(address);
  }
  return address ? (void *)(g_guest_memory_base + (uintptr_t)address) : NULL;
}
#else
static inline void *guest_memory_pointer(uint32_t address) {
  return address ? (void *)(g_guest_memory_base + (uintptr_t)address) : NULL;
}
#endif

static inline const void *guest_memory_const_pointer(uint32_t address) {
  return guest_memory_pointer(address);
}

static inline uint32_t guest_memory_address(const void *pointer) {
  return (uint32_t)((uintptr_t)pointer - g_guest_memory_base);
}

static inline void guest_memory_read(uint32_t address, void *destination,
                                     size_t size) {
  memcpy(destination, guest_memory_const_pointer(address), size);
}

static inline void guest_memory_write(uint32_t address, const void *source,
                                      size_t size) {
  memcpy(guest_memory_pointer(address), source, size);
}

int guest_memory_host_address(const void *pointer, uint32_t *address);

/* Reads that refuse instead of faulting, for a diagnostic that must be able to
   inspect a pointer the guest has just proved is wrong. */
int guest_memory_try_read(uint32_t address, void *destination, size_t size);

int guest_memory_init(void);
int guest_memory_map_fixed(uint32_t address, size_t size, int protection);
int guest_memory_map_any(uint32_t first, uint32_t last, size_t alignment,
                         size_t size, int protection, uint32_t *address);
int guest_memory_protect(uint32_t address, size_t size, int protection);
int guest_memory_release(uint32_t address, size_t size);
int guest_memory_is_readable(uint32_t address, size_t size);

#ifdef __cplusplus
} /* extern "C" */
#endif

#endif
