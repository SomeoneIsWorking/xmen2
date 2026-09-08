/* Browser guest-memory allocation and lifetime. x86port owns all guest range
 * lookup, permissions and partial mapping changes; this file owns only the
 * host allocations those mappings borrow and the title's Win32 page policy. */
#include "cpu.h"
#include "guest_memory.h"
#include "memory_sparse.h"
#include "x2_log.h"
#include "x86_engine.h"

#include "platform_threads.h"
#include <errno.h>
#include <stdlib.h>
#include <sys/mman.h>

#define GUEST_PAGE_SIZE 4096u
#define GUEST_SPACE_SIZE (UINT64_C(1) << 32)

typedef struct Allocation {
  struct Allocation *next;
  void *host;
  uint32_t size;
} Allocation;

uintptr_t g_guest_memory_base;
static X86pMem g_memory;
static Allocation *g_allocations;
static pthread_mutex_t g_lock = PTHREAD_MUTEX_INITIALIZER;

static int page_span(uint32_t address, size_t size, uint32_t *first,
                     uint32_t *length) {
  uint64_t end;
  if (!size || size > UINT32_MAX)
    return 0;
  *first = address & ~(GUEST_PAGE_SIZE - 1u);
  end = ((uint64_t)address + size + GUEST_PAGE_SIZE - 1u) &
        ~(uint64_t)(GUEST_PAGE_SIZE - 1u);
  if (end > GUEST_SPACE_SIZE || end - *first > UINT32_MAX)
    return 0;
  *length = (uint32_t)(end - *first);
  return 1;
}

static unsigned access_flags(int protection) {
  return ((protection & PROT_READ) ? kX86pMemRead : 0u) |
         ((protection & PROT_WRITE) ? kX86pMemWrite : 0u);
}

int guest_memory_init(void) {
  int ready;
  pthread_mutex_lock(&g_lock);
  if (!g_memory.sparse)
    g_memory.sparse = x86p_sparse_create();
  ready = g_memory.sparse != NULL;
  pthread_mutex_unlock(&g_lock);
  if (!ready)
    errno = ENOMEM;
  return ready ? 0 : -1;
}

const struct X86pMem *guest_memory_model(void) { return &g_memory; }

int guest_memory_map_fixed(uint32_t address, size_t size, int protection) {
  uint32_t first, length;
  Allocation *allocation;
  if (!page_span(address, size, &first, &length)) {
    errno = EINVAL;
    return -1;
  }
  if (guest_memory_init() != 0)
    return -1;
  pthread_mutex_lock(&g_lock);
  if (!x86p_sparse_range_available(g_memory.sparse, first, length)) {
    pthread_mutex_unlock(&g_lock);
    errno = EEXIST;
    return -1;
  }
  allocation = malloc(sizeof *allocation);
  if (!allocation) {
    pthread_mutex_unlock(&g_lock);
    errno = ENOMEM;
    return -1;
  }
  allocation->host = calloc(1, length);
  allocation->size = length;
  x2_engine_invalidate_memory(first, length);
  if (!allocation->host ||
      !x86p_sparse_map_access(g_memory.sparse, first, allocation->host, length,
                              access_flags(protection))) {
    free(allocation->host);
    free(allocation);
    pthread_mutex_unlock(&g_lock);
    errno = ENOMEM;
    return -1;
  }
  allocation->next = g_allocations;
  g_allocations = allocation;
  pthread_mutex_unlock(&g_lock);
  return 0;
}

int guest_memory_map_any(uint32_t first, uint32_t last, size_t alignment,
                         size_t size, int protection, uint32_t *address) {
  uint64_t candidate, step;
  if (!address || !size || size > UINT32_MAX || alignment > UINT32_MAX) {
    errno = EINVAL;
    return -1;
  }
  if (!alignment)
    alignment = GUEST_PAGE_SIZE;
  step = ((uint64_t)alignment + GUEST_PAGE_SIZE - 1u) &
         ~(uint64_t)(GUEST_PAGE_SIZE - 1u);
  candidate = ((uint64_t)first + step - 1u) / step * step;
  while (candidate + size <= last) {
    if (guest_memory_map_fixed((uint32_t)candidate, size, protection) == 0) {
      *address = (uint32_t)candidate;
      return 0;
    }
    if (errno != EEXIST)
      return -1;
    candidate += step;
  }
  errno = ENOMEM;
  return -1;
}

