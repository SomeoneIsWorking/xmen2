#include "host_code_location.hpp"

#if defined(_WIN32)
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#elif !defined(__EMSCRIPTEN__)
#include <dlfcn.h>
#endif

namespace x2::diagnostics {

#if defined(_WIN32)

namespace {
char g_image[MAX_PATH];
} // namespace

bool host_code_location(std::uintptr_t address, HostCodeLocation *location) {
  HMODULE module = nullptr;
  if (!GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS |
                              GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                          reinterpret_cast<LPCSTR>(address), &module) ||
      GetModuleFileNameA(module, g_image, sizeof g_image) == 0) {
    return false;
  }
  location->image = g_image;
  location->offset = address - reinterpret_cast<std::uintptr_t>(module);
  return true;
}

#elif defined(__EMSCRIPTEN__)

bool host_code_location(std::uintptr_t, HostCodeLocation *) { return false; }

#else

bool host_code_location(std::uintptr_t address, HostCodeLocation *location) {
  Dl_info info;
  if (!dladdr(reinterpret_cast<const void *>(address), &info) ||
      !info.dli_fbase || !info.dli_fname) {
    return false;
  }
  location->image = info.dli_fname;
  location->offset = address - reinterpret_cast<std::uintptr_t>(info.dli_fbase);
  return true;
}

#endif

} // namespace x2::diagnostics
