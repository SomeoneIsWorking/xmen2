#include "x2_log.h"
/*
 * The guest owns a 32-bit address space; several hosts will not lend it.
 *
 * Apple Silicon requires the normal 4 GB __PAGEZERO reservation and kills a
 * binary with a smaller one before main(); Android's loader and ART already
 * occupy the low addresses.  Both reserve a separate 4 GB host arena and add
 * its base at every guest-memory access instead.  Desktop Linux and Intel
 * hosts retain identity mappings, so the same boundary also centralises the
 * collision checks that used to be open-coded around mmap.
 *
 * The browser has no VM at all: nothing there can make one page of the
 * program's own linear memory unreadable. It reserves a window covering the
 * packed layout in guest_layout.h and projects the same page table into the
 * byte-per-page permissions x86port's generated code reads, so a guest access
 * costs a bounds compare and two loads instead of a host call that binary
 * searches a mapping array. Measured before this existed: 62% of the browser's
 * guest worker was working out where its memory operands point.
 */
#include "guest_layout.h"
#include "guest_memory.h"
#include "guest_memory_arena.h"
#include "platform_mman.h"

#include "platform_posix.h"
#include "platform_threads.h"
#include <errno.h>
#include <stdatomic.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* Win32 page state is always 4 KiB, including on a host whose VM protection
   granule is larger.  Apple Silicon uses 16 KiB hardware pages: treating that
   as the guest page size made a MEM_DECOMMIT of one Windows page revoke access
   to three still-committed neighbours. */
#define GUEST_PAGE_COUNT (GUEST_SPACE_SIZE / GUEST_PAGE_SIZE)
#define PAGE_MAPPED 0x80u

/* Both arenas are this process's own memory for the whole run: mapping and
   releasing move permissions in the page table rather than handing spans back
   to the host, so the two share every path below except how a permission is
   applied and how the arena is obtained. */
#define GUEST_ARENA_OWNED (GUEST_ARENA_RESERVED || GUEST_ARENA_WINDOW)

uintptr_t g_guest_memory_base;

/* Writers hold g_pages_lock, which orders them against each other. The
   entries are atomic so guest_memory_is_readable can read them without it:
   its answer was already stale by the time a caller used it (the lock was
   released before the copy it guards), and the lock per call was ~1.5% of
   samples under a native override that validates each field it reads. */
static _Atomic unsigned char g_pages[GUEST_PAGE_COUNT];
static pthread_mutex_t g_pages_lock = PTHREAD_MUTEX_INITIALIZER;

/* Call with g_pages_lock held. */
static void pages_fill(uint32_t first, uint32_t count, unsigned char value) {
  uint32_t i;
  for (i = 0; i < count; i++)
    atomic_store_explicit(&g_pages[first + i], value, memory_order_relaxed);
}
static int g_ready;
static GuestMemoryWindow g_window;
static GuestMemoryRemapObserver g_remapped;
#if GUEST_ARENA_RESERVED
static uint32_t g_host_page_size;
#endif
#if GUEST_ARENA_WINDOW
static uint8_t g_perms[GUEST_PAGE_COUNT];
#endif
#if GUEST_ARENA_OWNED
/*
 * Which pages have been mapped before, so a later map can hand back the zeroes
 * Win32 promises. An arena keeps its bytes when a page is released -- nothing
 * unmaps them -- so without this the guest reads whatever the previous owner
 * of that address left behind. The host's own mmap zeroes, which is why the
 * identity path needs no table.
 */
static unsigned char g_written[GUEST_PAGE_COUNT];
#endif

void guest_memory_set_remap_observer(GuestMemoryRemapObserver observer) {
  g_remapped = observer;
}

static atomic_ullong g_remap_calls[kGuestRemapCauseCount];
static atomic_ullong g_remap_pages[kGuestRemapCauseCount];

static void notify_remap(GuestMemoryRemapCause cause, uint32_t address,
                         uint32_t size) {
  atomic_fetch_add_explicit(&g_remap_calls[cause], 1u, memory_order_relaxed);
  atomic_fetch_add_explicit(&g_remap_pages[cause], size / GUEST_PAGE_SIZE,
                            memory_order_relaxed);
  if (g_remapped) {
    g_remapped(address, size);
  }
}

