#ifndef X2_TOUCH_RUNTIME_MENU_HPP
#define X2_TOUCH_RUNTIME_MENU_HPP

namespace x2::input {

/* The touch runtime's side of the retail menu: `padded` is a retail menu up
   that has no touch path of its own, the only screen the menu pad is drawn
   over. Guest thread. */
void touch_runtime_set_menu(bool padded);

} // namespace x2::input

#endif
