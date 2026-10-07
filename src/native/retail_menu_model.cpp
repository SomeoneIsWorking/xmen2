#include "retail_menu_model.hpp"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <string_view>
#include <unordered_map>

namespace x2::menu {
namespace {

/* XMen2.exe is linked at 0x00400000; every static below is an RVA. */
inline constexpr std::uint32_t kLinkedBase = 0x00400000u;

/* `FUN_005d88b0` stores the CMenuMgr singleton here. */
inline constexpr std::uint32_t kManagerRva = 0x004aff18u;
/* `FUN_005d83d0`: MOV EAX,[ECX+0x86090]. */
inline constexpr std::uint32_t kActiveMenu = 0x86090u;

/* CMenu. */
inline constexpr std::uint32_t kMenuName = 0x0cu;
inline constexpr std::size_t kMenuNameBytes = 64u;
/* `FUN_005bc6d0` called from CMenu::parse (FUN_005ad830). */
inline constexpr std::uint32_t kMenuDesctext = 0x2f4u;
inline constexpr std::uint32_t kMenuFocus = 0x324u;
inline constexpr std::uint32_t kMenuLiveBits = 0x15f0u;
inline constexpr std::uint32_t kMenuItemCount = 0x1608u;
inline constexpr std::uint32_t kMenuItemArray = 0x160cu;
inline constexpr unsigned kMenuSlots = 0xafu;

/* CMenuItem, from its constructor FUN_005bd860 and parse FUN_005bc7a0. */
inline constexpr std::size_t kItemHeaderBytes = 0xb8u;
inline constexpr std::uint32_t kItemName = 0x08u;
inline constexpr std::uint32_t kItemUse = 0x24u;
inline constexpr std::uint32_t kItemLeftCommand = 0x28u;
inline constexpr std::uint32_t kItemRightCommand = 0x2cu;
/* The gamevar getter `gamevar` resolves to through the registry's slot 1. */
inline constexpr std::uint32_t kItemGamevarGetter = 0x34u;
inline constexpr std::uint32_t kItemFill = 0x40u;
inline constexpr std::uint32_t kItemFlags = 0x54u;
/* left, right, up, down: FUN_005bbc30 and FUN_005aec00. */
inline constexpr std::uint32_t kItemLinks = 0x5cu;
inline constexpr std::uint32_t kItemBoxLeft = 0x70u;
inline constexpr std::uint32_t kItemBoxY = 0x72u;
inline constexpr std::uint32_t kItemBoxWidth = 0x74u;
inline constexpr std::uint32_t kItemBoxHeight = 0x76u;
inline constexpr std::uint32_t kItemBoxHalfZ = 0x7au;
/* CMenuItemText::getText (0x005c67f0). */
inline constexpr std::uint32_t kTextHandle = 0x9cu;
inline constexpr std::uint32_t kTextDynamicSlot = 0xa0u;
/* CMenuItemTextBox::getText (0x005c7320): [+0xb0] else [+0xac]. */
inline constexpr std::uint32_t kTextBoxPrimary = 0xb0u;
inline constexpr std::uint32_t kTextBoxFallback = 0xacu;
inline constexpr std::size_t kTextBoxBytes = 4096u;

/* CMenuItemText dynamic_text buffers, FUN_005c6720: 20 x 32 bytes. */
inline constexpr std::uint32_t kDynamicTextRva = 0x004adab8u;
inline constexpr std::uint32_t kDynamicTextLiveRva = 0x004add9cu;
inline constexpr std::uint32_t kDynamicTextSlots = 20u;
inline constexpr std::uint32_t kDynamicTextBytes = 0x20u;

/* The gamevar registry FUN_0055c8d0: a ratl tree whose node i keys a
   handle_str<0> name and whose getter is at +0x5ac + i*8 (FUN_0055c020). */
inline constexpr std::uint32_t kRegistryRva = 0x003acad0u;
inline constexpr std::uint32_t kRegistryNodeKeys = 0x1cu;
inline constexpr std::uint32_t kRegistryNodeStride = 0x10u;
inline constexpr std::uint32_t kRegistryGetters = 0x5acu;
inline constexpr std::uint32_t kRegistryNodes = 0x46u;

/* CPopupDialog singleton (FUN_005eb300) and its isUp (0x005e9e30): the shown
   flag of popup [+0x403c], or of popup 0 when that index is out of 0..2. */
inline constexpr std::uint32_t kPopupRva = 0x004b13ecu;
inline constexpr std::uint32_t kPopupCurrent = 0x403cu;
inline constexpr std::uint32_t kPopupShown = 0x18u + 0x155du;
inline constexpr std::uint32_t kPopupStride = 0x1560u;
inline constexpr std::int32_t kPopupCount = 3;

/* CMenuItemListBox (vtable 0x006a062c); see docs/RE/menus.md. */
inline constexpr std::size_t kListBoxBytes = 0xe4u;
inline constexpr std::uint32_t kListSelected = 0xacu;
inline constexpr std::uint32_t kListStore = 0xbcu;
inline constexpr std::uint32_t kListTop = 0xd8u;
inline constexpr std::uint32_t kListVisibleRows = 0xdbu;
inline constexpr std::uint32_t kListRowHeight = 0xe0u;
/* The store: one byte record id per entry, the count at +0x84. */
inline constexpr std::uint32_t kListStoreCount = 0x84u;
inline constexpr std::uint32_t kListStoreIds = 0x84u;
/* Every list box draws its entries from one table of 0x70-byte records. */
inline constexpr std::uint32_t kListRecordsRva = 0x004a83f4u;
inline constexpr std::uint32_t kListRecordStride = 0x70u;
/* The entry getter copies at most 0x3f bytes. */
inline constexpr std::size_t kListEntryBytes = 0x3fu;
/* The record's text ends where its entry handle starts, at +0x58. */
inline constexpr std::size_t kListRecordTextBytes = 0x58u;

/* CMenuItemCharSummary's draw (FUN_005bdf50) masks a locked name when bit 0
   is set. */
inline constexpr std::uint32_t kSummaryMask = 0xbcu;

/* CMenuItem::nextInDirection's visited list. */
inline constexpr int kStepVisits = 32;

struct ClassEntry {
  std::uint32_t vtable;
  ItemClass item_class;
};

inline constexpr ClassEntry kItemClasses[] = {
    {0x006a007cu, ItemClass::item},
    {0x006a00fcu, ItemClass::actor_model},
    {0x006a0244u, ItemClass::char_summary},
    {0x006a0304u, ItemClass::model},
    {0x006a0384u, ItemClass::effect},
    {0x006a042cu, ItemClass::bar},
    {0x006a04b4u, ItemClass::binary},
    {0x006a053cu, ItemClass::list},
    {0x006a062cu, ItemClass::list_box},
    {0x006a0724u, ItemClass::list_codex},
    {0x006a081cu, ItemClass::list_items},
    {0x006a0914u, ItemClass::list_cycle},
    {0x006a0cecu, ItemClass::list_chars},
    {0x006a0f14u, ItemClass::skills},
    {0x006a10ccu, ItemClass::text},
    {0x006a1154u, ItemClass::text_box},
};

struct MenuClassEntry {
  std::uint32_t vtable;
  const char *name;
};

inline constexpr MenuClassEntry kMenuClasses[] = {
    {0x0069e41cu, "CMenu"},
    {0x0069e4c4u, "CMenuAutoMap"},
    {0x0069e6d4u, "CMenuCodex"},
    {0x0069e964u, "CMenuDangerRoom"},
    {0x0069eb4cu, "CMenuShop"},
    {0x0069ebd4u, "CMenuOptions"},
    {0x0069ec5cu, "CMenuTrivia"},
    {0x0069ece4u, "CMenuWorldMap"},
    {0x0069ed6cu, "CMenuPDA"},
    {0x0069edf4u, "CMenuOptionsController"},
    {0x0069ee7cu, "CMenuReviewPaths"},
    {0x0069ef04u, "CMenuImageViewer"},
    {0x0069efa4u, "CMenuOnline"},
    {0x0069f06cu, "CMenuTextEntry"},
    {0x0069f134u, "CMenuMain"},
    {0x0069f1d4u, "CMenuCredits"},
    {0x0069f274u, "CMenuHost"},
    {0x0069f33cu, "CMenuJoin"},
    {0x0069f404u, "CMenuCampaignLobby"},
    {0x0069f4ccu, "CMenuPlayersList"},
    {0x0069f594u, "CMenuRegion"},
    {0x0069f65cu, "CMenuGameOptions"},
    {0x0069f724u, "CMenuPlayerGameOptions"},
    {0x0069f7ecu, "CMenuHeroList"},
    {0x0069f8b4u, "CMenuGamesList"},
    {0x0069f97cu, "CMenuPersonal"},
    {0x0069fa1cu, "CMenuLoading"},
    {0x006a13bcu, "CMenuMovie"},
    {0x006a1f64u, "CMenuSebas"},
    {0x006a2c94u, "CMenuTeam"},
};

ItemClass classify_item(std::uint32_t vtable, std::uint32_t image_base) {
  for (const ClassEntry &entry : kItemClasses) {
    if (entry.vtable - kLinkedBase + image_base == vtable) {
      return entry.item_class;
    }
  }
  return ItemClass::unknown;
}

/* A class's own screen state, read beside the items. */
struct MenuModeEntry {
  std::string_view menu_class;
  std::uint32_t offset;
};

/* CMenuTeam::onMouse (0x005e25c0) and CMenuCodex::onMouse (0x005b0c80) switch
   on menu+0x18d8; CMenuShop::onMouse (0x005d3400) picks its stash tabs on
   menu+0x18e8 bit 0. */
inline constexpr MenuModeEntry kMenuModes[] = {
    {"CMenuTeam", 0x18d8u},
    {"CMenuCodex", 0x18d8u},
    {"CMenuShop", 0x18e8u},
};

const char *classify_menu(std::uint32_t vtable, std::uint32_t image_base) {
  for (const MenuClassEntry &entry : kMenuClasses) {
    if (entry.vtable - kLinkedBase + image_base == vtable) {
      return entry.name;
    }
  }
  return "unknown";
}

/* Every class whose vtable slot 31 is CMenuItemText::getText. */
bool uses_text_get_text(ItemClass item_class) {
  switch (item_class) {
  case ItemClass::text:
  case ItemClass::bar:
  case ItemClass::binary:
  case ItemClass::list:
  case ItemClass::list_box:
  case ItemClass::list_codex:
  case ItemClass::list_items:
  case ItemClass::list_cycle:
  case ItemClass::list_chars:
  case ItemClass::char_summary:
  case ItemClass::skills:
    return true;
  default:
    return false;
  }
}

std::uint32_t field_u32(const std::uint8_t *header, std::uint32_t offset) {
  std::uint32_t value = 0;
  std::memcpy(&value, header + offset, sizeof value);
  return value;
}

int field_i16(const std::uint8_t *header, std::uint32_t offset) {
  std::int16_t value = 0;
  std::memcpy(&value, header + offset, sizeof value);
  return value;
}

/* The last word of a command, which for setincrement/setdecrement is the
   game variable it changes. */
std::string command_target(const std::string &command) {
  const std::size_t end = command.find_last_not_of(' ');
  if (end == std::string::npos) {
    return {};
  }
  const std::size_t space = command.find_last_of(' ', end);
  const std::size_t begin = space == std::string::npos ? 0u : space + 1u;
  return command.substr(begin, end + 1u - begin);
}

} // namespace

const char *item_class_name(ItemClass item_class) {
  switch (item_class) {
  case ItemClass::item:
    return "CMenuItem";
  case ItemClass::text:
    return "CMenuItemText";
  case ItemClass::text_box:
    return "CMenuItemTextBox";
  case ItemClass::model:
    return "CMenuItemModel";
  case ItemClass::actor_model:
    return "CMenuItemActorModel";
  case ItemClass::effect:
    return "CMenuItemEffect";
  case ItemClass::bar:
    return "CMenuItemBar";
  case ItemClass::binary:
    return "CMenuItemBinary";
  case ItemClass::list:
    return "CMenuItemList";
  case ItemClass::list_box:
    return "CMenuItemListBox";
  case ItemClass::list_codex:
    return "CMenuItemListCodex";
  case ItemClass::list_items:
    return "CMenuItemListItems";
  case ItemClass::list_cycle:
    return "CMenuItemListCycle";
  case ItemClass::list_chars:
    return "CMenuItemListChars";
  case ItemClass::char_summary:
    return "CMenuItemCharSummary";
  case ItemClass::skills:
    return "CMenuItemSkills";
  case ItemClass::unknown:
    break;
  }
  return "unknown";
}

const char *read_status_name(ReadStatus status) {
  switch (status) {
  case ReadStatus::ok:
    return "ok";
  case ReadStatus::no_manager:
    return "no menu manager yet";
  case ReadStatus::no_menu:
    return "no menu is up";
  case ReadStatus::unreadable:
    break;
  }
  return "unreadable";
}

RetailMenuModel::RetailMenuModel(const native::GuestMemoryView &memory,
                                 std::uint32_t image_base)
    : reader_(memory, image_base) {}

bool RetailMenuModel::registry_getter(const std::string &name,
                                      std::uint32_t *getter) {
  *getter = 0u;
  const std::uint32_t registry = reader_.image(kRegistryRva);
  for (std::uint32_t node = 0; node < kRegistryNodes; ++node) {
    std::uint32_t key = 0;
    if (!reader_.u32(registry + kRegistryNodeKeys + node * kRegistryNodeStride,
                     &key)) {
      return false;
    }
    std::string key_name;
    if (!reader_.pool0_string(key, &key_name)) {
      return false;
    }
    if (key_name == name) {
      return reader_.u32(registry + kRegistryGetters + node * 8u, getter);
    }
  }
  return true;
}

bool RetailMenuModel::read_label(const std::uint8_t *header,
                                 ItemClass item_class, std::string *out) {
  out->clear();
  std::string bytes;
  if (item_class == ItemClass::text_box) {
    std::uint32_t text = field_u32(header, kTextBoxPrimary);
    if (text == 0u) {
      text = field_u32(header, kTextBoxFallback);
    }
    if (text != 0u && !reader_.c_string(text, kTextBoxBytes, &bytes)) {
      return false;
    }
    *out = native::latin1_to_utf8(bytes);
    return true;
  }
  if (!uses_text_get_text(item_class)) {
    return true;
  }
  const std::uint32_t dynamic = field_u32(header, kTextDynamicSlot);
  if (dynamic == 0u) {
    return reader_.pool2_string(field_u32(header, kTextHandle), out);
  }
  if (dynamic >= kDynamicTextSlots) {
    return reader_.fail(reader_.image(kDynamicTextLiveRva));
  }
  std::uint32_t live = 0;
  if (!reader_.u32(reader_.image(kDynamicTextLiveRva), &live)) {
    return false;
  }
  /* A released dynamic slot reads as "", as getText returns it. */
  if ((live & (1u << dynamic)) != 0u &&
      !reader_.c_string(
          reader_.image(kDynamicTextRva + dynamic * kDynamicTextBytes),
          kDynamicTextBytes, &bytes)) {
    return false;
  }
  *out = native::latin1_to_utf8(bytes);
  return true;
}

bool RetailMenuModel::read_list_box(std::uint32_t address,
                                    const std::uint8_t *header,
                                    ListBoxState *out) {
  std::uint8_t box[kListBoxBytes];
  if (!reader_.bytes(address, box, sizeof box)) {
    return false;
  }
  out->selected = field_i16(box, kListSelected);
  out->top = field_i16(box, kListTop);
  out->visible_rows = box[kListVisibleRows];
  out->row_height = static_cast<int>(field_u32(box, kListRowHeight));
  out->hit.left = field_i16(header, kItemBoxLeft);
  out->hit.right = out->hit.left + field_i16(header, kItemBoxWidth) - 1;
  out->hit.bottom = field_i16(header, kItemBoxY);
  out->hit.top = out->hit.bottom - field_i16(header, kItemBoxHeight) - 1;
  const std::uint32_t store = field_u32(box, kListStore);
  if (store == 0u) {
    return true;
  }
  std::uint32_t count = 0;
  if (!reader_.u32(store + kListStoreCount, &count)) {
    return false;
  }
  if (count > kListStoreIds) {
    return reader_.fail(store + kListStoreCount);
  }
  std::uint8_t ids[kListStoreIds];
  if (count > 0u && !reader_.bytes(store, ids, count)) {
    return false;
  }
  for (std::uint32_t i = 0; i < count; ++i) {
    char text[kListRecordTextBytes];
    if (!reader_.bytes(
            reader_.image(kListRecordsRva + ids[i] * kListRecordStride), text,
            sizeof text)) {
      return false;
    }
    std::string entry;
    std::vector<std::string> columns;
    for (std::size_t at = 0; at < sizeof text && text[at] != '\0'; ++at) {
      const char c = text[at];
      if (c == '\t') {
        columns.emplace_back();
      } else if (!columns.empty()) {
        columns.back().push_back(c);
      } else if (at < kListEntryBytes) {
        entry.push_back(c);
      }
    }
    for (std::string &column : columns) {
      column = native::latin1_to_utf8(column);
    }
    out->entries.push_back(native::latin1_to_utf8(entry));
    out->columns.push_back(std::move(columns));
  }
  return true;
}

bool RetailMenuModel::read_item(std::uint32_t address, unsigned slot,
                                MenuItem *out,
                                std::array<std::uint32_t, 4> *links,
                                std::uint32_t *getter) {
  std::uint8_t header[kItemHeaderBytes];
  if (!reader_.bytes(address, header, sizeof header)) {
    return false;
  }
  out->address = address;
  out->slot = slot;
  out->item_class = classify_item(field_u32(header, 0u), reader_.image_base());
  out->flags = header[kItemFlags];
  if (!reader_.pool2_string(field_u32(header, kItemName), &out->name) ||
      !reader_.pool2_string(field_u32(header, kItemUse), &out->use_command) ||
      !reader_.pool2_string(field_u32(header, kItemLeftCommand),
                            &out->left_command) ||
      !reader_.pool2_string(field_u32(header, kItemRightCommand),
                            &out->right_command) ||
      !read_label(header, out->item_class, &out->label)) {
    return false;
  }
  const int left = field_i16(header, kItemBoxLeft);
  const int y = field_i16(header, kItemBoxY);
  const int half = field_i16(header, kItemBoxHalfZ) / 2;
  out->rect.left = left;
  out->rect.right = left + field_i16(header, kItemBoxWidth) - 1;
  out->rect.bottom = y + half;
  out->rect.top = y - field_i16(header, kItemBoxHeight) + half - 1;
  out->navigable = (out->flags & (kItemHidden | kItemNavigationSkip)) == 0u &&
                   (out->flags & kItemEnabled) != 0u;
  if (out->item_class == ItemClass::bar) {
    float fill = 0.0F;
    std::memcpy(&fill, header + kItemFill, sizeof fill);
    if (std::isfinite(fill)) {
      out->fill = fill;
    }
  }
  for (std::size_t i = 0; i < links->size(); ++i) {
    (*links)[i] =
        field_u32(header, kItemLinks + static_cast<std::uint32_t>(i) * 4u);
  }
  *getter = field_u32(header, kItemGamevarGetter);
  /* CMenuItemListCodex overrides only the list box's parse (+0x44) and
     destructor, so its entries, window and onMouse are the list box's.
     CMenuItemListChars keeps the store; its +0xac is the first card.
     CMenuItemSkills overrides only parse and draw. */
  if (out->item_class == ItemClass::char_summary) {
    std::uint8_t mask = 0;
    if (!reader_.bytes(address + kSummaryMask, &mask, 1u)) {
      return false;
    }
    out->masks_locked = (mask & 1u) != 0u;
  }
  if (out->item_class == ItemClass::list_box ||
      out->item_class == ItemClass::list_codex ||
      out->item_class == ItemClass::list_chars ||
      out->item_class == ItemClass::skills) {
    out->list_box.emplace();
    if (!read_list_box(address, header, &*out->list_box)) {
      return false;
    }
  }
  return true;
}

/* CMenuItem::nextInDirection (0x005bccd0): follow one link kind, skipping
   items it may not land on, until an item it may, null, or the start. A loop
   falls back to the latest visited item it may land on. Direction is an index
   into the link fields: 0 left, 1 right, 2 up, 3 down. */
int RetailMenuModel::step(const MenuSnapshot &menu, int from,
                          int direction) const {
  int visited[kStepVisits];
  int count = 0;
  int at = from;
  for (;;) {
    bool looped = count >= kStepVisits;
    for (int i = 0; i < count && !looped; ++i) {
      looped = visited[i] == at;
    }
    if (looped) {
      for (int i = count - 1; i >= 0; --i) {
        if (visited[i] >= 0 &&
            menu.items[static_cast<std::size_t>(visited[i])].navigable) {
          return visited[i];
        }
      }
      return from;
    }
    visited[count++] = at;
    const MenuItem &item = menu.items[static_cast<std::size_t>(at)];
    const int links[4] = {item.link_left, item.link_right, item.link_up,
                          item.link_down};
    at = links[direction];
    if (at == from || at < 0) {
      return at;
    }
    if (menu.items[static_cast<std::size_t>(at)].navigable) {
      return at;
    }
  }
}

/*
 * The up/down order: from the start-active row (else the first navigable item
 * in slot order), up to the head of the chain or once round a loop, then down
 * until the chain ends or returns. Our ordering rule over the game's own step;
 * it does not depend on where the focus currently is.
 */
void RetailMenuModel::order_rows(MenuSnapshot *menu) const {
  constexpr int kUp = 2;
  constexpr int kDown = 3;
  int anchor = -1;
  for (std::size_t i = 0; i < menu->items.size() && anchor < 0; ++i) {
    if (menu->items[i].start_active() && menu->items[i].navigable) {
      anchor = static_cast<int>(i);
    }
  }
  for (std::size_t i = 0; i < menu->items.size() && anchor < 0; ++i) {
    if (menu->items[i].navigable) {
      anchor = static_cast<int>(i);
    }
  }
  if (anchor < 0) {
    return;
  }
  std::vector<bool> seen(menu->items.size(), false);
  int head = anchor;
  seen[static_cast<std::size_t>(anchor)] = true;
  for (int at = step(*menu, anchor, kUp);
       at >= 0 && !seen[static_cast<std::size_t>(at)];
       at = step(*menu, at, kUp)) {
    seen[static_cast<std::size_t>(at)] = true;
    head = at;
  }
  if (step(*menu, head, kUp) == anchor && head != anchor) {
    head = anchor;
  }
  std::fill(seen.begin(), seen.end(), false);
  for (int at = head; at >= 0 && !seen[static_cast<std::size_t>(at)];
       at = step(*menu, at, kDown)) {
    seen[static_cast<std::size_t>(at)] = true;
    menu->rows.push_back(at);
  }
}

/* A row whose command names a registered game variable is paired with the item
   whose gamevar getter is that variable's. */
bool RetailMenuModel::pair_values(MenuSnapshot *menu,
                                  const std::vector<std::uint32_t> &getters) {
  for (const int row : menu->rows) {
    MenuItem &item = menu->items[static_cast<std::size_t>(row)];
    const std::string &command = !item.use_command.empty() ? item.use_command
                                 : !item.left_command.empty()
                                     ? item.left_command
                                     : item.right_command;
    const std::string target = command_target(command);
    if (target.empty()) {
      continue;
    }
    std::uint32_t getter = 0;
    if (!registry_getter(target, &getter)) {
      return false;
    }
    if (getter == 0u) {
      continue;
    }
    for (std::size_t i = 0; i < getters.size(); ++i) {
      if (getters[i] == getter && static_cast<int>(i) != row) {
        item.value_item = static_cast<int>(i);
        break;
      }
    }
  }
  return true;
}

/* The hero cards' names, levels and states, from the character table. */
bool RetailMenuModel::attach_heroes(MenuSnapshot *menu) {
  const bool cards = std::any_of(
      menu->items.begin(), menu->items.end(), [](const MenuItem &item) {
        return item.item_class == ItemClass::char_summary ||
               item.item_class == ItemClass::list_chars;
      });
  if (!cards) {
    return true;
  }
  std::vector<native::HeroRecord> table;
  if (!native::RetailHeroTable(reader_).read(&table)) {
    return false;
  }
  for (MenuItem &item : menu->items) {
    if (item.item_class == ItemClass::char_summary) {
      const native::HeroRecord *hero = native::find_hero(table, item.label);
      if (hero != nullptr) {
        item.hero = *hero;
      }
    }
    if (item.item_class == ItemClass::list_chars && item.list_box) {
      for (const std::string &entry : item.list_box->entries) {
        const native::HeroRecord *hero = native::find_hero(table, entry);
        item.heroes.push_back(hero != nullptr
                                  ? std::optional<native::HeroRecord>(*hero)
                                  : std::nullopt);
      }
    }
  }
  return true;
}

bool RetailMenuModel::read_popup(bool *up) {
  *up = false;
  std::uint32_t popup = 0;
  if (!reader_.u32(reader_.image(kPopupRva), &popup)) {
    return false;
  }
  if (popup == 0u) {
    return true;
  }
  std::uint32_t current = 0;
  if (!reader_.u32(popup + kPopupCurrent, &current)) {
    return false;
  }
  const auto index = static_cast<std::int32_t>(current);
  const std::uint32_t slot = index >= 0 && index < kPopupCount
                                 ? static_cast<std::uint32_t>(index)
                                 : 0u;
  std::uint8_t shown = 0;
  if (!reader_.bytes(popup + slot * kPopupStride + kPopupShown, &shown, 1u)) {
    return false;
  }
  *up = (shown & 1u) != 0u;
  return true;
}

ReadStatus RetailMenuModel::read(MenuSnapshot *out) {
  *out = MenuSnapshot{};
  reader_.reset_failure();
  if (!read_popup(&out->popup_up)) {
    return ReadStatus::unreadable;
  }
  std::uint32_t manager = 0;
  if (!reader_.u32(reader_.image(kManagerRva), &manager)) {
    return ReadStatus::unreadable;
  }
  if (manager == 0u) {
    return ReadStatus::no_manager;
  }
  std::uint32_t menu = 0;
  if (!reader_.u32(manager + kActiveMenu, &menu)) {
    return ReadStatus::unreadable;
  }
  if (menu == 0u) {
    return ReadStatus::no_menu;
  }
  return read_menu(menu, out);
}

ReadStatus RetailMenuModel::read_menu(std::uint32_t menu, MenuSnapshot *out) {
  out->address = menu;
  std::uint32_t vtable = 0;
  std::uint32_t count = 0;
  std::uint32_t focus = 0;
  std::uint32_t live[6] = {};
  if (!reader_.u32(menu, &vtable) ||
      !reader_.c_string(menu + kMenuName, kMenuNameBytes, &out->name) ||
      !reader_.u32(menu + kMenuItemCount, &count) ||
      !reader_.u32(menu + kMenuFocus, &focus) ||
      !reader_.bytes(menu + kMenuLiveBits, live, sizeof live)) {
    return ReadStatus::unreadable;
  }
  if (count > kMenuSlots) {
    reader_.fail(menu + kMenuItemCount);
    return ReadStatus::unreadable;
  }
  out->menu_class = classify_menu(vtable, reader_.image_base());
  for (const MenuModeEntry &entry : kMenuModes) {
    if (entry.menu_class != out->menu_class) {
      continue;
    }
    std::uint32_t mode = 0;
    if (!reader_.u32(menu + entry.offset, &mode)) {
      return ReadStatus::unreadable;
    }
    out->mode = mode;
  }
  for (std::size_t i = 0; i < out->desctext.size(); ++i) {
    std::uint32_t handle = 0;
    if (!reader_.u32(menu + kMenuDesctext + static_cast<std::uint32_t>(i) * 4u,
                     &handle) ||
        !reader_.pool2_string(handle, &out->desctext[i])) {
      return ReadStatus::unreadable;
    }
  }
  std::vector<std::array<std::uint32_t, 4>> links;
  std::vector<std::uint32_t> getters;
  std::unordered_map<std::uint32_t, int> index_of;
  for (unsigned slot = 0; slot < kMenuSlots; ++slot) {
    if ((live[slot >> 5] & (1u << (slot & 31u))) == 0u) {
      continue;
    }
    std::uint32_t address = 0;
    if (!reader_.u32(menu + kMenuItemArray + slot * 4u, &address)) {
      return ReadStatus::unreadable;
    }
    if (address == 0u) {
      continue;
    }
    MenuItem item;
    std::array<std::uint32_t, 4> item_links{};
    std::uint32_t getter = 0;
    if (!read_item(address, slot, &item, &item_links, &getter)) {
      return ReadStatus::unreadable;
    }
    item.focused = address == focus;
    index_of[address] = static_cast<int>(out->items.size());
    if (item.focused) {
      out->focused = static_cast<int>(out->items.size());
    }
    out->items.push_back(std::move(item));
    links.push_back(item_links);
    getters.push_back(getter);
  }
  for (std::size_t i = 0; i < out->items.size(); ++i) {
    int resolved[4];
    for (std::size_t k = 0; k < 4u; ++k) {
      const auto found = index_of.find(links[i][k]);
      resolved[k] = found == index_of.end() ? -1 : found->second;
    }
    out->items[i].link_left = resolved[0];
    out->items[i].link_right = resolved[1];
    out->items[i].link_up = resolved[2];
    out->items[i].link_down = resolved[3];
  }
  order_rows(out);
  if (!pair_values(out, getters)) {
    return ReadStatus::unreadable;
  }
  if (!attach_heroes(out)) {
    return ReadStatus::unreadable;
  }
  return ReadStatus::ok;
}

} // namespace x2::menu
