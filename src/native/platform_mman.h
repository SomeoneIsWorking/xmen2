#pragma once

#include <cstddef>

#if defined(_WIN32)

namespace x2::native {

/* Keep the guest-memory owner expressed in POSIX-style protection bits while
 * translating the host VM calls at this one boundary (platform_mman_win32.cpp).
 * The rest of the title must not grow platform-specific VirtualAlloc branches.
 */
inline constexpr int kProtNone = 0;
inline constexpr int kProtRead = 0x1;
inline constexpr int kProtWrite = 0x2;
inline constexpr int kProtExec = 0x4;
inline constexpr void *kMapFailed = nullptr;

void *map_anonymous(void *hint, std::size_t size, int protection);
int unmap(void *address, std::size_t size);
int protect(void *address, std::size_t size, int protection);
long page_size();

} // namespace x2::native

#else

#include <sys/mman.h>
#include <unistd.h>

namespace x2::native {

inline constexpr int kProtNone = PROT_NONE;
inline constexpr int kProtRead = PROT_READ;
inline constexpr int kProtWrite = PROT_WRITE;
inline constexpr int kProtExec = PROT_EXEC;
inline void *const kMapFailed = MAP_FAILED;

/* Linux requests a fixed mapping without replacing an existing one. Darwin has
 * no MAP_FIXED_NOREPLACE, so the address is a non-destructive hint there:
 * every fixed guest mapping checks the returned address and unmaps a different
 * result, giving the same refuse-on-collision contract. */
inline void *map_anonymous(void *hint, std::size_t size, int protection) {
  int flags = MAP_PRIVATE | MAP_ANONYMOUS | MAP_NORESERVE;
#if defined(MAP_FIXED_NOREPLACE)
  if (hint != nullptr)
    flags |= MAP_FIXED_NOREPLACE;
#endif
  return mmap(hint, size, protection, flags, -1, 0);
}

inline int unmap(void *address, std::size_t size) {
  return munmap(address, size);
}

inline int protect(void *address, std::size_t size, int protection) {
  return mprotect(address, size, protection);
}

inline long page_size() { return sysconf(_SC_PAGESIZE); }

} // namespace x2::native

#endif
