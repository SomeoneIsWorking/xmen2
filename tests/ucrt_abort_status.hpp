#pragma once

#include <cstdint>

namespace x2::test {

/* UCRT abort(): exit 3, or STATUS_STACK_BUFFER_OVERRUN where it can fast-fail.
 */
inline bool is_ucrt_abort_status(std::intptr_t status) {
  return status == 3 || static_cast<std::uint32_t>(status) == 0xc0000409u;
}

} // namespace x2::test
