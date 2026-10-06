#include "retail_menu_model.hpp"

#include "x86rt_native.h"

namespace x2::menu {

ReadStatus read_live_menu(MenuSnapshot *out, std::uint32_t *failed_address) {
  *failed_address = 0u;
  const std::uint32_t image = x86_module_base("XMen2.exe");
  if (image == 0u) {
    *out = MenuSnapshot{};
    return ReadStatus::no_manager;
  }
  const native::LiveGuestMemory memory;
  RetailMenuModel model(memory, image);
  const ReadStatus status = model.read(out);
  *failed_address = model.failed_address();
  return status;
}

} // namespace x2::menu
