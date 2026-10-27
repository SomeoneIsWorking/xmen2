/*
 * CPopupDialog's option rows on touch, both halves from popup_row_slot: the
 * text box draw (0x004552f0) of an option advances its pen by the option's
 * slot, and the text measure (0x00597c90) CPopupDialog::onMouse sizes an
 * option's hit box with answers that slot.
 */
#include "../input/touch_runtime.h"
#include "guest_body.h"
#include "guest_modules.h"
#include "popup_rows.hpp"
#include "touch_menu_source.hpp"
#include "x86rt.h"
#include "x86rt_native.h"

#include <cstdint>
#include <optional>

namespace {

constexpr std::uint32_t kTextBoxDraw = 0x004552f0u;
constexpr std::uint32_t kTextMeasure = 0x00597c90u;
/* After the option measure at 0x005ea3c3 in CPopupDialog::onMouse. */
constexpr std::uint32_t kOptionHitReturn = 0x005ea3c6u;
/* After the option draw at 0x005eae70 in CPopupDialog's draw. */
constexpr std::uint32_t kOptionDrawReturn = 0x005eae75u;
/* The pen's drop below the box top, a float. */
constexpr std::uint32_t kTextBoxPen = 0x404u;

std::uint32_t linked(std::uint32_t preferred) {
  return x86_module_base("XMen2.exe") + (preferred - x2::native::kExePreferred);
}

std::optional<x2::presentation::RetailScenePlane> scene_plane() {
  const x2::native::LiveGuestMemory memory;
  return x2::native::read_scene_plane(memory, x86_module_base("XMen2.exe"));
}

bool on_touch(const CPU *cpu, std::uint32_t option_return) {
  return RD32(cpu->reg[kX86pEsp]) == linked(option_return) &&
         x2::input::touch_runtime_active() != 0;
}

} // namespace

extern "C" void x2_override_004552f0(CPU *cpu) {
  const std::uint32_t box = cpu->reg[kX86pEcx];
  const bool option = on_touch(cpu, kOptionDrawReturn);
  const long double before = RDF32(box + kTextBoxPen);
  x86_guest_body(cpu, "XMen2.exe", kTextBoxDraw);
  if (!option) {
    return;
  }
  const auto plane = scene_plane();
  if (!plane) {
    return;
  }
  const long double after = RDF32(box + kTextBoxPen);
  const float line = static_cast<float>(after - before);
  WRF32(box + kTextBoxPen, before + x2::native::popup_row_slot(line, *plane));
}

extern "C" void x2_override_00597c90(CPU *cpu) {
  const bool option = on_touch(cpu, kOptionHitReturn);
  x86_guest_body(cpu, "XMen2.exe", kTextMeasure);
  if (!option) {
    return;
  }
  const auto plane = scene_plane();
  if (!plane) {
    return;
  }
  const auto line = static_cast<float>(x87_pop(cpu));
  x87_push(cpu, x2::native::popup_row_slot(line, *plane));
}

__attribute__((constructor)) static void x2_popup_row_register(void) {
  x86_register_override("XMen2.exe", kTextBoxDraw, x2_override_004552f0);
  x86_register_override("XMen2.exe", kTextMeasure, x2_override_00597c90);
}
