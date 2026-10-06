/*
 * The retail menu model, read by the shipping reader from a fake guest image
 * laid out at the offsets docs/RE/menus.md records.
 */
#include "retail_menu_model.hpp"

#include <cstdio>
#include <cstring>
#include <map>
#include <string>
#include <vector>

namespace {

int failures;
int checks;

void check(bool ok, const std::string &what) {
  ++checks;
  if (!ok) {
    ++failures;
    std::printf("FAIL: %s\n", what.c_str());
  }
}

/* Sparse guest memory: a read succeeds only over bytes that were written. */
class FakeGuest final : public x2::native::GuestMemoryView {
public:
  bool read(std::uint32_t address, void *out,
            std::size_t bytes) const override {
    auto *dst = static_cast<std::uint8_t *>(out);
    for (std::size_t i = 0; i < bytes; ++i) {
      const auto found = bytes_.find(address + static_cast<std::uint32_t>(i));
      if (found == bytes_.end()) {
        return false;
      }
      dst[i] = found->second;
    }
    return true;
  }

  void put(std::uint32_t address, const void *data, std::size_t bytes) {
    const auto *src = static_cast<const std::uint8_t *>(data);
    for (std::size_t i = 0; i < bytes; ++i) {
      bytes_[address + static_cast<std::uint32_t>(i)] = src[i];
    }
  }
  void zero(std::uint32_t address, std::size_t bytes) {
    for (std::size_t i = 0; i < bytes; ++i) {
      bytes_[address + static_cast<std::uint32_t>(i)] = 0u;
    }
  }
  void u32(std::uint32_t address, std::uint32_t value) {
    put(address, &value, sizeof value);
  }
  void i16(std::uint32_t address, std::int16_t value) {
    put(address, &value, sizeof value);
  }
  void f32(std::uint32_t address, float value) {
    put(address, &value, sizeof value);
  }
  void text(std::uint32_t address, const std::string &value) {
    put(address, value.c_str(), value.size() + 1u);
  }

private:
  std::map<std::uint32_t, std::uint8_t> bytes_;
};

/* The image is mapped away from its linked base, so every static must be
   rebased rather than read at its linked address. */
constexpr std::uint32_t kImage = 0x01000000u;
constexpr std::uint32_t kLinked = 0x00400000u;
constexpr std::uint32_t kManager = 0x20000000u;
constexpr std::uint32_t kMenu = 0x21000000u;
constexpr std::uint32_t kPool2 = 0x22000000u;
constexpr std::uint32_t kItems = 0x23000000u;
constexpr std::uint32_t kItemStride = 0x324u;
constexpr std::uint32_t kTextBoxString = 0x24000000u;

constexpr std::uint32_t kVtText = 0x006a10ccu;
constexpr std::uint32_t kVtModel = 0x006a0304u;
constexpr std::uint32_t kVtBar = 0x006a042cu;
constexpr std::uint32_t kVtBinary = 0x006a04b4u;
constexpr std::uint32_t kVtTextBox = 0x006a1154u;
constexpr std::uint32_t kVtMenuOptions = 0x0069ebd4u;
constexpr std::uint32_t kVtMenuTeam = 0x006a2c94u;
constexpr std::uint32_t kVtCharSummary = 0x006a0244u;
constexpr std::uint32_t kVtMenuShop = 0x0069eb4cu;
constexpr std::uint32_t kVtListBox = 0x006a062cu;
constexpr std::uint32_t kVtMenuCodex = 0x0069e6d4u;
constexpr std::uint32_t kVtListCodex = 0x006a0724u;
constexpr std::uint32_t kListStore = 0x25000000u;

std::uint32_t rebased(std::uint32_t linked) {
  return linked - kLinked + kImage;
}

/* handle_str<2>: generation in the top byte, slot in the low bits. */
class Pool2 {
public:
  explicit Pool2(FakeGuest *guest) : guest_(guest) {
    guest_->zero(kPool2, 0x1008u);
    guest_->u32(kPool2, 5u);
    guest_->u32(rebased(0x00a0a81cu), kPool2);
  }
  std::uint32_t add(const std::string &value) {
    const std::uint32_t slot = next_slot_++;
    guest_->u32(kPool2 + 4u + slot * 4u, cursor_);
    guest_->text(kPool2 + 0x1008u + cursor_, value);
    cursor_ += static_cast<std::uint32_t>(value.size()) + 1u;
    return 0x05000000u | slot;
  }

private:
  FakeGuest *guest_;
  std::uint32_t next_slot_ = 7u;
  std::uint32_t cursor_ = 2u;
};

struct ItemSpec {
  std::uint32_t vtable;
  std::string name;
  std::string text;
  std::uint8_t flags;
  std::string use;
  std::string left;
  std::string right;
  std::int16_t box[5]; /* +0x70 x, +0x72 y, +0x74 w, +0x76 h, +0x7a hz */
  std::uint32_t getter;
};

std::uint32_t item_address(unsigned slot) {
  return kItems + slot * kItemStride;
}

void put_item(FakeGuest *guest, Pool2 *pool, unsigned slot,
              const ItemSpec &spec) {
  const std::uint32_t at = item_address(slot);
  guest->zero(at, kItemStride);
  guest->u32(at, rebased(spec.vtable));
  guest->u32(at + 0x04u, kMenu);
  guest->u32(at + 0x08u, pool->add(spec.name));
  if (!spec.use.empty()) {
    guest->u32(at + 0x24u, pool->add(spec.use));
  }
  if (!spec.left.empty()) {
    guest->u32(at + 0x28u, pool->add(spec.left));
  }
  if (!spec.right.empty()) {
    guest->u32(at + 0x2cu, pool->add(spec.right));
  }
  guest->u32(at + 0x34u, spec.getter);
  guest->put(at + 0x54u, &spec.flags, 1u);
  guest->i16(at + 0x70u, spec.box[0]);
  guest->i16(at + 0x72u, spec.box[1]);
  guest->i16(at + 0x74u, spec.box[2]);
  guest->i16(at + 0x76u, spec.box[3]);
  guest->i16(at + 0x7au, spec.box[4]);
  if (!spec.text.empty()) {
    guest->u32(at + 0x9cu, pool->add(spec.text));
  }
}

void link(FakeGuest *guest, unsigned slot, int up, int down) {
  const std::uint32_t at = item_address(slot);
  guest->u32(at + 0x64u, up < 0 ? 0u : item_address(static_cast<unsigned>(up)));
  guest->u32(at + 0x68u,
             down < 0 ? 0u : item_address(static_cast<unsigned>(down)));
}

/* The gamevar registry: node 3 names "sfxvolume" with getter 0x00490010. */
void put_registry(FakeGuest *guest) {
  const std::uint32_t registry = rebased(0x007acad0u);
  const std::uint32_t pool0 = rebased(0x00a0a820u);
  guest->zero(registry, 0x5ac + 0x46 * 8);
  guest->zero(pool0, 4u + 0x2000u * 4u);
  guest->u32(pool0 + 4u + 0x5b3u * 4u, 0u);
  guest->text(pool0 + 0x8008u, "sfxvolume");
  guest->u32(registry + 0x1cu + 3u * 0x10u, 0x5b3u);
  guest->u32(registry + 0x5acu + 3u * 8u, 0x00490010u);
}

/*
 * An options-shaped menu. Slots are deliberately out of navigation order and
 * have a released hole at slot 2 whose array entry is stale.
 *   slot 0  label_accept    text, up 4, down 3 (loops to the top)
 *   slot 1  backdrop        model, enabled, no links
 *   slot 2  (released; stale pointer to slot 1)
 *   slot 3  label_effects   text, start active, focused, left/right cmds
 *   slot 4  label_subtitles text, up 5, down 0
 *   slot 5  label_hidden    text, hidden; up 3, down 4 (skipped by the step)
 *   slot 6  fx_vol          bar, never focus, getter of sfxvolume, fill 0.8
 *   slot 7  subtitles_val   binary showing "On"
 *   slot 8  help            text box
 */
FakeGuest build_options() {
  FakeGuest guest;
  Pool2 pool(&guest);
  guest.u32(rebased(0x008aff18u), kManager);
  guest.u32(rebased(0x008b13ecu), 0u);
  guest.u32(kManager + 0x86090u, kMenu);
  guest.zero(kMenu, 0x1800u);
  guest.u32(kMenu, rebased(kVtMenuOptions));
  guest.text(kMenu + 0x0cu, "options");
  guest.u32(kMenu + 0x2f4u, pool.add("$MENU_BACK Back"));
  guest.u32(kMenu + 0x324u, item_address(3));
  const std::uint32_t live = 0x1fbu; /* slots 0,1,3..8 */
  guest.u32(kMenu + 0x15f0u, live);
  guest.u32(kMenu + 0x1608u, 8u);
  for (unsigned slot = 0; slot < 9u; ++slot) {
    guest.u32(kMenu + 0x160cu + slot * 4u,
              slot == 2u ? item_address(1) : item_address(slot));
  }
  put_item(&guest, &pool, 0,
           {kVtText,
            "label_accept",
            "Accept",
            0x08u,
            "saveoptions",
            "",
            "",
            {120, 60, 80, 13, 6},
            0u});
  put_item(
      &guest, &pool, 1,
      {kVtModel, "backdrop", "", 0x08u, "", "", "", {0, 0, 512, 384, 0}, 0u});
  put_item(&guest, &pool, 3,
           {kVtText,
            "label_effects_volume",
            "Effects Volume",
            0x0bu,
            "",
            "setdecrement sfxvolume",
            "setincrement sfxvolume",
            {29, 254, 130, 13, 6},
            0u});
  put_item(&guest, &pool, 4,
           {kVtText,
            "label_subtitles",
            "Subtitles",
            0x08u,
            "setincrement subtitles",
            "",
            "",
            {29, 218, 130, 13, 6},
            0u});
  put_item(&guest, &pool, 5,
           {kVtText,
            "label_hidden",
            "Hidden",
            0x0cu,
            "x",
            "",
            "",
            {29, 236, 130, 13, 6},
            0u});
  put_item(&guest, &pool, 6,
           {kVtBar,
            "fx_vol",
            "",
            0x28u,
            "",
            "",
            "",
            {272, 31, 138, 8, 3},
            0x00490010u});
  guest.f32(item_address(6) + 0x40u, 0.8F);
  put_item(&guest, &pool, 7,
           {kVtBinary,
            "subtitles",
            "On",
            0x28u,
            "",
            "",
            "",
            {300, 218, 40, 13, 6},
            0x00490240u});
  put_item(
      &guest, &pool, 8,
      {kVtTextBox, "help", "", 0x28u, "", "", "", {10, 10, 400, 40, 20}, 0u});
  guest.u32(item_address(8) + 0xb0u, kTextBoxString);
  guest.text(kTextBoxString, std::string("Caf\xe9"));
  link(&guest, 3, 0, 5);
  link(&guest, 5, 3, 4);
  link(&guest, 4, 5, 0);
  link(&guest, 0, 4, 3);
  put_registry(&guest);
  return guest;
}

const x2::menu::MenuItem *find(const x2::menu::MenuSnapshot &menu,
                               const std::string &name) {
  for (const auto &item : menu.items) {
    if (item.name == name) {
      return &item;
    }
  }
  return nullptr;
}

std::vector<std::string> row_names(const x2::menu::MenuSnapshot &menu) {
  std::vector<std::string> names;
  for (const int row : menu.rows) {
    names.push_back(menu.items[static_cast<std::size_t>(row)].name);
  }
  return names;
}

void test_reads_the_options_menu() {
  const FakeGuest guest = build_options();
  x2::menu::RetailMenuModel model(guest, kImage);
  x2::menu::MenuSnapshot menu;
  check(model.read(&menu) == x2::menu::ReadStatus::ok, "the menu reads");
  check(menu.name == "options", "the menu's name");
  check(menu.menu_class == "CMenuOptions", "the menu's class from its vtable");
  check(menu.desctext[0] == "$MENU_BACK Back", "the menu's desctext1");
  check(menu.items.size() == 8u,
        "the released slot is not an item, the stale pointer notwithstanding");

  const auto *effects = find(menu, "label_effects_volume");
  check(effects != nullptr, "the start row is read");
  if (effects == nullptr) {
    return;
  }
  check(effects->item_class == x2::menu::ItemClass::text, "a text item");
  check(effects->label == "Effects Volume", "its displayed text");
  check(effects->focused && effects->start_active(), "focused, start active");
  check(effects->has_left_right() && effects->use_command.empty(),
        "a left/right value row with no use command");
  check(effects->rect.left == 29 && effects->rect.right == 29 + 130 - 1 &&
            effects->rect.bottom == 254 + 3 &&
            effects->rect.top == 254 - 13 + 3 - 1,
        "the RECT CMenuItem::onMouse tests");

  const std::vector<std::string> expected = {"label_effects_volume",
                                             "label_subtitles", "label_accept"};
  check(row_names(menu) == expected,
        "rows in up/down order from the start row, the hidden row skipped");
  check(menu.focused >= 0 &&
            menu.items[static_cast<std::size_t>(menu.focused)].name ==
                "label_effects_volume",
        "the focused item");

  const auto *bar = find(menu, "fx_vol");
  check(bar != nullptr && bar->fill && *bar->fill > 0.79F && *bar->fill < 0.81F,
        "the bar's drawn level");
  check(effects->value_item >= 0 &&
            menu.items[static_cast<std::size_t>(effects->value_item)].name ==
                "fx_vol",
        "the volume row pairs with the bar showing its game variable");
  const auto *subtitles = find(menu, "label_subtitles");
  check(subtitles != nullptr && subtitles->value_item < 0,
        "a command naming no registered variable pairs with nothing");
  const auto *binary = find(menu, "subtitles");
  check(binary != nullptr && binary->label == "On",
        "a binary item shows its current text");
  const auto *help = find(menu, "help");
  check(help != nullptr && help->label == "Caf\xc3\xa9",
        "a text box's string, carried as UTF-8");
  const auto *hidden = find(menu, "label_hidden");
  check(hidden != nullptr && hidden->hidden() && !hidden->navigable,
        "a hidden item is not navigable");
}

void test_dynamic_text() {
  FakeGuest guest = build_options();
  const std::uint32_t at = item_address(4);
  guest.u32(at + 0xa0u, 3u);
  guest.text(rebased(0x008adab8u) + 3u * 0x20u, "Slot 1: Genosha");
  guest.u32(rebased(0x008add9cu), 1u << 3);
  x2::menu::RetailMenuModel model(guest, kImage);
  x2::menu::MenuSnapshot menu;
  check(model.read(&menu) == x2::menu::ReadStatus::ok, "reads with dynamic");
  const auto *row = find(menu, "label_subtitles");
  check(row != nullptr && row->label == "Slot 1: Genosha",
        "a live dynamic_text buffer is the label");
  guest.u32(rebased(0x008add9cu), 0u);
  check(model.read(&menu) == x2::menu::ReadStatus::ok, "reads again");
  row = find(menu, "label_subtitles");
  check(row != nullptr && row->label.empty(),
        "a released dynamic_text slot reads as empty, as getText returns it");
}

void test_popup() {
  FakeGuest guest = build_options();
  constexpr std::uint32_t kPopup = 0x25000000u;
  guest.u32(rebased(0x008b13ecu), kPopup);
  guest.u32(kPopup + 0x403cu, 1u);
  const std::uint8_t hidden = 0u;
  const std::uint8_t shown = 1u;
  guest.put(kPopup + 0x18u + 0x155du, &hidden, 1u);
  guest.put(kPopup + 0x18u + 0x1560u + 0x155du, &shown, 1u);
  x2::menu::RetailMenuModel model(guest, kImage);
  x2::menu::MenuSnapshot menu;
  check(model.read(&menu) == x2::menu::ReadStatus::ok && menu.popup_up,
        "the current popup's shown flag");
  guest.u32(kPopup + 0x403cu, 0xffffffffu);
  check(model.read(&menu) == x2::menu::ReadStatus::ok && !menu.popup_up,
        "an index outside 0..2 reads popup 0, as isUp does");
}

void test_no_menu_and_faults() {
  FakeGuest guest = build_options();
  guest.u32(kManager + 0x86090u, 0u);
  x2::menu::RetailMenuModel model(guest, kImage);
  x2::menu::MenuSnapshot menu;
  check(model.read(&menu) == x2::menu::ReadStatus::no_menu, "no menu is up");
  guest.u32(rebased(0x008aff18u), 0u);
  check(model.read(&menu) == x2::menu::ReadStatus::no_manager,
        "no manager yet");

  FakeGuest broken = build_options();
  broken.u32(kMenu + 0x1608u, 176u);
  x2::menu::RetailMenuModel broken_model(broken, kImage);
  check(broken_model.read(&menu) == x2::menu::ReadStatus::unreadable,
        "a count past 175 is refused");
  check(broken_model.failed_address() == kMenu + 0x1608u,
        "naming the count's address");

  FakeGuest missing = build_options();
  missing.u32(kMenu + 0x160cu + 4u * 4u, 0x30000000u);
  x2::menu::RetailMenuModel missing_model(missing, kImage);
  check(missing_model.read(&menu) == x2::menu::ReadStatus::unreadable,
        "an unmapped item is a fault, not a shorter menu");
  check(missing_model.failed_address() == 0x30000000u, "naming the item");
}

/* A team-shaped menu: the party as four char summaries, the second lit, in
   the detail mode the class keeps at menu+0x18d8. */
FakeGuest build_team(std::uint32_t mode) {
  FakeGuest guest;
  Pool2 pool(&guest);
  guest.u32(rebased(0x008aff18u), kManager);
  guest.u32(rebased(0x008b13ecu), 0u);
  guest.u32(kManager + 0x86090u, kMenu);
  guest.zero(kMenu, 0x1900u);
  guest.u32(kMenu, rebased(kVtMenuTeam));
  guest.text(kMenu + 0x0cu, "team");
  guest.u32(kMenu + 0x15f0u, 0x0fu);
  guest.u32(kMenu + 0x1608u, 4u);
  guest.u32(kMenu + 0x18d8u, mode);
  const char *heroes[] = {"Magneto", "Cyclops", "Wolverine", "Storm"};
  for (unsigned slot = 0; slot < 4u; ++slot) {
    guest.u32(kMenu + 0x160cu + slot * 4u, item_address(slot));
    put_item(&guest, &pool, slot,
             {kVtCharSummary,
              "char_summary0" + std::to_string(slot + 1u),
              heroes[slot],
              static_cast<std::uint8_t>(slot == 1u ? 0x29u : 0x28u),
              "",
              "",
              "",
              {364, static_cast<std::int16_t>(313 - 80 * slot), 116, 59, 0},
              0u});
  }
  put_registry(&guest);
  return guest;
}

void test_team_menu() {
  const FakeGuest guest = build_team(0u);
  x2::menu::RetailMenuModel model(guest, kImage);
  x2::menu::MenuSnapshot menu;
  check(model.read(&menu) == x2::menu::ReadStatus::ok, "the team menu reads");
  check(menu.menu_class == "CMenuTeam", "CMenuTeam from its vtable");
  check(menu.mode && *menu.mode == 0u, "the team's party mode");
  const auto *cyclops = find(menu, "char_summary02");
  check(cyclops != nullptr &&
            cyclops->item_class == x2::menu::ItemClass::char_summary &&
            cyclops->label == "Cyclops" &&
            (cyclops->flags & x2::menu::kItemFocusLit) != 0u,
        "a char summary's hero name and the lit selection");
  const auto *storm = find(menu, "char_summary04");
  check(storm != nullptr && (storm->flags & x2::menu::kItemFocusLit) == 0u,
        "an unselected hero is not lit");

  const FakeGuest details = build_team(2u);
  x2::menu::RetailMenuModel details_model(details, kImage);
  check(details_model.read(&menu) == x2::menu::ReadStatus::ok && menu.mode &&
            *menu.mode == 2u,
        "a detail tab's mode");

  const FakeGuest options = build_options();
  x2::menu::RetailMenuModel options_model(options, kImage);
  check(options_model.read(&menu) == x2::menu::ReadStatus::ok && !menu.mode,
        "a class without a mode reads none");
}

/* A shop-shaped menu: one list box of three entries drawn from the shared
   record table, the second selected, the window scrolled by one. */
FakeGuest build_list_menu(std::uint32_t entries, std::uint32_t menu_vtable,
                          std::uint32_t list_vtable) {
  FakeGuest guest;
  Pool2 pool(&guest);
  guest.u32(rebased(0x008aff18u), kManager);
  guest.u32(rebased(0x008b13ecu), 0u);
  guest.u32(kManager + 0x86090u, kMenu);
  guest.zero(kMenu, 0x1900u);
  guest.u32(kMenu, rebased(menu_vtable));
  guest.text(kMenu + 0x0cu, "shop");
  guest.u32(kMenu + 0x15f0u, 0x01u);
  guest.u32(kMenu + 0x1608u, 1u);
  guest.u32(kMenu + 0x160cu, item_address(0));
  guest.u32(kMenu + 0x324u, item_address(0));
  put_item(&guest, &pool, 0,
           {list_vtable,
            "list",
            "",
            0x0bu,
            "",
            "",
            "",
            {215, 329, 268, 185, 97},
            0u});
  const std::uint32_t at = item_address(0);
  guest.i16(at + 0xacu, 1);
  guest.u32(at + 0xbcu, kListStore);
  guest.i16(at + 0xd8u, 1);
  const std::uint8_t rows = 23u;
  guest.put(at + 0xdbu, &rows, 1u);
  guest.u32(at + 0xe0u, 8u);
  guest.zero(kListStore, 0x88u);
  guest.u32(kListStore + 0x84u, entries);
  const std::uint8_t ids[3] = {26u, 27u, 0xffu};
  guest.put(kListStore, ids, sizeof ids);
  const std::uint32_t records = rebased(0x008a83f4u);
  guest.zero(records + 26u * 0x70u, 0x70u);
  guest.text(records + 26u * 0x70u, "Magneto: Level Advance");
  guest.zero(records + 27u * 0x70u, 0x70u);
  guest.text(records + 27u * 0x70u, "Med Kit\t3");
  guest.zero(records + 0xffu * 0x70u, 0x70u);
  guest.put(records + 0xffu * 0x70u,
            "An entry name long enough that the getter cuts it at 63 bytes "
            "and no further",
            0x40u);
  put_registry(&guest);
  return guest;
}

FakeGuest build_shop(std::uint32_t entries) {
  return build_list_menu(entries, kVtMenuShop, kVtListBox);
}

void test_shop_list_box() {
  const FakeGuest guest = build_shop(3u);
  x2::menu::RetailMenuModel model(guest, kImage);
  x2::menu::MenuSnapshot menu;
  check(model.read(&menu) == x2::menu::ReadStatus::ok, "the shop reads");
  check(menu.menu_class == "CMenuShop", "CMenuShop from its vtable");
  const auto *list = find(menu, "list");
  check(list != nullptr && list->list_box.has_value(),
        "a list box carries its entries");
  if (list == nullptr || !list->list_box) {
    return;
  }
  const x2::menu::ListBoxState &box = *list->list_box;
  check(box.entries.size() == 3u, "one entry per record id");
  check(box.entries.size() == 3u &&
            box.entries[0] == "Magneto: Level Advance" &&
            box.entries[1] == "Med Kit",
        "an entry's text, cut at its first tab");
  check(box.entries.size() == 3u && box.entries[2].size() == 63u,
        "an entry's text is at most what the getter copies");
  check(box.selected == 1 && box.top == 1, "the selection and window top");
  check(box.visible_rows == 23 && box.row_height == 8, "the window's rows");
  check(box.hit.bottom == 329 && box.hit.top == 143 && box.hit.left == 215 &&
            box.hit.right == 482,
        "the list's own hit box has no half-depth lift");
  check(list->rect.bottom == 377, "while the item's base box keeps it");

  const FakeGuest options = build_options();
  x2::menu::RetailMenuModel options_model(options, kImage);
  check(options_model.read(&menu) == x2::menu::ReadStatus::ok &&
            find(menu, "label_accept") != nullptr &&
            !find(menu, "label_accept")->list_box,
        "a text item has no list");

  const FakeGuest overflow = build_shop(0x85u);
  x2::menu::RetailMenuModel overflow_model(overflow, kImage);
  check(overflow_model.read(&menu) == x2::menu::ReadStatus::unreadable &&
            overflow_model.failed_address() == kListStore + 0x84u,
        "a count past the store is a fault naming the count");
}

void test_codex_list() {
  FakeGuest guest = build_list_menu(3u, kVtMenuCodex, kVtListCodex);
  guest.u32(kMenu + 0x18d8u, 1u);
  x2::menu::RetailMenuModel model(guest, kImage);
  x2::menu::MenuSnapshot menu;
  check(model.read(&menu) == x2::menu::ReadStatus::ok, "the codex reads");
  check(menu.menu_class == "CMenuCodex", "CMenuCodex from its vtable");
  check(menu.mode && *menu.mode == 1u, "the codex's description mode");
  const auto *list = find(menu, "list");
  check(list != nullptr && list->list_box &&
            list->list_box->entries.size() == 3u &&
            list->list_box->entries[0] == "Magneto: Level Advance" &&
            list->list_box->selected == 1,
        "a ListCodex reads as the list box it extends");
}

void test_linked_base_is_not_read() {
  const FakeGuest guest = build_options();
  x2::menu::RetailMenuModel model(guest, kLinked);
  x2::menu::MenuSnapshot menu;
  check(model.read(&menu) == x2::menu::ReadStatus::unreadable,
        "the manager is read through the mapped base only");
}

} // namespace

int main() {
  test_reads_the_options_menu();
  test_dynamic_text();
  test_popup();
  test_no_menu_and_faults();
  test_linked_base_is_not_read();
  test_team_menu();
  test_shop_list_box();
  test_codex_list();
  std::printf("%d/%d check(s) passed\n", checks - failures, checks);
  return failures == 0 ? 0 : 1;
}
