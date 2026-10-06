#include "guest_memory_view.hpp"

#include "guest_memory.h"

namespace x2::native {

bool LiveGuestMemory::read(std::uint32_t address, void *out,
                           std::size_t bytes) const {
  return address != 0u && guest_memory_try_read(address, out, bytes) != 0;
}

} // namespace x2::native
