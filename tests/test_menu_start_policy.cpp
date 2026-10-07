#include "menu_start_policy.hpp"

#include <cassert>
#include <cstdio>

namespace {

int checks;
#define CHECK(c)                                                               \
  do {                                                                         \
    assert(c);                                                                 \
    checks++;                                                                  \
  } while (0)

/* Escape on a focused item of a menu with no Start of its own: the world map,
   the codex. */
class Menu final : public x2::native::MenuStartDispatch {
public:
  std::uint32_t run_start() override {
    starts++;
    return start_result;
  }
  [[nodiscard]] bool start_is_base_handler() const override {
    return base_start;
  }
  [[nodiscard]] bool from_focused_item() const override { return focused_item; }
  [[nodiscard]] bool back_pressed() const override { return back; }
  std::uint32_t run_back() override {
    backs++;
    return back_result;
  }

  std::uint32_t start_result = 0;
  bool base_start = true;
  bool focused_item = true;
  bool back = true;
  std::uint32_t back_result = 2;
  int starts = 0;
  int backs = 0;
};

} // namespace

int main() {
  {
    Menu menu;
    const std::uint32_t result = x2::native::resolve_menu_start(menu);
    CHECK(menu.starts == 1);
    CHECK(menu.backs == 1);
    CHECK(result == 2);
  }
  {
    /* A Start that acted is the whole answer, as in retail. */
    Menu menu;
    menu.start_result = 1;
    CHECK(x2::native::resolve_menu_start(menu) == 1);
    CHECK(menu.backs == 0);
  }
  {
    /* Start alone, a pad's Start button, never closes a menu. */
    Menu menu;
    menu.back = false;
    CHECK(x2::native::resolve_menu_start(menu) == 0);
    CHECK(menu.backs == 0);
  }
  {
    /* A menu with its own Start owns the key: Options opens Advanced Options
       and the PDA closes itself. */
    Menu menu;
    menu.base_start = false;
    CHECK(x2::native::resolve_menu_start(menu) == 0);
    CHECK(menu.backs == 0);
  }
  {
    /* The menu's own update already runs Back and Start in turn. */
    Menu menu;
    menu.focused_item = false;
    CHECK(x2::native::resolve_menu_start(menu) == 0);
    CHECK(menu.backs == 0);
  }
  std::printf("test_menu_start_policy: %d checks passed\n", checks);
  return 0;
}