int guest_memory_protect(uint32_t address, size_t size, int protection) {
  uint32_t first, length;
  int changed;
  if (!page_span(address, size, &first, &length)) {
    errno = EINVAL;
    return -1;
  }
  pthread_mutex_lock(&g_lock);
  x2_engine_invalidate_memory(first, length);
  changed = x86p_sparse_protect(g_memory.sparse, first, length,
                                access_flags(protection));
  pthread_mutex_unlock(&g_lock);
  if (!changed)
    errno = EFAULT;
  return changed ? 0 : -1;
}

int guest_memory_release(uint32_t address, size_t size) {
  uint32_t first, length;
  Allocation **current;
  if (!page_span(address, size, &first, &length)) {
    errno = EINVAL;
    return -1;
  }
  pthread_mutex_lock(&g_lock);
  x2_engine_invalidate_memory(first, length);
  if (!x86p_sparse_unmap_range(g_memory.sparse, first, length)) {
    pthread_mutex_unlock(&g_lock);
    errno = ENOMEM;
    return -1;
  }
  current = &g_allocations;
  while (*current) {
    Allocation *allocation = *current;
    if (x86p_sparse_host_in_use(g_memory.sparse, allocation->host,
                                allocation->size)) {
      current = &allocation->next;
    } else {
      *current = allocation->next;
      free(allocation->host);
      free(allocation);
    }
  }
  pthread_mutex_unlock(&g_lock);
  return 0;
}

int guest_memory_is_readable(uint32_t address, size_t size) {
  int readable;
  if (!size || size > UINT32_MAX)
    return 0;
  pthread_mutex_lock(&g_lock);
  readable =
      x86p_mem_accessible(&g_memory, address, (uint32_t)size, kX86pMemRead);
  pthread_mutex_unlock(&g_lock);
  return readable;
}

int guest_memory_host_address(const void *pointer, uint32_t *address) {
  uint32_t resolved;
  int found;
  pthread_mutex_lock(&g_lock);
  found = x86p_sparse_guest_address(g_memory.sparse, pointer, 1, &resolved);
  pthread_mutex_unlock(&g_lock);
  if (found && address)
    *address = resolved;
  return found;
}

static void invalid_access(uint32_t address, size_t size,
                           const char *operation) {
  x2_log_error("guest_memory: invalid %s at guest 0x%08x for %zu bytes\n",
               operation, address, size);
  abort();
}

void *guest_memory_pointer(uint32_t address) {
  uint8_t *host = NULL;
  int found;
  if (!address)
    return NULL;
  pthread_mutex_lock(&g_lock);
  found = x86p_mem_resolve(&g_memory, address, 1, &host);
  pthread_mutex_unlock(&g_lock);
  if (!found)
    invalid_access(address, 1, "native pointer resolution");
  return host;
}

const void *guest_memory_const_pointer(uint32_t address) {
  return guest_memory_pointer(address);
}

uint32_t guest_memory_address(const void *pointer) {
  uint32_t address = 0;
  if (pointer && !guest_memory_host_address(pointer, &address)) {
    x2_log_error("guest_memory: native pointer has no guest mapping\n");
    abort();
  }
  return address;
}

int guest_memory_try_read(uint32_t address, void *destination, size_t size) {
  int read;
  if (!size || size > UINT32_MAX)
    return 0;
  pthread_mutex_lock(&g_lock);
  read = x86p_mem_read_bytes(&g_memory, address, destination, (uint32_t)size);
  pthread_mutex_unlock(&g_lock);
  return read;
}

void guest_memory_read(uint32_t address, void *destination, size_t size) {
  if (!guest_memory_try_read(address, destination, size))
    invalid_access(address, size, "native read");
}

void guest_memory_write(uint32_t address, const void *source, size_t size) {
  int wrote = 0;
  if (size && size <= UINT32_MAX) {
    pthread_mutex_lock(&g_lock);
    wrote = x86p_mem_write_bytes(&g_memory, address, source, (uint32_t)size);
    pthread_mutex_unlock(&g_lock);
  }
  if (!wrote)
    invalid_access(address, size, "native write");
}
