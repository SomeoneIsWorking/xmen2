#include "guest_memory.h"

#include "platform_posix.h"
#include <errno.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/mman.h>

/* Only guest_memory.c is compiled with these syscall names redirected. The
 * production page table and protection grouping execute unchanged against
 * real anonymous memory, with the requested host's alignment enforced here. */
static long host_page_size;
static unsigned queries, mappings, protections, alignment_failures;
static unsigned checks, failures;

static void check(int condition, const char *description) {
  checks++;
  if (!condition) {
    fprintf(stderr, "FAIL: %s\n", description);
    failures++;
  }
}

void x2_log_error(const char *format, ...) {
  va_list args;
  va_start(args, format);
  vfprintf(stderr, format, args);
  va_end(args);
}

long x2_test_sysconf(int name) {
  check(name == _SC_PAGESIZE, "queries the host page size");
  queries++;
  return host_page_size;
}

void *x2_test_mmap(void *address, size_t length, int protection, int flags,
                   int fd, off_t offset) {
  size_t granule = (size_t)host_page_size;
  mappings++;
  check(address == NULL, "reserves an independently placed guest arena");
  void *allocation =
      mmap(address, length + granule, protection, flags, fd, offset);
  if (allocation == MAP_FAILED)
    return allocation;
  uintptr_t start = (uintptr_t)allocation;
  uintptr_t aligned = (start + granule - 1u) & ~(uintptr_t)(granule - 1u);
  size_t prefix = aligned - start;
  if (prefix)
    check(munmap(allocation, prefix) == 0, "trims arena alignment prefix");
  check(munmap((void *)(aligned + length), granule - prefix) == 0,
        "trims arena alignment suffix");
  return (void *)aligned;
}

int x2_test_mprotect(void *address, size_t length, int protection) {
  size_t granule = (size_t)host_page_size;
  protections++;
  if ((uintptr_t)address % granule) {
    alignment_failures++;
    errno = EINVAL;
    return -1;
  }
  size_t rounded_length = (length + granule - 1u) & ~(granule - 1u);
  return mprotect(address, rounded_length, protection);
}

static void exercise_pages(void) {
  const uint32_t base = 0x00400000u;
  const uint32_t first = base + 4096u;
  const uint32_t second = base + 8192u;
  check(guest_memory_init() == 0, "initializes the reserved arena");
  check(guest_memory_init() == 0 && queries == 1 && mappings == 1,
        "initialization queries and reserves once");
  check(guest_memory_map_fixed(0x80000u, 4096, PROT_READ | PROT_WRITE) == 0,
        "maps the boot return trampoline");
  check(guest_memory_map_fixed(base, 32768, PROT_READ | PROT_WRITE) == 0,
        "maps a multi-page image");
  if (failures)
    return;
  unsigned char *a = guest_memory_pointer(first);
  unsigned char *b = guest_memory_pointer(second);
  *a = 17;
  *b = 23;
  check(guest_memory_protect(first, 4096, PROT_NONE) == 0,
        "decommits one guest page");
  check(!guest_memory_is_readable(first, 4096) &&
            guest_memory_is_readable(second, 4096),
        "guest permissions stay independent within a host granule");
  *b = 29;
  check(*b == 29, "a neighboring committed page remains writable");
  check(guest_memory_protect(first, 4096, PROT_READ | PROT_WRITE) == 0,
        "restores one guest page");
  check(*a == 17, "protection changes preserve page contents");
  errno = 0;
  check(guest_memory_map_fixed(second, 4096, PROT_READ) == -1 &&
            errno == EEXIST,
        "refuses overlapping guest allocations");
  check(guest_memory_release(first, 4096) == 0 &&
            !guest_memory_is_readable(first, 4096),
        "releases one guest page without publishing it as readable");
  check(*b == 29, "release preserves its committed neighbor");
  check(guest_memory_map_fixed(first, 4096, PROT_READ | PROT_WRITE) == 0,
        "maps a guest page unaligned to the larger host granule");
  *a = 31;
  check(*a == 31 && *b == 29, "remapping preserves neighboring data");
  check(guest_memory_release(base, 32768) == 0,
        "releases the multi-page image");
  check(!guest_memory_is_readable(base, 32768), "released image is unreadable");
  check(guest_memory_release(0x80000u, 4096) == 0,
        "releases the boot return trampoline");
  check(protections > 0 && alignment_failures == 0,
        "every host protection call obeys the measured granule");
  check(munmap((void *)g_guest_memory_base, (size_t)UINT64_C(1) << 32) == 0,
        "releases the test arena");
}

int main(int argc, char **argv) {
  if (argc != 2)
    return 2;
  char *end;
  host_page_size = strtol(argv[1], &end, 10);
  if (*end)
    return 2;
  if (host_page_size == 4096 || host_page_size == 16384) {
    exercise_pages();
  } else {
    errno = 0;
    check(guest_memory_init() == -1 && errno == EINVAL,
          "refuses an unsupported host granule");
    check(queries == 1 && mappings == 0 && protections == 0,
          "refuses before reserving or mutating host memory");
  }
  printf("guest_memory: %u/%u checks passed; host page %ld; %u protection "
         "calls, %u alignment failures\n",
         checks - failures, checks, host_page_size, protections,
         alignment_failures);
  return failures ? 1 : 0;
}
