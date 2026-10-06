/* platform_mman_win32.cpp -- platform_mman.h over the Windows VM. */
#include "platform_mman.h"

#include <errno.h>
#include <stdint.h>

#define WIN32_LEAN_AND_MEAN
#include <windows.h>

namespace {

DWORD page_protection(int protection) {
  const bool writable = (protection & PROT_WRITE) != 0;
  const bool readable = (protection & PROT_READ) != 0;
  if (protection & PROT_EXEC) {
    return writable ? PAGE_EXECUTE_READWRITE : PAGE_EXECUTE_READ;
  }
  if (writable) {
    return PAGE_READWRITE; // VirtualAlloc has no write-only page
  }
  return readable ? PAGE_READONLY : PAGE_NOACCESS;
}

void set_errno_from_last_error() {
  const DWORD error = GetLastError();
  errno = (error == ERROR_NOT_ENOUGH_MEMORY || error == ERROR_COMMITMENT_LIMIT)
              ? ENOMEM
              : EINVAL;
}

// Committed pages in [address, address + size) become no-access; reserved
// pages already are.
int protect_none(void *address, size_t size) {
  auto *cursor = static_cast<uint8_t *>(address);
  uint8_t *const end = cursor + size;
  while (cursor < end) {
    MEMORY_BASIC_INFORMATION info;
    if (VirtualQuery(cursor, &info, sizeof info) == 0) {
      set_errno_from_last_error();
      return -1;
    }
    uint8_t *const region_end =
        static_cast<uint8_t *>(info.BaseAddress) + info.RegionSize;
    const size_t span =
        static_cast<size_t>((region_end < end ? region_end : end) - cursor);
    if (info.State == MEM_COMMIT) {
      DWORD old_protection;
      if (!VirtualProtect(cursor, span, PAGE_NOACCESS, &old_protection)) {
        set_errno_from_last_error();
        return -1;
      }
    } else if (info.State == MEM_FREE) {
      errno = ENOMEM;
      return -1;
    }
    cursor += span;
  }
  return 0;
}

} // namespace

// A no-access mapping is only reserved, so a 4 GB guest arena costs no commit
// charge; x2_protect commits pages as they become accessible.
extern "C" void *x2_map_anonymous(void *hint, size_t size, int protection) {
  const DWORD type =
      protection == PROT_NONE ? MEM_RESERVE : (MEM_RESERVE | MEM_COMMIT);
  void *mapped = VirtualAlloc(hint, size, type, page_protection(protection));
  if (mapped == NULL) {
    set_errno_from_last_error();
    return NULL;
  }
  if (hint != NULL && mapped != hint) {
    (void)VirtualFree(mapped, 0, MEM_RELEASE);
    errno = EEXIST;
    return NULL;
  }
  return mapped;
}

// Windows releases only whole reservations, so a span inside one is
// decommitted: no-access, holding no memory, and not claimable by anyone else.
extern "C" int x2_unmap(void *address, size_t size) {
  MEMORY_BASIC_INFORMATION info;
  if (VirtualQuery(address, &info, sizeof info) == 0) {
    set_errno_from_last_error();
    return -1;
  }
  if (info.AllocationBase == address) {
    MEMORY_BASIC_INFORMATION whole;
    SIZE_T reserved = 0;
    auto *cursor = static_cast<uint8_t *>(address);
    while (VirtualQuery(cursor, &whole, sizeof whole) != 0 &&
           whole.AllocationBase == address) {
      reserved += whole.RegionSize;
      cursor += whole.RegionSize;
    }
    if (reserved == size) {
      if (!VirtualFree(address, 0, MEM_RELEASE)) {
        set_errno_from_last_error();
        return -1;
      }
      return 0;
    }
  }
  if (!VirtualFree(address, size, MEM_DECOMMIT)) {
    set_errno_from_last_error();
    return -1;
  }
  return 0;
}

extern "C" int x2_protect(void *address, size_t size, int protection) {
  if (protection == PROT_NONE) {
    return protect_none(address, size);
  }
  // Committing an already committed page keeps its contents and only sets
  // the protection.
  if (VirtualAlloc(address, size, MEM_COMMIT, page_protection(protection)) ==
      NULL) {
    set_errno_from_last_error();
    return -1;
  }
  return 0;
}

extern "C" long x2_page_size(void) {
  SYSTEM_INFO info;
  GetSystemInfo(&info);
  return static_cast<long>(info.dwPageSize);
}
