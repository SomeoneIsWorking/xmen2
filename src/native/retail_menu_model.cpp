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

/* handle_str<2> pool, FUN_00602200: a heap object held here. */
inline constexpr std::uint32_t kPool2PointerRva = 0x0060a81cu;
inline constexpr std::uint32_t kPool2Slots = 0x400u;
inline constexpr std::uint32_t kPool2Strings = 0x1008u;
inline constexpr std::uint32_t kPool2Capacity = 0x1c00u;
/* handle_str<0> pool, FUN_00602140: a static object. */
inline constexpr std::uint32_t kPool0Rva = 0x0060a820u;
inline constexpr std::uint32_t kPool0Slots = 0x2000u;
inline constexpr std::uint32_t kPool0Strings = 0x8008u;
inline constexpr std::uint32_t kPool0Capacity = 0x14400u;
inline constexpr std::uint32_t kHandleIndexMask = 0x00ffffffu;
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

/* CMenuTeam::onMouse (0x005e25c0) switches on menu+0x18d8. */
inline constexpr MenuModeEntry kMenuModes[] = {
    {"CMenuTeam", 0x18d8u},
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

/* The game's strings are single bytes drawn through its own font; each byte is
   carried as the code point of the same value. */
std::string latin1_to_utf8(const std::string &bytes) {
  std::string out;
  out.reserve(bytes.size());
  for (const char c : bytes) {
    const auto byte = static_cast<unsigned char>(c);
    if (byte < 0x80u) {
      out.push_back(c);
    } else {
      out.push_back(static_cast<char>(0xc0u | (byte >> 6)));
      out.push_back(static_cast<char>(0x80u | (byte & 0x3fu)));
    }
  }
  return out;
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
    : memory_(memory), image_base_(image_base) {}

bool RetailMenuModel::read_bytes(std::uint32_t address, void *out,
                                 std::size_t bytes) {
  if (memory_.read(address, out, bytes)) {
    return true;
  }
  failed_ = address;
  return false;
}

bool RetailMenuModel::read_u32(std::uint32_t address, std::uint32_t *out) {
  return read_bytes(address, out, sizeof *out);
}

/* A NUL-terminated string found within `capacity` bytes, read in chunks so the
   end of a mapping past the terminator is never touched. */
bool RetailMenuModel::read_c_string(std::uint32_t address, std::size_t capacity,
                                    std::string *out) {
  out->clear();
  constexpr std::size_t kChunk = 16u;
  char chunk[kChunk];
  std::size_t scanned = 0;
  while (scanned < capacity) {
    const auto at = static_cast<std::uint32_t>(address + scanned);
    std::size_t got = std::min(kChunk, capacity - scanned);
    if (!memory_.read(at, chunk, got)) {
      got = 1u;
      if (!read_bytes(at, chunk, got)) {
        return false;
      }
    }
    for (std::size_t i = 0; i < got; ++i) {
      if (chunk[i] == '\0') {
        return true;
      }
      out->push_back(chunk[i]);
    }
    scanned += got;
  }
  failed_ = address;
  return false;
}

/* handle_str<2>: pool + 0x1008 + pool[1 + (handle & 0xffffff)], the lookup
   every CMenuItem accessor inlines; the high byte is not checked by the game
   either. */
bool RetailMenuModel::pool_string(std::uint32_t handle, std::string *out) {
  out->clear();
  if (handle == 0u) {
    return true;
  }
  std::uint32_t pool = 0;
  if (!read_u32(image_base_ + kPool2PointerRva, &pool)) {
    return false;
  }
  const std::uint32_t index = handle & kHandleIndexMask;
  std::uint32_t offset = 0;
  if (pool == 0u || index >= kPool2Slots ||
      !read_u32(pool + 4u + index * 4u, &offset) || offset >= kPool2Capacity) {
    failed_ = pool + 4u + index * 4u;
    return false;
  }
  std::string bytes;
  if (!read_c_string(pool + kPool2Strings + offset, kPool2Capacity - offset,
                     &bytes)) {
    return false;
  }
  *out = latin1_to_utf8(bytes);
  return true;
}

bool RetailMenuModel::registry_getter(const std::string &name,
                                      std::uint32_t *getter) {
  *getter = 0u;
  const std::uint32_t registry = image_base_ + kRegistryRva;
  const std::uint32_t pool = image_base_ + kPool0Rva;
  for (std::uint32_t node = 0; node < kRegistryNodes; ++node) {
    std::uint32_t key = 0;
    if (!read_u32(registry + kRegistryNodeKeys + node * kRegistryNodeStride,
                  &key)) {
      return false;
    }
    const std::uint32_t index = key & kHandleIndexMask;
    if (key == 0u || index >= kPool0Slots) {
      continue;
    }
    std::uint32_t offset = 0;
    if (!read_u32(pool + 4u + index * 4u, &offset)) {
      return false;
    }
    if (offset >= kPool0Capacity) {
      continue;
    }
    std::string key_name;
    if (!read_c_string(pool + kPool0Strings + offset, kPool0Capacity - offset,
                       &key_name)) {
      return false;
    }
    if (key_name == name) {
      return read_u32(registry + kRegistryGetters + node * 8u, getter);
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
    if (text != 0u && !read_c_string(text, kTextBoxBytes, &bytes)) {
      return false;
    }
    *out = latin1_to_utf8(bytes);
    return true;
  }
  if (!uses_text_get_text(item_class)) {
    return true;
  }
  const std::uint32_t dynamic = field_u32(header, kTextDynamicSlot);
  if (dynamic == 0u) {
    return pool_string(field_u32(header, kTextHandle), out);
  }
  if (dynamic >= kDynamicTextSlots) {
    failed_ = image_base_ + kDynamicTextLiveRva;
    return false;
  }
  std::uint32_t live = 0;
  if (!read_u32(image_base_ + kDynamicTextLiveRva, &live)) {
    return false;
  }
  /* A released dynamic slot reads as "", as getText returns it. */
  if ((live & (1u << dynamic)) != 0u &&
      !read_c_string(image_base_ + kDynamicTextRva +
                         dynamic * kDynamicTextBytes,
                     kDynamicTextBytes, &bytes)) {
    return false;
  }
  *out = latin1_to_utf8(bytes);
  return true;
}

bool RetailMenuModel::read_item(std::uint32_t address, unsigned slot,
                                MenuItem *out,
                                std::array<std::uint32_t, 4> *links,
                                std::uint32_t *getter) {
  std::uint8_t header[kItemHeaderBytes];
  if (!read_bytes(address, header, sizeof header)) {
    return false;
  }
  out->address = address;
  out->slot = slot;
  out->item_class = classify_item(field_u32(header, 0u), image_base_);
  out->flags = header[kItemFlags];
  if (!pool_string(field_u32(header, kItemName), &out->name) ||
      !pool_string(field_u32(header, kItemUse), &out->use_command) ||
      !pool_string(field_u32(header, kItemLeftCommand), &out->left_command) ||
      !pool_string(field_u32(header, kItemRightCommand), &out->right_command) ||
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

bool RetailMenuModel::read_popup(bool *up) {
  *up = false;
  std::uint32_t popup = 0;
  if (!read_u32(image_base_ + kPopupRva, &popup)) {
    return false;
  }
  if (popup == 0u) {
    return true;
  }
  std::uint32_t current = 0;
  if (!read_u32(popup + kPopupCurrent, &current)) {
    return false;
  }
  const auto index = static_cast<std::int32_t>(current);
  const std::uint32_t slot = index >= 0 && index < kPopupCount
                                 ? static_cast<std::uint32_t>(index)
                                 : 0u;
  std::uint8_t shown = 0;
  if (!read_bytes(popup + slot * kPopupStride + kPopupShown, &shown, 1u)) {
    return false;
  }
  *up = (shown & 1u) != 0u;
  return true;
}

ReadStatus RetailMenuModel::read(MenuSnapshot *out) {
  *out = MenuSnapshot{};
  failed_ = 0u;
  if (!read_popup(&out->popup_up)) {
    return ReadStatus::unreadable;
  }
  std::uint32_t manager = 0;
  if (!read_u32(image_base_ + kManagerRva, &manager)) {
    return ReadStatus::unreadable;
  }
  if (manager == 0u) {
    return ReadStatus::no_manager;
  }
  std::uint32_t menu = 0;
  if (!read_u32(manager + kActiveMenu, &menu)) {
    return ReadStatus::unreadable;
  }
  if (menu == 0u) {
    return ReadStatus::no_menu;
  }
  out->address = menu;
  std::uint32_t vtable = 0;
  std::uint32_t count = 0;
  std::uint32_t focus = 0;
  std::uint32_t live[6] = {};
  if (!read_u32(menu, &vtable) ||
      !read_c_string(menu + kMenuName, kMenuNameBytes, &out->name) ||
      !read_u32(menu + kMenuItemCount, &count) ||
      !read_u32(menu + kMenuFocus, &focus) ||
      !read_bytes(menu + kMenuLiveBits, live, sizeof live)) {
    return ReadStatus::unreadable;
  }
  if (count > kMenuSlots) {
    failed_ = menu + kMenuItemCount;
    return ReadStatus::unreadable;
  }
  out->menu_class = classify_menu(vtable, image_base_);
  for (const MenuModeEntry &entry : kMenuModes) {
    if (entry.menu_class != out->menu_class) {
      continue;
    }
    std::uint32_t mode = 0;
    if (!read_u32(menu + entry.offset, &mode)) {
      return ReadStatus::unreadable;
    }
    out->mode = mode;
  }
  for (std::size_t i = 0; i < out->desctext.size(); ++i) {
    std::uint32_t handle = 0;
    if (!read_u32(menu + kMenuDesctext + static_cast<std::uint32_t>(i) * 4u,
                  &handle) ||
        !pool_string(handle, &out->desctext[i])) {
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
    if (!read_u32(menu + kMenuItemArray + slot * 4u, &address)) {
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
  return ReadStatus::ok;
}

} // namespace x2::menu
