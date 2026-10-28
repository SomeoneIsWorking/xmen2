#ifndef X2_TOUCH_RUNTIME_MENU_HPP
#define X2_TOUCH_RUNTIME_MENU_HPP

namespace x2::input {

/* The touch runtime's side of the retail menu: `native` is a menu up that a
   finger drives directly, over which no menu pad is drawn. Guest thread. */
void touch_runtime_set_menu(bool native);

} // namespace x2::input

#endif