GuestMemoryRemapCounts guest_memory_remap_counts(void) {
  GuestMemoryRemapCounts out;
  int i;
  for (i = 0; i < kGuestRemapCauseCount; i++) {
    out.calls[i] =
        atomic_load_explicit(&g_remap_calls[i], memory_order_relaxed);
    out.pages[i] =
        atomic_load_explicit(&g_remap_pages[i], memory_order_relaxed);
  }
  return out;
}

const char *guest_memory_remap_cause_name(GuestMemoryRemapCause cause) {
  switch (cause) {
  case kGuestRemapMap:
    return "map";
  case kGuestRemapProtect:
    return "protect";
  case kGuestRemapRelease:
    return "release";
  case kGuestRemapCauseCount:
    break;
  }
  return "unknown";
}

static void *host_pointer(uint32_t address) {
  return (void *)(g_guest_memory_base + (uintptr_t)address);
}

static uint64_t align_down(uint64_t value) {
  return value & ~(uint64_t)(GUEST_PAGE_SIZE - 1u);
}

static uint64_t align_up(uint64_t value) {
  return (value + GUEST_PAGE_SIZE - 1u) & ~(uint64_t)(GUEST_PAGE_SIZE - 1u);
}

static int span(uint32_t address, size_t size, uint32_t *first,
                uint32_t *count) {
  uint64_t start = align_down(address);
  uint64_t end = align_up((uint64_t)address + size);
  if (!size || end > GUEST_SPACE_SIZE || end <= start)
    return -1;
  *first = (uint32_t)(start / GUEST_PAGE_SIZE);
  *count = (uint32_t)((end - start) / GUEST_PAGE_SIZE);
  return 0;
}

#if GUEST_ARENA_OWNED
/*
 * Hand back the zeroes Win32 promises a freshly committed page.
 *
 * Only pages this arena has mapped before are cleared, so the first commit of
 * a region costs nothing: the arena starts zero and the pages the guest never
 * touches never become real memory. Call with g_pages_lock held, before the
 * guest can see the mapping.
 */
static void zero_reused_pages(uint32_t first, uint32_t count) {
  uint32_t i;
  for (i = 0; i < count; i++) {
    if (g_written[first + i]) {
      memset(host_pointer((first + i) * GUEST_PAGE_SIZE), 0, GUEST_PAGE_SIZE);
    }
    g_written[first + i] = 1;
  }
}
#endif

#if GUEST_ARENA_WINDOW
/*
 * Apply the page table to the permissions x86port's generated code reads.
 *
 * Exact, because the table is already a byte per Windows page and so is the
 * one it is copied into: there is no host granule to round up to.
 *
 * PAGE_MAPPED is deliberately not consulted. PROT_NONE is zero, so a page
 * reserved without access and a page never mapped at all mask to the same
 * empty permission -- which is the same refusal, and the only answer generated
 * code needs. g_pages remains the authority for VirtualQuery, which does have
 * to tell the two apart.
 *
 * Call with g_pages_lock held.
 */
static int apply_host_protection(uint32_t first, uint32_t count) {
  uint32_t i;
  for (i = 0; i < count; i++) {
    g_perms[first + i] =
        (uint8_t)(g_pages[first + i] & (PROT_READ | PROT_WRITE));
  }
  return 0;
}
#elif GUEST_ARENA_RESERVED
/*
 * Apply the logical 4 KiB page table to the host's VM granule.
 *
 * The host reports its granule at initialization. On a 16 KiB host the
 * grouping below is load-bearing; on a 4 KiB host it collapses to one guest
 * page per group.
 *
 * A granule must remain accessible while ANY Windows page in it is accessible.
 * The per-4-KiB table remains authoritative for VirtualQuery and validation;
 * the necessarily broader host permission is only the closest protection the
 * hardware can express.  Call with g_pages_lock held.
 */
