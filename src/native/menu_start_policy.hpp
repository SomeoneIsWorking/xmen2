#pragma once

#include <cstdint>

namespace x2::native {

/* Retail Escape is Pause (Start, bits 19 and 20) and Back (bit 21). A focused
   item's update (0x005bbfe0) dispatches Start first and only otherwise Back, so
   a menu whose Start is the base handler (0x005ad730) loses Escape; see
   docs/RE/menus.md, "Escape in a menu with a focused item". */
class MenuStartDispatch {
public:
  MenuStartDispatch() = default;
  MenuStartDispatch(const MenuStartDispatch &) = delete;
  MenuStartDispatch &operator=(const MenuStartDispatch &) = delete;
  MenuStartDispatch(MenuStartDispatch &&) = delete;
  MenuStartDispatch &operator=(MenuStartDispatch &&) = delete;
  virtual ~MenuStartDispatch() = default;

  /* Non-zero when the retail Start handler acted. */
  virtual std::uint32_t run_start() = 0;
  [[nodiscard]] virtual bool start_is_base_handler() const = 0;
  [[nodiscard]] virtual bool from_focused_item() const = 0;
  [[nodiscard]] virtual bool back_pressed() const = 0;
  virtual std::uint32_t run_back() = 0;
};

/* Start, then Back when Start did nothing and the same press carried Back.
   Returns the result of the handler that ran last. */
std::uint32_t resolve_menu_start(MenuStartDispatch &dispatch);

} // namespace x2::native
