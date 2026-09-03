/*
 * The guest owns a 32-bit address space; several hosts will not lend it.
 *
 * Apple Silicon requires the normal 4 GB __PAGEZERO reservation and kills a
 * binary with a smaller one before main(); Android's loader and ART already
 * occupy the low addresses.  Both reserve a separate 4 GB host arena and add
 * its base at every guest-memory access instead.  Desktop Linux and Intel
 * hosts retain identity mappings, so the same boundary also centralises the
 * collision checks that used to be open-coded around mmap.
 */
#include "guest_memory.h"
#include "platform_mman.h"

#include <errno.h>
#include <pthread.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

#define GUEST_SPACE_SIZE (UINT64_C(1) << 32)
/* Win32 page state is always 4 KiB, including on a host whose VM protection
   granule is larger.  Apple Silicon uses 16 KiB hardware pages: treating that
   as the guest page size made a MEM_DECOMMIT of one Windows page revoke access
   to three still-committed neighbours. */
#define GUEST_PAGE_SIZE 0x1000u
#if defined(__APPLE__) && defined(__aarch64__)
#define HOST_PAGE_SIZE 0x4000u
#else
#define HOST_PAGE_SIZE GUEST_PAGE_SIZE
#endif
#define GUEST_PAGE_COUNT (GUEST_SPACE_SIZE / GUEST_PAGE_SIZE)
#define PAGE_MAPPED 0x80u

/*
 * Whether the host refuses to hand this process the low 4 GB one-to-one, so
 * the guest space must be reserved up front and every address rebased into it.
 * Apple Silicon refuses those addresses outright; on Android the loader and
 * ART already occupy them, and a fixed map of the guest heap at 0x71000000
 * fails with the arena having nowhere to live.
 *
 * Such a host must also never munmap inside the arena -- it drops protection
 * instead, so that nothing else can claim the hole it would leave. This is a
 * separate question from HOST_PAGE_SIZE above: Apple's 16 KiB granule needs
 * the grouping in apply_host_protection, while a 4 KiB Android page makes the
 * same code a one-page no-op.
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

uintptr_t g_guest_memory_base;

static unsigned char g_pages[GUEST_PAGE_COUNT];
static pthread_mutex_t g_pages_lock = PTHREAD_MUTEX_INITIALIZER;
static int g_ready;

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

#if GUEST_ARENA_RESERVED
/*
 * Apply the logical 4 KiB page table to the host's VM granule.
 *
 * On macOS that granule is 16 KiB and the grouping below is load-bearing; on a
 * 4 KiB host such as Android it collapses to one page per group and the loop
 * is an ordinary mprotect.
 *
 * A granule must remain accessible while ANY Windows page in it is accessible.
 * The per-4-KiB table remains authoritative for VirtualQuery and validation;
 * the necessarily broader host permission is only the closest protection the
 * hardware can express.  Call with g_pages_lock held.
 */
static int apply_host_protection(uint32_t first, uint32_t count) {
  const uint32_t pages_per_host = HOST_PAGE_SIZE / GUEST_PAGE_SIZE;
  uint32_t group = first & ~(pages_per_host - 1u);
  uint32_t end = (first + count + pages_per_host - 1u) & ~(pages_per_host - 1u);

  for (; group < end; group += pages_per_host) {
    uint32_t i;
    int protection = PROT_NONE;
    for (i = 0; i < pages_per_host; i++)
      if (g_pages[group + i] & PAGE_MAPPED)
        protection |= g_pages[group + i] & ~PAGE_MAPPED;
    if (mprotect(host_pointer(group * GUEST_PAGE_SIZE), HOST_PAGE_SIZE,
                 protection) != 0)
      return -1;
  }
  return 0;
}
#endif

int guest_memory_init(void) {
  if (g_ready)
    return 0;
#if GUEST_ARENA_RESERVED
  void *arena = mmap(NULL, (size_t)GUEST_SPACE_SIZE, PROT_NONE,
                     MAP_PRIVATE | MAP_ANONYMOUS | MAP_NORESERVE, -1, 0);
  if (arena == MAP_FAILED) {
    fprintf(stderr, "guest_memory: cannot reserve the 4 GB guest arena: %s\n",
            strerror(errno));
    return -1;
  }
  g_guest_memory_base = (uintptr_t)arena;
  fprintf(stderr, "guest_memory: reserved guest arena 0x%llx..0x%llx\n",
          (unsigned long long)g_guest_memory_base,
          (unsigned long long)(g_guest_memory_base + GUEST_SPACE_SIZE));
#else
  g_guest_memory_base = 0;
#endif
  g_ready = 1;
  return 0;
}

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
  void *host;
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
  host = host_pointer((uint32_t)start);
#if GUEST_ARENA_RESERVED
  memset(g_pages + first, PAGE_MAPPED | (unsigned char)protection, count);
  if (apply_host_protection(first, count) != 0) {
    memset(g_pages + first, 0, count);
    (void)apply_host_protection(first, count);
    pthread_mutex_unlock(&g_pages_lock);
    return -1;
  }
#else
  host = mmap(host, (size_t)count * GUEST_PAGE_SIZE, protection,
              MAP_PRIVATE | MAP_ANONYMOUS | MAP_FIXED_NOREPLACE | MAP_NORESERVE,
              -1, 0);
  if (host == MAP_FAILED || (uintptr_t)host != start) {
    if (host != MAP_FAILED)
      munmap(host, (size_t)count * GUEST_PAGE_SIZE);
    pthread_mutex_unlock(&g_pages_lock);
    errno = EEXIST;
    return -1;
  }
#endif
#if !GUEST_ARENA_RESERVED
  memset(g_pages + first, PAGE_MAPPED | (unsigned char)protection, count);
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
#if GUEST_ARENA_RESERVED
  pthread_mutex_lock(&g_pages_lock);
  for (i = 0; i < count; i++)
    if (g_pages[first + i] & PAGE_MAPPED)
      g_pages[first + i] = PAGE_MAPPED | (unsigned char)protection;
  result = apply_host_protection(first, count);
  pthread_mutex_unlock(&g_pages_lock);
  return result;
#else
  result = mprotect(host_pointer(first * GUEST_PAGE_SIZE),
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
  void *host;
  if (span(address, size, &first, &count) != 0)
    return -1;
  host = host_pointer(first * GUEST_PAGE_SIZE);
  pthread_mutex_lock(&g_pages_lock);
#if GUEST_ARENA_RESERVED
  for (i = 0; i < count; i++)
    g_pages[first + i] = 0;
  if (apply_host_protection(first, count) != 0) {
    pthread_mutex_unlock(&g_pages_lock);
    return -1;
  }
#else
  if (munmap(host, (size_t)count * GUEST_PAGE_SIZE) != 0) {
    pthread_mutex_unlock(&g_pages_lock);
    return -1;
  }
#endif
#if !GUEST_ARENA_RESERVED
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
  pthread_mutex_lock(&g_pages_lock);
  for (i = 0; i < count; i++)
    if (!(g_pages[first + i] & PAGE_MAPPED) ||
        !(g_pages[first + i] & PROT_READ)) {
      readable = 0;
      break;
    }
  pthread_mutex_unlock(&g_pages_lock);
  return readable;
}
