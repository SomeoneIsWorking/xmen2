#ifndef X2_SEBAS_GUI_HPP
#define X2_SEBAS_GUI_HPP

#include "guest_memory_view.hpp"

#include <array>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

/* Advanced Options' widget GUI (CMenuSebas, docs/RE/menus.md): the window
   FUN_0061dc10 builds at DAT_00a6ad34 and its widgets. Offsets from the
   linked image. */
namespace x2::native::sebas {

inline constexpr std::uint32_t kWindowRva = 0x0066ad34u;
/* The confirmation windows over it, in the order FUN_006193e0 gives one the
   input: CANCEL_WARNING (FUN_0061c740), NEW_RESZ_RESTART (FUN_0061cbf0),
   PREVENT_NO_BINDING (FUN_0061d0a0) and REVERT_DEFAULT (FUN_0061d760). */
inline constexpr std::array<std::uint32_t, 4> kDialogRvas{
    0x0066ad40u, 0x0066ad3cu, 0x0066ad44u, 0x0066ad38u};

/* The window: the widget holding the pointer, the widget array and count,
   set while any widget animates, and its close state. */
inline constexpr std::uint32_t kCaptured = 0x10u;
inline constexpr std::uint32_t kWidgets = 0x14u;
inline constexpr std::uint32_t kWidgetCount = 0x24u;
inline constexpr std::uint32_t kAnimating = 0x44u;
inline constexpr std::uint32_t kState = 0x48u;
inline constexpr int kStateAccepted = 1;
inline constexpr int kStateBack = 2;

/* A widget: client rectangle (left, top, right, bottom, inclusive), its
   layout in the window's 640x480 units (x, y, width, height), shown, id,
   the animation running (-1 none) and its text, a heap copy. */
inline constexpr std::uint32_t kClient = 0x04u;
inline constexpr std::uint32_t kLayout = 0x14u;
inline constexpr std::uint32_t kShown = 0x30u;
inline constexpr std::uint32_t kText = 0x34u;
inline constexpr std::uint32_t kId = 0x5cu;
inline constexpr std::uint32_t kAnimation = 0x70u;
/* A widget's animation records (FUN_00620090, run by FUN_00622300): 0x2c
   bytes each from +0x78, a step function first; a dialog's highlight steps
   with FUN_00617d80 toward the x, y at +0x0c, +0x10, slot 0 at Yes, 1 at No. */
inline constexpr std::uint32_t kAnimationRecords = 0x78u;
inline constexpr std::uint32_t kAnimationRecordSize = 0x2cu;
inline constexpr std::uint32_t kAnimationTargetX = 0x0cu;
inline constexpr std::uint32_t kAnimationTargetY = 0x10u;

/* The widget classes, by linked vtable. */
enum class Kind : std::uint8_t {
  image,
  label,
  button,
  tab,
  text_button,
  slider,
  cycle,
  table,
  scroll_bar,
  panel,
  value,
  other,
};
const char *kind_name(Kind kind);
Kind kind_of(std::uint32_t linked_vtable);

struct Rect {
  int left = 0;
  int top = 0;
  int right = 0;
  int bottom = 0;
};

struct Widget {
  std::uint32_t address = 0u;
  int id = 0;
  Kind kind = Kind::other;
  Rect client;
  Rect layout;
  bool shown = false;
  int animation = -1;
  std::string text;
};

struct Window {
  std::uint32_t address = 0u;
  int state = 0;
  bool animating = false;
  std::vector<Widget> widgets;
};

/* Advanced Options' window and the dialogs up over it, the one taking the
   input first. */
/* The key binding table (id 0xe): the player its tab shows, the cell a
   click put into capture waiting for a key (-1 none, FUN_006265a0) and its
   scroll bar's position, 0 at the first row. */
inline constexpr std::uint32_t kTablePlayer = 0x544u;
inline constexpr std::uint32_t kTableCaptureRow = 0x1c8u;
inline constexpr std::uint32_t kTableCaptureColumn = 0x1ccu;
inline constexpr std::uint32_t kTableScrollBar = 0x540u;
inline constexpr std::uint32_t kScrollPosition = 0x1bcu;

struct Table {
  int player = 0;
  int capture_row = -1;
  int capture_column = -1;
  float scroll = 0.0F;
};

struct Gui {
  Window main;
  std::vector<Window> dialogs;
  std::optional<Table> table;
  const Window &front() const { return dialogs.empty() ? main : dialogs[0]; }
};

std::optional<Window> read_window(const GuestMemoryView &memory,
                                  std::uint32_t address,
                                  std::uint32_t image_base);
/* The GUI while Advanced Options is up, nullopt otherwise. */
std::optional<Gui> read_gui(const GuestMemoryView &memory,
                            std::uint32_t image_base);

} // namespace x2::native::sebas

#endif
