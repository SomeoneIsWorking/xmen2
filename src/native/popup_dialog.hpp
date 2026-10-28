#ifndef X2_POPUP_DIALOG_HPP
#define X2_POPUP_DIALOG_HPP

#include <cstdint>

/* CPopupDialog's memory (docs/RE/menus.md): the singleton, its three pages,
   its models and its text box. */
namespace x2::native::popup {

/* FUN_005eb300's singleton, an image RVA; vtable 0x006a332c. */
inline constexpr std::uint32_t kSingletonRva = 0x004b13ecu;
inline constexpr std::uint32_t kManagerRva = 0x001eb300u;
inline constexpr std::uint32_t kCurrentPage = 0x403cu;
inline constexpr std::uint32_t kPages = 0x18u;
inline constexpr std::uint32_t kPageBytes = 0x1560u;
inline constexpr std::int32_t kPageCount = 3;

/* The page CPopupDialog works on: [+0x403c], or page 0 out of 0..2. */
constexpr std::uint32_t page_offset(std::int32_t current) {
  return kPages + (current >= 0 && current < kPageCount
                       ? static_cast<std::uint32_t>(current) * kPageBytes
                       : 0u);
}

/* A page: the title at +0, entry i's text at +(i+8)*0x80, its accept script
   at +0x901 + i*0x80 and its second script at +0xe02 + i*0x80; bit i of the
   dwords at +0x1304 and +0x1308 says each script is set, and at +0x130c that
   the entry is enabled; then its counts (all char) and flags. */
inline constexpr std::uint32_t kEntryBytes = 0x80u;
inline constexpr std::uint32_t kEntries = 8u * kEntryBytes;
inline constexpr std::uint32_t kScripts = 0x901u;
inline constexpr std::uint32_t kSecondScripts = 0xe02u;
inline constexpr std::uint32_t kScriptBits = 0x1304u;
inline constexpr std::uint32_t kSecondScriptBits = 0x1308u;
inline constexpr std::uint32_t kEnabledBits = 0x130cu;
inline constexpr std::uint32_t kEntryCount = 0x1558u;
inline constexpr std::uint32_t kVisible = 0x1559u;
inline constexpr std::uint32_t kFirstShown = 0x155au;
inline constexpr std::uint32_t kSelected = 0x155bu;
inline constexpr std::uint32_t kFlags = 0x155du;
inline constexpr std::uint8_t kOpen = 0x01u;

/* Models loaded at init (FUN_005e95a0): the frame "ui/hud/dialog", the two
   "ui/models/m_review_list_arrow" and the bar "ui/hud/dialog_select". */
inline constexpr std::uint32_t kFrameModel = 0x4040u;
inline constexpr std::uint32_t kArrowUpModel = 0x4044u;
inline constexpr std::uint32_t kArrowDownModel = 0x4048u;
inline constexpr std::uint32_t kBarModel = 0x404cu;
inline constexpr std::uint32_t kTextBox = 0x4050u;
/* A short: the text width every measure of the popup's text passes. */
inline constexpr std::uint32_t kTextWidth = 0x4054u;
/* Slot 0x34 (FUN_005e99e0) focuses an option. */
inline constexpr std::uint32_t kFocusSlot = 0x34u;

/* The text box: its pen's drop below the top (float); left, top, width (the
   span onMouse hit-tests, from the popup's short at +0x4054) and a second
   int the line call passes on; colour (igVec4f); then its lines from +0x43c,
   0x214 apart, counted at +0x21844: a RECT {top, bottom, left, right} at +4,
   the text at +0x14. */
inline constexpr std::uint32_t kBoxPen = 0x404u;
inline constexpr std::uint32_t kBoxLeft = 0x408u;
inline constexpr std::uint32_t kBoxTop = 0x40cu;
inline constexpr std::uint32_t kBoxWidth = 0x410u;
inline constexpr std::uint32_t kBoxStyle = 0x414u;
inline constexpr std::uint32_t kBoxColor = 0x418u;
inline constexpr std::uint32_t kBoxLines = 0x43cu;
inline constexpr std::uint32_t kLineBytes = 0x214u;
inline constexpr std::uint32_t kLineCount = 0x21844u;
inline constexpr std::uint32_t kLineRect = 0x4u;
inline constexpr std::uint32_t kLineText = 0x14u;
inline constexpr std::uint32_t kLineTextBytes = 0x1eeu;

} // namespace x2::native::popup

#endif
