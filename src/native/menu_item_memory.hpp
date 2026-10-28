#ifndef X2_MENU_ITEM_MEMORY_HPP
#define X2_MENU_ITEM_MEMORY_HPP

#include <cstdint>

/* CMenuItem's and CMenuItemListBox's memory (docs/RE/menus.md). Offsets from
   the item. */
namespace x2::native::menu_item {

/* FUN_005adc10, thiscall(menu, name): the menu's item of that name, or 0. */
inline constexpr std::uint32_t kItemByNameRva = 0x001adc10u;

/* FUN_005bbd10, thiscall(item): the gamevar refresh every item draw starts
   with. */
inline constexpr std::uint32_t kRefresh = 0x005bbd10u;
/* Virtual slots: hidden (al), hide (1 hides, as CMenu FUN_005ade10 does) and
   the item's text. */
inline constexpr std::uint32_t kSlotHidden = 0x04u;
inline constexpr std::uint32_t kSlotHide = 0x08u;
inline constexpr std::uint32_t kSlotText = 0x7cu;

inline constexpr std::uint32_t kMenu = 0x04u;
inline constexpr std::uint32_t kFlags = 0x54u;
inline constexpr std::uint32_t kModel = 0x58u;
/* The hit box FUN_005bc530 sets from the model's bounds, all i16: the world
   left and top, the width and height, the local left and top, and the world
   depth. World = local + the item's translation; z grows up. */
inline constexpr std::uint32_t kBoxLeft = 0x70u;
inline constexpr std::uint32_t kBoxTop = 0x72u;
inline constexpr std::uint32_t kBoxWidth = 0x74u;
inline constexpr std::uint32_t kBoxHeight = 0x76u;
inline constexpr std::uint32_t kLocalLeft = 0x78u;
inline constexpr std::uint32_t kLocalTop = 0x7au;
inline constexpr std::uint32_t kDepth = 0x7cu;
inline constexpr std::uint32_t kStyle = 0x94u;
inline constexpr std::uint32_t kTextBox = 0xa4u;

} // namespace x2::native::menu_item

/* CMenuItemListBox, vtable 0x006a062c. */
namespace x2::native::list_box {

/* A click's walk to its entry: steps left (positive is down), frames to the
   next step, and set while it walks; the list's tick 0x005c0c60 publishes
   each step as the pad's. */
inline constexpr std::uint32_t kWalkSteps = 0x88u;
inline constexpr std::uint32_t kWalkTimer = 0x8cu;
inline constexpr std::uint32_t kWalking = 0x90u;
inline constexpr std::uint32_t kSelected = 0xacu;
inline constexpr std::uint32_t kStore = 0xbcu;
/* Up to four columns of 6 bytes: i16 width, i16 alignment, i16 widest
   text. */
inline constexpr std::uint32_t kColumns = 0xc0u;
inline constexpr std::uint32_t kColumnBytes = 6u;
inline constexpr std::uint32_t kColumnAlign = 2u;
inline constexpr std::uint32_t kMaxColumns = 4u;
inline constexpr std::uint32_t kTop = 0xd8u;
inline constexpr std::uint32_t kVisibleRows = 0xdbu;
inline constexpr std::uint32_t kColumnCount = 0xdcu;
/* Rows navigation keeps between the selection and the window's edge. */
inline constexpr std::uint32_t kMargin = 0xddu;
inline constexpr std::uint32_t kRowHeight = 0xe0u;
/* The `list_focus` cursor model. */
inline constexpr std::uint32_t kCursor = 0xe4u;
/* The icon sheet's entity and its sprite batch. */
inline constexpr std::uint32_t kIconSheet = 0xf0u;
inline constexpr std::uint32_t kIconBatch = 0xf8u;
inline constexpr std::uint32_t kBits = 0x2a4u;
inline constexpr std::uint8_t kCursorAttached = 0x01u;
inline constexpr std::uint8_t kIcons = 0x10u;
inline constexpr std::uint8_t kIconsRight = 0x40u;

/* The store: one record id per entry, the count at +0x84. */
inline constexpr std::uint32_t kStoreCount = 0x84u;
inline constexpr std::uint32_t kStoreIds = 0x84u;
/* Every list box draws its entries from one table of 0x70-byte records,
   their text to +0x58; the entry getter (0x005c23c0) copies at most 0x3f
   bytes of it. */
inline constexpr std::uint32_t kRecordsRva = 0x004a83f4u;
inline constexpr std::uint32_t kRecordStride = 0x70u;
inline constexpr std::uint32_t kRecordTextBytes = 0x58u;
inline constexpr std::uint32_t kEntryBytes = 0x3fu;

} // namespace x2::native::list_box

#endif
