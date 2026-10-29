#include "sebas_gui.hpp"

#include <array>
#include <cstddef>
#include <cstring>
#include <utility>

namespace x2::native::sebas {
namespace {

inline constexpr std::uint32_t kLinkedBase = 0x00400000u;
/* A window holds a few dozen widgets; more is not this GUI. */
inline constexpr std::uint32_t kMaxWidgets = 256u;
inline constexpr std::size_t kMaxText = 64u;

constexpr std::array<std::pair<std::uint32_t, Kind>, 11> kVtables{{
    {0x006a55e4u, Kind::image},
    {0x006a5634u, Kind::label},
    {0x006a548cu, Kind::button},
    {0x006a5684u, Kind::tab},
    {0x006a5724u, Kind::text_button},
    {0x006a54dcu, Kind::slider},
    {0x006a557cu, Kind::cycle},
    {0x006a57ecu, Kind::table},
    {0x006a552cu, Kind::scroll_bar},
    {0x006a56d4u, Kind::panel},
    {0x006a5774u, Kind::value},
}};

bool read_i32(const GuestMemoryView &memory, std::uint32_t address, int *out) {
  std::uint32_t bits = 0u;
  if (!memory.read_u32(address, &bits)) {
    return false;
  }
  *out = static_cast<int>(bits);
  return true;
}

bool read_rect(const GuestMemoryView &memory, std::uint32_t address,
               Rect *out) {
  return read_i32(memory, address, &out->left) &&
         read_i32(memory, address + 4u, &out->top) &&
         read_i32(memory, address + 8u, &out->right) &&
         read_i32(memory, address + 12u, &out->bottom);
}

std::string read_text(const GuestMemoryView &memory, std::uint32_t address) {
  std::string text;
  char c = 0;
  while (
      address != 0u && text.size() < kMaxText &&
      memory.read(address + static_cast<std::uint32_t>(text.size()), &c, 1u) &&
      c != 0) {
    text.push_back(c);
  }
  return text;
}

std::optional<Widget> read_widget(const GuestMemoryView &memory,
                                  std::uint32_t address,
                                  std::uint32_t image_base) {
  Widget widget;
  widget.address = address;
  std::uint32_t vtable = 0u;
  std::uint32_t text = 0u;
  std::uint8_t shown = 0u;
  Rect layout;
  if (!memory.read_u32(address, &vtable) ||
      !read_rect(memory, address + kClient, &widget.client) ||
      !read_rect(memory, address + kLayout, &layout) ||
      !memory.read(address + kShown, &shown, sizeof shown) ||
      !memory.read_u32(address + kText, &text) ||
      !read_i32(memory, address + kId, &widget.id) ||
      !read_i32(memory, address + kAnimation, &widget.animation)) {
    return std::nullopt;
  }
  widget.kind = kind_of(vtable - image_base + kLinkedBase);
  widget.layout = {layout.left, layout.top, layout.left + layout.right,
                   layout.top + layout.bottom};
  widget.shown = shown != 0u;
  widget.text = read_text(memory, text);
  return widget;
}

} // namespace

const char *kind_name(Kind kind) {
  switch (kind) {
  case Kind::image:
    return "image";
  case Kind::label:
    return "label";
  case Kind::button:
    return "button";
  case Kind::tab:
    return "tab";
  case Kind::text_button:
    return "text_button";
  case Kind::slider:
    return "slider";
  case Kind::cycle:
    return "cycle";
  case Kind::table:
    return "table";
  case Kind::scroll_bar:
    return "scroll_bar";
  case Kind::panel:
    return "panel";
  case Kind::value:
    return "value";
  case Kind::other:
    return "other";
  }
  return "other";
}

Kind kind_of(std::uint32_t linked_vtable) {
  for (const auto &[vtable, kind] : kVtables) {
    if (vtable == linked_vtable) {
      return kind;
    }
  }
  return Kind::other;
}

namespace {

std::optional<Table> read_table(const GuestMemoryView &memory,
                                const Window &window) {
  for (const Widget &widget : window.widgets) {
    if (widget.kind != Kind::table) {
      continue;
    }
    Table table;
    std::uint32_t bar = 0u;
    std::uint32_t scroll = 0u;
    if (!read_i32(memory, widget.address + kTablePlayer, &table.player) ||
        !read_i32(memory, widget.address + kTableCaptureRow,
                  &table.capture_row) ||
        !read_i32(memory, widget.address + kTableCaptureColumn,
                  &table.capture_column) ||
        !memory.read_u32(widget.address + kTableScrollBar, &bar) ||
        (bar != 0u && !memory.read_u32(bar + kScrollPosition, &scroll))) {
      return std::nullopt;
    }
    std::memcpy(&table.scroll, &scroll, sizeof scroll);
    return table;
  }
  return std::nullopt;
}

} // namespace

std::optional<Window> read_window(const GuestMemoryView &memory,
                                  std::uint32_t address,
                                  std::uint32_t image_base) {
  Window window;
  window.address = address;
  std::uint32_t widgets = 0u;
  int count = 0;
  std::uint8_t animating = 0u;
  if (!read_i32(memory, address + kState, &window.state) ||
      !memory.read(address + kAnimating, &animating, sizeof animating) ||
      !memory.read_u32(address + kWidgets, &widgets) ||
      !read_i32(memory, address + kWidgetCount, &count) || count < 0 ||
      static_cast<std::uint32_t>(count) > kMaxWidgets) {
    return std::nullopt;
  }
  window.animating = animating != 0u;
  for (std::uint32_t i = 0u; i < static_cast<std::uint32_t>(count); ++i) {
    std::uint32_t widget_address = 0u;
    if (!memory.read_u32(widgets + 4u * i, &widget_address)) {
      return std::nullopt;
    }
    std::optional<Widget> widget =
        read_widget(memory, widget_address, image_base);
    if (!widget) {
      return std::nullopt;
    }
    window.widgets.push_back(*widget);
  }
  return window;
}

std::optional<Gui> read_gui(const GuestMemoryView &memory,
                            std::uint32_t image_base) {
  std::uint32_t main = 0u;
  if (!memory.read_u32(image_base + kWindowRva, &main) || main == 0u) {
    return std::nullopt;
  }
  std::optional<Window> window = read_window(memory, main, image_base);
  if (!window) {
    return std::nullopt;
  }
  Gui gui{*window, {}, read_table(memory, *window)};
  for (const std::uint32_t rva : kDialogRvas) {
    std::uint32_t dialog = 0u;
    if (!memory.read_u32(image_base + rva, &dialog)) {
      return std::nullopt;
    }
    if (dialog == 0u) {
      continue;
    }
    std::optional<Window> shown = read_window(memory, dialog, image_base);
    if (!shown) {
      return std::nullopt;
    }
    gui.dialogs.push_back(*shown);
  }
  return gui;
}

} // namespace x2::native::sebas
