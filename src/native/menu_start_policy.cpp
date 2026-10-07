#include "menu_start_policy.hpp"

namespace x2::native {

std::uint32_t resolve_menu_start(MenuStartDispatch &dispatch) {
  const std::uint32_t result = dispatch.run_start();
  if (result != 0 || !dispatch.start_is_base_handler() ||
      !dispatch.from_focused_item() || !dispatch.back_pressed()) {
    return result;
  }
  return dispatch.run_back();
}

} // namespace x2::native
