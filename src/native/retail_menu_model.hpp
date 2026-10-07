#ifndef X2_RETAIL_MENU_MODEL_HPP
#define X2_RETAIL_MENU_MODEL_HPP

#include "guest_memory_view.hpp"

#include <array>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

/*
 * The active retail CMenu read out of guest memory as plain data: its items,
 * their displayed text, flags and hit boxes, and the rows in the order the
 * game's own up/down navigation visits them. Every field and its evidence is
 * in docs/RE/menus.md. Checked reads only, no guest call, no lock, so it is
 * safe from any host thread.
 */
namespace x2::menu {

/* The item classes the factory FUN_005bf220 builds, named from RTTI. */
enum class ItemClass : std::uint8_t {
  unknown,
  item,
  text,
  text_box,
  model,
  actor_model,
  effect,
  bar,
  binary,
  list,
  list_box,
  list_codex,
  list_items,
  list_cycle,
  list_chars,
  char_summary,
  skills,
};

const char *item_class_name(ItemClass item_class);

/* The RECT CMenuItem::onMouse (FUN_005bc1b0) tests, in scene units. */
struct SceneRect {
  int left = 0;
  int top = 0;
  int right = 0;
  int bottom = 0;
};

/* item+0x54. */
inline constexpr std::uint8_t kItemFocusLit = 0x01u;
inline constexpr std::uint8_t kItemStartActive = 0x02u;
inline constexpr std::uint8_t kItemHidden = 0x04u;
inline constexpr std::uint8_t kItemEnabled = 0x08u;
inline constexpr std::uint8_t kItemNavigationSkip = 0x10u;
inline constexpr std::uint8_t kItemNeverFocus = 0x20u;

/* A CMenuItemListBox's entries and the window it shows them through. */
struct ListBoxState {
  /* What the box's entry getter (0x005c23c0) returns: the entry record's
     text up to its first tab. */
  std::vector<std::string> entries;
  /* Each entry's further columns: the record's text after its first tab,
     tabs as single spaces; empty for a one-column entry. */
  std::vector<std::string> values;
  /* First entry in the window, item+0xd8. */
  int top = 0;
  /* item+0xac; -1 when none. */
  int selected = -1;
  /* Rows the window holds, item+0xdb. */
  int visible_rows = 0;
  /* item+0xe0, in scene units. */
  int row_height = 0;
  /* The box the list's own onMouse (0x005c0e10) tests, without the base
     class's half-depth lift. Row k spans `row_height` down from
     bottom - k * row_height. */
  SceneRect hit;
};

struct MenuItem {
  std::uint32_t address = 0;
  unsigned slot = 0;
  ItemClass item_class = ItemClass::unknown;
  std::string name;
  /* What CMenuItemText::getText (or the text box's) returns, as UTF-8. Raw:
     style escapes such as "~05" and prompt tokens such as "$MENU_BACK" are the
     game's own. */
  std::string label;
  std::string use_command;
  std::string left_command;
  std::string right_command;
  SceneRect rect;
  std::uint8_t flags = 0;
  bool focused = false;
  /* CMenuItem::nextInDirection's test (0x005bccd0): enabled, neither hidden
     nor flag 0x10. */
  bool navigable = false;
  /* CMenuItemBar's drawn level, item+0x40. */
  std::optional<float> fill;
  /* A list box's entries; set for ItemClass::list_box and list_codex. */
  std::optional<ListBoxState> list_box;
  /* The item showing the game variable this row's command changes. */
  int value_item = -1;
  int link_left = -1;
  int link_right = -1;
  int link_up = -1;
  int link_down = -1;

  bool enabled() const { return (flags & kItemEnabled) != 0u; }
  bool hidden() const { return (flags & kItemHidden) != 0u; }
  bool never_focus() const { return (flags & kItemNeverFocus) != 0u; }
  bool start_active() const { return (flags & kItemStartActive) != 0u; }
  bool has_left_right() const {
    return !left_command.empty() || !right_command.empty();
  }
};

struct MenuSnapshot {
  std::uint32_t address = 0;
  std::string name;
  std::string menu_class;
  /* The menu's own desctext1..5 attributes (menu+0x2f4). */
  std::array<std::string, 5> desctext;
  /* Live items, in name-slot order. */
  std::vector<MenuItem> items;
  /* Indices into `items`, in up/down navigation order. */
  std::vector<int> rows;
  int focused = -1;
  /* A CPopupDialog is shown; it takes the pointer before any menu does. */
  bool popup_up = false;
  /* The class's own screen state, for the classes that keep one. CMenuTeam:
     0 the party, 1 the roster, 2..6 a hero's detail tabs. CMenuShop: bit 0
     set for the stash. CMenuCodex: 1 while the description is shown. */
  std::optional<std::uint32_t> mode;
};

enum class ReadStatus : std::uint8_t {
  ok,
  no_manager,
  no_menu,
  unreadable,
};

const char *read_status_name(ReadStatus status);

class RetailMenuModel {
public:
  /* `image_base` is where XMen2.exe is mapped. */
  RetailMenuModel(const native::GuestMemoryView &memory,
                  std::uint32_t image_base);

  ReadStatus read(MenuSnapshot *out);

  /* The guest address the last unreadable read failed at. */
  std::uint32_t failed_address() const { return failed_; }

private:
  bool read_u32(std::uint32_t address, std::uint32_t *out);
  bool read_popup(bool *up);
  bool read_bytes(std::uint32_t address, void *out, std::size_t bytes);
  bool read_c_string(std::uint32_t address, std::size_t capacity,
                     std::string *out);
  bool pool_string(std::uint32_t handle, std::string *out);
  bool registry_getter(const std::string &name, std::uint32_t *getter);
  bool read_item(std::uint32_t address, unsigned slot, MenuItem *out,
                 std::array<std::uint32_t, 4> *links, std::uint32_t *getter);
  bool read_label(const std::uint8_t *header, ItemClass item_class,
                  std::string *out);
  bool read_list_box(std::uint32_t address, const std::uint8_t *header,
                     ListBoxState *out);
  int step(const MenuSnapshot &menu, int from, int direction) const;
  void order_rows(MenuSnapshot *menu) const;
  bool pair_values(MenuSnapshot *menu,
                   const std::vector<std::uint32_t> &getters);

  const native::GuestMemoryView &memory_;
  std::uint32_t image_base_;
  std::uint32_t failed_ = 0;
};

/* The running game's menu, through LiveGuestMemory. */
ReadStatus read_live_menu(MenuSnapshot *out, std::uint32_t *failed_address);

} // namespace x2::menu

#endif
