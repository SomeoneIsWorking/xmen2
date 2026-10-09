#pragma once

#include <cstdint>

namespace x2::native {

/* Publish the watched dword after a native CRT bulk write that fully covers
   it. Ordinary JIT guest stores are handled by the x86 runtime helpers. */
void crt_write_watch_dst(uint32_t destination, uint32_t size);

} // namespace x2::native
