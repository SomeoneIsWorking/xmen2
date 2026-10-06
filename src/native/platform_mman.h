#ifndef X2_PLATFORM_MMAN_H
#define X2_PLATFORM_MMAN_H

#if defined(_WIN32)

#include <stddef.h>

/* Keep the guest-memory owner expressed in POSIX-style protection bits while
 * translating the host VM calls at this one boundary (platform_mman_win32.cpp).
 * The rest of the title must not grow platform-specific VirtualAlloc branches.
 */
#define PROT_NONE 0
#define PROT_READ 0x1
#define PROT_WRITE 0x2
#define PROT_EXEC 0x4
#define X2_MAP_FAILED NULL

#ifdef __cplusplus
extern "C" {
#endif
void *x2_map_anonymous(void *hint, size_t size, int protection);
int x2_unmap(void *address, size_t size);
int x2_protect(void *address, size_t size, int protection);
long x2_page_size(void);
#ifdef __cplusplus
}
#endif

#else

#include <sys/mman.h>
#include <unistd.h>

#define X2_MAP_FAILED MAP_FAILED

static inline void *x2_map_anonymous(void *hint, size_t size, int protection) {
  int flags = MAP_PRIVATE | MAP_ANONYMOUS | MAP_NORESERVE;
#if defined(MAP_FIXED_NOREPLACE)
  if (hint != NULL)
    flags |= MAP_FIXED_NOREPLACE;
#endif
  return mmap(hint, size, protection, flags, -1, 0);
}

static inline int x2_unmap(void *address, size_t size) {
  return munmap(address, size);
}

static inline int x2_protect(void *address, size_t size, int protection) {
  return mprotect(address, size, protection);
}

static inline long x2_page_size(void) { return sysconf(_SC_PAGESIZE); }

#endif

/*
 * Linux can request a fixed mapping without replacing an existing one.
 * Darwin has no MAP_FIXED_NOREPLACE, but an address passed without MAP_FIXED
 * is a non-destructive hint: mmap returns that address when the range is free
 * and chooses another range when it is not. Every fixed guest mapping checks
 * the returned address and unmaps a different result, giving this port the
 * same refuse-on-collision contract without MAP_FIXED's destructive overwrite.
 */
#if defined(__APPLE__) && !defined(MAP_FIXED_NOREPLACE)
#define MAP_FIXED_NOREPLACE 0
#endif

#endif
