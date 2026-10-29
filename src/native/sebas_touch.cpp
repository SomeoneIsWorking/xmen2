/*
 * Advanced Options (CMenuSebas) on touch (docs/touch-play.md,
 * docs/RE/menus.md): the widget GUI FUN_0061dc10 builds is re-laid for a
 * finger through each widget's own layout setter, a press near the
 * Resolution slider is delivered on its line, and the key binding table is
 * read-only: a finger drags it through its own wheel handler and never puts
 * a cell into capture, which only a key can finish.
 */
#include "../input/menu_touch_gaps.hpp"
#include "../input/sebas_touch_layout.hpp"
#include "../input/touch_runtime.h"
#include "guest_body.h"
#include "guest_call.hpp"
#include "guest_modules.h"
#include "sebas_gui.hpp"
#include "touch_menu_source.hpp"
#include "x86rt.h"
#include "x86rt_native.h"

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string_view>
#include <utility>
#include <vector>

namespace {

namespace sebas = x2::native::sebas;

/* The builders: the window and its four dialogs. */
constexpr std::uint32_t kBuild = 0x0061dc10u;
constexpr std::uint32_t kBuildCancel = 0x0061c740u;
constexpr std::uint32_t kBuildRestart = 0x0061cbf0u;
constexpr std::uint32_t kBuildNoBinding = 0x0061d0a0u;
constexpr std::uint32_t kBuildRevert = 0x0061d760u;
/* FUN_00621ea0, thiscall(window, message, wparam, lparam), ret 0xc. */
constexpr std::uint32_t kDispatch = 0x00621ea0u;
/* FUN_0061fe20, thiscall(widget, x, y, width, height): the layout setter. */
constexpr std::uint32_t kSetLayout = 0x0061fe20u;
/* A widget's text setter (FUN_0061fd50 copies it) and pointer handler. */
constexpr std::uint32_t kSlotText = 0x2cu;
constexpr std::uint32_t kSlotMouse = 0x40u;

constexpr std::uint32_t kPointerMove = 0x200u;
constexpr std::uint32_t kButtonDown = 0x201u;
constexpr std::uint32_t kButtonUp = 0x202u;
constexpr std::uint32_t kWheel = 0x20au;
/* WM_MOUSEWHEEL's wparam for one notch up and one down. */
constexpr std::uint32_t kNotchUp = 0x00780000u;
constexpr std::uint32_t kNotchDown = 0xff880000u;
/* The binding table's rows, in layout units (16 of 480). */
constexpr int kTableRow = 16;
constexpr int kLayoutHeight = 480;
/* A dialog's texs\selected.png bar and its Back's text. */
constexpr int kHighlightHeight = 18;
constexpr std::string_view kBackToken = "$MENU_BACK";

bool on_touch() { return x2::input::touch_runtime_active() != 0; }

int client_height() {
  const auto plane = x2::native::live_scene_plane();
  return plane ? static_cast<int>(plane->client_height()) : 0;
}

int finger_pixels() {
  const auto plane = x2::native::live_scene_plane();
  return plane ? static_cast<int>(x2::input::menu_finger_target(*plane)) : 0;
}

bool inside(const sebas::Rect &rect, int x, int y) {
  return x >= rect.left && x <= rect.right && y >= rect.top && y <= rect.bottom;
}

const sebas::Widget *widget_of(const sebas::Window &window, sebas::Kind kind) {
  for (const sebas::Widget &widget : window.widgets) {
    if (widget.kind == kind && widget.shown) {
      return &widget;
    }
  }
  return nullptr;
}

class SebasTouch {
public:
  /* Puts the widgets of the window just built at `slot` in their touch
     places. */
  static void lay_out(const CPU &cpu, std::uint32_t slot) {
    const x2::native::LiveGuestMemory memory;
    const std::uint32_t base = x2::native::guest_exe_base();
    std::uint32_t address = 0u;
    if (!memory.read_u32(base + slot, &address) || address == 0u) {
      return;
    }
    const auto window = sebas::read_window(memory, address, base);
    if (!window) {
      return;
    }
    x2::guest::GuestCall call(cpu);
    if (slot == sebas::kWindowRva) {
      lay_out_main(call, *window);
    } else {
      lay_out_dialog(call, *window);
    }
  }