static int apply_host_protection(uint32_t first, uint32_t count) {
  const uint32_t pages_per_host = g_host_page_size / GUEST_PAGE_SIZE;
  uint32_t group = first & ~(pages_per_host - 1u);
  uint32_t end = (first + count + pages_per_host - 1u) & ~(pages_per_host - 1u);

  for (; group < end; group += pages_per_host) {
    uint32_t i;
    int protection = PROT_NONE;
    for (i = 0; i < pages_per_host; i++)
      if (g_pages[group + i] & PAGE_MAPPED)
        protection |= g_pages[group + i] & ~PAGE_MAPPED;
    if (x2_protect(host_pointer(group * GUEST_PAGE_SIZE), g_host_page_size,
                   protection) != 0)
      return -1;
  }
  return 0;
}
#endif

int guest_memory_init(void) {
  if (g_ready)
    return 0;
  GuestArena arena;
  if (guest_arena_acquire(&arena) != 0)
    return -1;
  g_guest_memory_base = arena.base;
  g_window.host = (uint8_t *)arena.base;
  g_window.guard_above = arena.guard_above;
#if GUEST_ARENA_WINDOW
  g_window.size = (uint32_t)GUEST_SPACE_SIZE;
  g_window.perms = g_perms;
  g_window.page_shift = GUEST_PAGE_SHIFT;
#else
  /* The host's own VM owns permissions here, so the window carries no table
     and spans everything: UINT32_MAX rather than 4 GB because a byte count of
     the whole space does not fit, and its last byte is unaddressable anyway. */
  g_window.size = UINT32_MAX;
  g_window.perms = NULL;
  g_window.page_shift = 0;
#endif
#if GUEST_ARENA_RESERVED
  g_host_page_size = arena.host_page_size;
#endif
  g_ready = 1;
  return 0;
}

GuestMemoryWindow guest_memory_window(void) { return g_window; }

int guest_memory_host_address(const void *pointer, uint32_t *address) {
  uintptr_t host = (uintptr_t)pointer;
  if (host < g_guest_memory_base ||
      (uint64_t)(host - g_guest_memory_base) >= GUEST_SPACE_SIZE)
    return 0;
  if (address)
    *address = (uint32_t)(host - g_guest_memory_base);
  return 1;
}

int guest_memory_map_fixed(uint32_t address, size_t size, int protection) {
  uint32_t first, count, i;
  uint64_t start;
  if (!g_ready && guest_memory_init() != 0)
    return -1;
  if (span(address, size, &first, &count) != 0)
    return -1;
  pthread_mutex_lock(&g_pages_lock);
  for (i = 0; i < count; i++) {
    if (g_pages[first + i]) {
      pthread_mutex_unlock(&g_pages_lock);
      errno = EEXIST;
      return -1;
    }
  }
  start = (uint64_t)first * GUEST_PAGE_SIZE;
  notify_remap(kGuestRemapMap, (uint32_t)start, count * GUEST_PAGE_SIZE);
#if GUEST_ARENA_OWNED
  pages_fill(first, count, PAGE_MAPPED | (unsigned char)protection);
  if (apply_host_protection(first, count) != 0) {
    pages_fill(first, count, 0);
    (void)apply_host_protection(first, count);
    pthread_mutex_unlock(&g_pages_lock);
    return -1;
  }
  zero_reused_pages(first, count);
#else
  void *host = x2_map_anonymous(host_pointer((uint32_t)start),
                                (size_t)count * GUEST_PAGE_SIZE, protection);
  if (host == X2_MAP_FAILED || (uintptr_t)host != start) {
    if (host != X2_MAP_FAILED && host != NULL)
      (void)x2_unmap(host, (size_t)count * GUEST_PAGE_SIZE);
    pthread_mutex_unlock(&g_pages_lock);
    errno = EEXIST;
    return -1;
  }
#endif
#if !GUEST_ARENA_OWNED
  pages_fill(first, count, PAGE_MAPPED | (unsigned char)protection);
#endif
  pthread_mutex_unlock(&g_pages_lock);
  return 0;
}

