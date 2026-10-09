#pragma once

#include <cstddef>
#include <cstdint>

namespace x2::native {

struct XboxDefaultBinding {
  uint8_t binding;
  uint8_t code;
};

/* The bindable part of the original Xbox control layout, recovered from the
   XBE action IDs, the console controller screen, and XMen2.exe's binding ABI.
 */
const XboxDefaultBinding *xbox_default_bindings(size_t *count);

} // namespace x2::native