  /* A pointer message on touch for the main window: true when the
     finger's read of it consumed it; `lparam` may be moved onto the
     slider's line. */
  bool pointer(const CPU &cpu, std::uint32_t window, std::uint32_t message,
               std::uint32_t *lparam) {
    const int x = static_cast<std::int16_t>(*lparam & 0xffffu);
    const int y = static_cast<std::int16_t>(*lparam >> 16);
    if (drag_) {
      if (message == kButtonUp) {
        drag_.reset();
      } else if (message == kPointerMove) {
        scroll(cpu, y);
      }
      return true;
    }
    const x2::native::LiveGuestMemory memory;
    const auto gui = sebas::read_gui(memory, x2::native::guest_exe_base());
    if (!gui || gui->main.address != window) {
      return false;
    }
    if (const sebas::Widget *table = widget_of(gui->main, sebas::Kind::table);
        message == kButtonDown && table && inside(table->client, x, y)) {
      drag_ = Drag{table->address, y, 0};
      return true;
    }
    /* Hovering takes the window's capture, so the finger's moves are
       delivered on the slider's line too. */
    if (const sebas::Widget *slider = widget_of(gui->main, sebas::Kind::slider);
        slider && x >= slider->client.left && x <= slider->client.right) {
      if (const auto line = x2::input::sebas_slider_y(
              slider->client.top, slider->client.bottom, y, finger_pixels())) {
        *lparam =
            (static_cast<std::uint32_t>(*line) << 16) | (*lparam & 0xffffu);
      }
    }
    return false;
  }

private:
  static x2::input::SebasBox box_of(const sebas::Widget &widget) {
    return {widget.layout.left, widget.layout.top,
            widget.layout.right - widget.layout.left,
            widget.layout.bottom - widget.layout.top};
  }

  static void place(x2::guest::GuestCall &call, const sebas::Widget &widget,
                    const x2::input::SebasBox &box, std::string_view text) {
    call.thiscall(x2::native::exe_linked(kSetLayout), widget.address,
                  {static_cast<std::uint32_t>(box.x),
                   static_cast<std::uint32_t>(box.y),
                   static_cast<std::uint32_t>(box.width),
                   static_cast<std::uint32_t>(box.height)});
    if (!text.empty()) {
      const x2::guest::GuestText shown(text);
      call.virtual_call(widget.address, kSlotText, {shown.address()});
    }
  }

  static void lay_out_main(x2::guest::GuestCall &call,
                           const sebas::Window &window) {
    for (const sebas::Widget &widget : window.widgets) {
      if (const auto touch = x2::input::sebas_touch_place(box_of(widget))) {
        place(call, widget, touch->touch, touch->text);
      }
    }
  }

  static x2::input::SebasDialogRole role_of(const sebas::Widget &widget) {
    using x2::input::SebasDialogRole;
    if (widget.kind == sebas::Kind::value) {
      return SebasDialogRole::row;
    }
    if (widget.kind == sebas::Kind::image &&
        widget.layout.bottom - widget.layout.top == kHighlightHeight) {
      return SebasDialogRole::highlight;
    }
    if (widget.kind == sebas::Kind::text_button &&
        widget.text.starts_with(kBackToken)) {
      return SebasDialogRole::back;
    }
    return SebasDialogRole::other;
  }

  static void lay_out_dialog(x2::guest::GuestCall &call,
                             const sebas::Window &window) {
    std::vector<std::pair<x2::input::SebasDialogRole, x2::input::SebasBox>>
        widgets;
    for (const sebas::Widget &widget : window.widgets) {
      widgets.emplace_back(role_of(widget), box_of(widget));
    }
    const std::vector<x2::input::SebasBox> boxes =
        x2::input::sebas_dialog_boxes(widgets);
    std::vector<x2::input::SebasBox> rows;
    const sebas::Widget *highlight = nullptr;
    for (std::size_t i = 0; i < boxes.size(); ++i) {
      const x2::input::SebasDialogRole role = widgets[i].first;
      if (role == x2::input::SebasDialogRole::row) {
        rows.push_back(boxes[i]);
      } else if (role == x2::input::SebasDialogRole::highlight) {
        highlight = &window.widgets[i];
      } else if (role == x2::input::SebasDialogRole::other) {
        continue;
      }
      place(call, window.widgets[i], boxes[i],
            role == x2::input::SebasDialogRole::back ? x2::input::kSebasBackText
                                                     : std::string_view{});
    }
    if (highlight) {
      aim_highlight(*highlight, rows);
    }
  }