int guest_memory_map_any(uint32_t first_address, uint32_t last_address,
                         size_t alignment, size_t size, int protection,
                         uint32_t *address) {
  uint64_t candidate, step;
  if (!alignment)
    alignment = GUEST_PAGE_SIZE;
  step = align_up(alignment);
  candidate = (first_address + step - 1u) / step * step;
  while (candidate + size <= (uint64_t)last_address) {
    if (guest_memory_map_fixed((uint32_t)candidate, size, protection) == 0) {
      *address = (uint32_t)candidate;
      return 0;
    }
    candidate += step;
  }
  return -1;
}

int guest_memory_protect(uint32_t address, size_t size, int protection) {
  uint32_t first, count, i;
  int result;
  if (span(address, size, &first, &count) != 0)
    return -1;
  /*
   * A protection change invalidates translations even though generated code
   * reads the page table on every access and so would not need telling for the
   * access check alone: a decommit and recommit of the same span is how the
   * game REPLACES what is there, and a block translated from the old bytes
   * would keep running them. Issue #157 -- dropping this left the guest
   * spinning in code that had already been thrown away.
   */
  notify_remap(kGuestRemapProtect, first * GUEST_PAGE_SIZE,
               count * GUEST_PAGE_SIZE);
#if GUEST_ARENA_OWNED
  pthread_mutex_lock(&g_pages_lock);
  for (i = 0; i < count; i++)
    if (g_pages[first + i] & PAGE_MAPPED)
      g_pages[first + i] = PAGE_MAPPED | (unsigned char)protection;
  result = apply_host_protection(first, count);
  pthread_mutex_unlock(&g_pages_lock);
  return result;
#else
  result = x2_protect(host_pointer(first * GUEST_PAGE_SIZE),
                      (size_t)count * GUEST_PAGE_SIZE, protection);
  if (result != 0)
    return result;
  pthread_mutex_lock(&g_pages_lock);
  for (i = 0; i < count; i++)
    if (g_pages[first + i] & PAGE_MAPPED)
      g_pages[first + i] = PAGE_MAPPED | (unsigned char)protection;
  pthread_mutex_unlock(&g_pages_lock);
  return 0;
#endif
}

int guest_memory_release(uint32_t address, size_t size) {
  uint32_t first, count, i;
  if (span(address, size, &first, &count) != 0)
    return -1;
  notify_remap(kGuestRemapRelease, first * GUEST_PAGE_SIZE,
               count * GUEST_PAGE_SIZE);
  pthread_mutex_lock(&g_pages_lock);
#if GUEST_ARENA_OWNED
  for (i = 0; i < count; i++)
    g_pages[first + i] = 0;
  if (apply_host_protection(first, count) != 0) {
    pthread_mutex_unlock(&g_pages_lock);
    return -1;
  }
#else
  if (x2_unmap(host_pointer(first * GUEST_PAGE_SIZE),
               (size_t)count * GUEST_PAGE_SIZE) != 0) {
    pthread_mutex_unlock(&g_pages_lock);
    return -1;
  }
#endif
#if !GUEST_ARENA_OWNED
  for (i = 0; i < count; i++)
    g_pages[first + i] = 0;
#endif
  pthread_mutex_unlock(&g_pages_lock);
  return 0;
}

int guest_memory_is_readable(uint32_t address, size_t size) {
  uint32_t first, count, i;
  int readable = 1;
  if (span(address, size, &first, &count) != 0)
    return 0;
  for (i = 0; i < count; i++) {
    const unsigned page =
        atomic_load_explicit(&g_pages[first + i], memory_order_relaxed);
    if (!(page & PAGE_MAPPED) || !(page & PROT_READ)) {
      readable = 0;
      break;
    }
  }
  return readable;
}

int guest_memory_try_read(uint32_t address, void *destination, size_t size) {
  if (!destination || !size || !guest_memory_is_readable(address, size))
    return 0;
  memcpy(destination, (const void *)(g_guest_memory_base + (uintptr_t)address),
         size);
  return 1;
}

#if GUEST_ARENA_WINDOW
void guest_memory_outside_window(uint32_t address) {
  x2_log_error("guest_memory: guest address 0x%08x is outside the %llu MB "
               "window; the layout in guest_layout.h ends at 0x%08x\n",
               address, (unsigned long long)(GUEST_SPACE_SIZE >> 20),
               (unsigned)GUEST_LAYOUT_LIMIT);
  abort();
}
#endif