  /* The highlight's step toward the row slot i answers is aimed at that
     row's touch place. */
  static void aim_highlight(const sebas::Widget &highlight,
                            const std::vector<x2::input::SebasBox> &rows) {
    for (std::size_t slot = 0; slot < rows.size(); ++slot) {
      const std::uint32_t record =
          highlight.address + sebas::kAnimationRecords +
          (static_cast<std::uint32_t>(slot) * sebas::kAnimationRecordSize);
      WR32(record + sebas::kAnimationTargetX,
           static_cast<std::uint32_t>(rows[slot].x));
      WR32(record + sebas::kAnimationTargetY,
           static_cast<std::uint32_t>(rows[slot].y));
    }
  }

  struct Drag {
    std::uint32_t table = 0u;
    int from = 0;
    int sent = 0;
  };

  void scroll(const CPU &cpu, int y) {
    const int row = kTableRow * client_height() / kLayoutHeight;
    const int notches = x2::input::sebas_table_notches(y - drag_->from, row);
    x2::guest::GuestCall call(cpu);
    while (drag_->sent != notches) {
      const bool up = notches > drag_->sent;
      call.virtual_call(drag_->table, kSlotMouse,
                        {kWheel, up ? kNotchUp : kNotchDown, 0u});
      drag_->sent += up ? 1 : -1;
    }
  }

  std::optional<Drag> drag_;
};

SebasTouch g_sebas_touch;

void build_and_lay_out(CPU *cpu, std::uint32_t builder, std::uint32_t slot) {
  x86_guest_body(cpu, "XMen2.exe", builder);
  if (on_touch()) {
    SebasTouch::lay_out(*cpu, slot);
  }
}

} // namespace

extern "C" void x2_override_0061dc10(CPU *cpu) {
  build_and_lay_out(cpu, kBuild, sebas::kWindowRva);
}

extern "C" void x2_override_0061c740(CPU *cpu) {
  build_and_lay_out(cpu, kBuildCancel, sebas::kDialogRvas[0]);
}

extern "C" void x2_override_0061cbf0(CPU *cpu) {
  build_and_lay_out(cpu, kBuildRestart, sebas::kDialogRvas[1]);
}

extern "C" void x2_override_0061d0a0(CPU *cpu) {
  build_and_lay_out(cpu, kBuildNoBinding, sebas::kDialogRvas[2]);
}

extern "C" void x2_override_0061d760(CPU *cpu) {
  build_and_lay_out(cpu, kBuildRevert, sebas::kDialogRvas[3]);
}

extern "C" void x2_override_00621ea0(CPU *cpu) {
  const std::uint32_t esp = cpu->reg[kX86pEsp];
  const std::uint32_t message = RD32(esp + 4u);
  if (on_touch() && message >= kPointerMove && message <= kButtonUp) {
    std::uint32_t lparam = RD32(esp + 12u);
    if (g_sebas_touch.pointer(*cpu, cpu->reg[kX86pEcx], message, &lparam)) {
      cpu->reg[kX86pEsp] += 4u + 12u; /* RET 0xc */
      return;
    }
    WR32(esp + 12u, lparam);
  }
  x86_guest_body(cpu, "XMen2.exe", kDispatch);
}

__attribute__((constructor)) static void x2_sebas_touch_register(void) {
  x86_register_override("XMen2.exe", kBuild, x2_override_0061dc10);
  x86_register_override("XMen2.exe", kBuildCancel, x2_override_0061c740);
  x86_register_override("XMen2.exe", kBuildRestart, x2_override_0061cbf0);
  x86_register_override("XMen2.exe", kBuildNoBinding, x2_override_0061d0a0);
  x86_register_override("XMen2.exe", kBuildRevert, x2_override_0061d760);
  x86_register_override("XMen2.exe", kDispatch, x2_override_00621ea0);
}
