/*
 * CPopupDialog on touch (docs/touch-play.md): its frame grown to most of the
 * screen, its entries as cards of the game's selection bar art with the
 * game's font over them, its footer prompts as bar buttons, and a finger
 * that taps a card or button to act and drags the cards to scroll. The
 * layout is layout_popup_touch; this file draws it with the popup's own
 * models and text box and takes the popup's pointer handler.
 */
#include "../input/menu_touch_gaps.hpp"
#include "../input/popup_touch_layout.hpp"
#include "../input/touch_runtime.h"
#include "guest_body.h"
#include "guest_call.hpp"
#include "guest_memory_view.hpp"
#include "guest_modules.h"
#include "load_game_menu_runtime.h"
#include "menu_action.hpp"
#include "menu_scene.hpp"
#include "popup_dialog.hpp"
#include "popup_touch_drawn.hpp"
#include "touch_menu_source.hpp"
#include "x86rt.h"
#include "x86rt_native.h"

#include <array>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <optional>
#include <string>
#include <string_view>

namespace {

namespace popup = x2::native::popup;
using x2::input::SceneBox;
using x2::native::menu_scene::Vectors;
using x2::presentation::ScenePoint;
namespace menu_scene = x2::native::menu_scene;

constexpr std::uint32_t kPopupDraw = 0x005ea9e0u;
/* CPopupDialog::onMouse(x, z, _, message, _), ret 0x14. */
constexpr std::uint32_t kPopupOnMouse = 0x005ea2c0u;
/* The popup attaching its models to the menu scene or the world scene, and
   detaching them; +0x4056 bit 0 says they are attached. */
constexpr std::uint32_t kAttachMenu = 0x005e91c0u;
constexpr std::uint32_t kAttachWorld = 0x005e90d0u;
constexpr std::uint32_t kDetachMenu = 0x005e93b0u;
constexpr std::uint32_t kDetachWorld = 0x005e92d0u;
constexpr std::uint32_t kAttached = 0x4056u;
/* CMenuMgr, and the world scene with its attach and detach slots. */
constexpr std::uint32_t kMenuManager = 0x005d8920u;
constexpr std::uint32_t kWorldScene = 0x0059ee20u;
constexpr std::uint32_t kWorldAttach = 0x24u;
constexpr std::uint32_t kWorldDetach = 0x28u;
/* FUN_005e9510(name, &entity): one entity of a model's art. */
constexpr std::uint32_t kLoadModel = 0x005e9510u;
constexpr std::uint32_t kBarArtName = 0x006a32d4u;
/* The text line call (lines, x, z, width, style, scale, flags, colour,
   format, ...) and its "%s". */
constexpr std::uint32_t kTextLine = 0x005ef270u;
constexpr std::uint32_t kLineFormat = 0x00685ca4u;
/* The font manager's text height (width, text, scale, 0, 1) and "A". */
constexpr std::uint32_t kFonts = 0x00597c20u;
constexpr std::uint32_t kTextHeight = 0x38u;
constexpr std::uint32_t kLineProbe = 0x0069a444u;
/* STYLE_MENU: the draw's lazily looked-up handle, its ready bit, and the
   lookup, a name hash (FUN_00425df0) passed to CMenuMgr vt+0x228. */
constexpr std::uint32_t kMenuStyle = 0x008b1400u;
constexpr std::uint32_t kStyleReady = 0x008b1408u;
constexpr std::uint32_t kMenuStyleReady = 0x2u;
constexpr std::uint32_t kStyleName = 0x0069e83cu;
constexpr std::uint32_t kNameHash = 0x00425df0u;
constexpr std::uint32_t kStyleLookup = 0x228u;
/* The page's footer prompts, three of 0x40 bytes, shown while the page's
   timeout (+0x1554) is at most [0x00680030]. */
constexpr std::uint32_t kPrompts = 0x1310u;
constexpr std::uint32_t kPromptBytes = 0x40u;
constexpr std::uint32_t kPromptCount = 3u;
constexpr std::uint32_t kTimeout = 0x1554u;
constexpr std::uint32_t kPromptTimeout = 0x00680030u;

constexpr std::uint32_t kButtonDown = 0x201u;
constexpr std::uint32_t kButtonUp = 0x202u;
constexpr std::uint32_t kPointerMove = 0x200u;

constexpr float kHidden = menu_scene::kHidden;
constexpr float kModelDepth = -720.0F;
constexpr float kFrameDepth = -175.0F;
/* The grown frame comes in front of the menu it covers. */
constexpr float kTouchFrameDepth = -450.0F;
constexpr float kPi = 3.14159265F;
/* ui/hud/dialog's extents at its retail placement, and ui/hud/dialog_select's
   about its origin; measured from a 1280x720 frame. */
constexpr SceneBox kFrameArt{77.0F, 91.0F, 437.0F, 321.0F};
constexpr SceneBox kBarArt{-3.7F, -16.6F, 339.7F, 1.8F};
/* A drag starts after this share of a card pitch. */
constexpr float kDragStart = 0.25F;
/* Card and button text sits this far in from the left of its bar. */
constexpr float kTextPad = 0.3F;

constexpr int kCards = 8;
constexpr int kButtons = static_cast<int>(kPromptCount);

enum class Scene { menu, world };

std::uint32_t bits(float value) {
  std::uint32_t out = 0;
  std::memcpy(&out, &value, sizeof out);
  return out;
}

std::optional<x2::presentation::RetailScenePlane> scene_plane() {
  const x2::native::LiveGuestMemory memory;
  return x2::native::read_scene_plane(memory, x86_module_base("XMen2.exe"));
}

std::uint32_t page_of(std::uint32_t popup) {
  return popup + popup::page_offset(static_cast<std::int32_t>(
                     RD32(popup + popup::kCurrentPage)));
}

bool entry_enabled(std::uint32_t page, int entry) {
  const std::uint32_t word = RD32(page + popup::kEnabledBits +
                                  static_cast<std::uint32_t>(entry / 32) * 4u);
  return ((word >> (entry % 32)) & 1u) != 0u;
}

/* A footer prompt's words: the text after its `$MENU_*` token. */
std::string prompt_words(std::string_view prompt) {
  const std::size_t token = prompt.find("$MENU");
  if (token == std::string_view::npos) {
    return std::string(prompt);
  }
  const std::size_t space = prompt.find(' ', token);
  return space == std::string_view::npos
             ? std::string()
             : std::string(prompt.substr(space + 1));
}

class PopupTouch {
public:
  void attach(const CPU &cpu, Scene scene) {
    x2::guest::GuestCall call(cpu);
    load_bars(call);
    for (const std::uint32_t bar : bars_) {
      if (bar != 0u) {
        scene_call(call, scene, true, bar);
      }
    }
  }

  void detach(const CPU &cpu, Scene scene) {
    x2::guest::GuestCall call(cpu);
    for (const std::uint32_t bar : bars_) {
      if (bar != 0u) {
        scene_call(call, scene, false, bar);
      }
    }
  }

  /* The touch draw; false leaves the popup to retail's. */
  bool draw(const CPU &cpu, std::uint32_t popup) {
    x2::native::popup_touch_drawn() = {};
    const std::uint32_t page = page_of(popup);
    if ((RD8(page + popup::kFlags) & popup::kOpen) == 0u || bars_[0] == 0u) {
      return false;
    }
    const auto plane = scene_plane();
    Vectors vectors;
    if (!plane || !vectors) {
      return false;
    }
    x2::guest::GuestCall call(cpu);
    const std::uint32_t box = RD32(popup + popup::kTextBox);
    const auto count = static_cast<std::int8_t>(RD8(page + popup::kEntryCount));
    std::array<std::string, kPromptCount> prompts{};
    int footers = 0;
    if (RDF32(page + kTimeout) <=
        RDF32(x2::native::exe_linked(kPromptTimeout))) {
      for (std::uint32_t i = 0; i < kPromptCount; ++i) {
        const std::string_view prompt = x2::guest::guest_string(
            page + kPrompts + i * kPromptBytes, kPromptBytes);
        if (!prompt.empty()) {
          prompts[static_cast<std::size_t>(footers++)] = std::string(prompt);
        }
      }
    }

    x2::input::PopupTouchInput input;
    input.plane = *plane;
    input.frame = kFrameArt;
    input.text_left = static_cast<float>(
        static_cast<std::int32_t>(RD32(box + popup::kBoxLeft)));
    input.text_top = static_cast<float>(
        static_cast<std::int32_t>(RD32(box + popup::kBoxTop)));
    input.line = line_height(call, popup);
    input.finger = x2::input::menu_finger_target(*plane) * plane->height() /
                   static_cast<float>(plane->client_height());
    const auto projected = x2::native::load_game_menu_list(popup);
    projected_ = projected.has_value();
    input.entries = projected ? static_cast<int>(projected->entries) : count;
    input.first_shown =
        projected ? static_cast<int>(projected->first)
                  : static_cast<std::int8_t>(RD8(page + popup::kFirstShown));
    input.footers = footers;
    input.max_rows = kCards;
    layout_ = x2::input::layout_popup_touch(input);
    if (layout_.cards.rows == 0) {
      return false;
    }
    show_from(page, layout_.cards.first);

    menu_scene::place(call, vectors, RD32(popup + popup::kFrameModel),
                      {layout_.frame_offset.x, layout_.frame_offset.z},
                      kTouchFrameDepth, layout_.frame_scale.x,
                      layout_.frame_scale.z, 0.0F);
    frame_moved_ = true;
    menu_scene::place(call, vectors, RD32(popup + popup::kBarModel),
                      {kHidden, kHidden}, 0.0F, 1.0F, 1.0F, 0.0F);
    menu_scene::place(call, vectors, RD32(popup + popup::kArrowUpModel),
                      layout_.cards.more_above ? layout_.arrow_up
                                               : ScenePoint{kHidden, kHidden},
                      kModelDepth, layout_.frame_scale.x, layout_.frame_scale.x,
                      0.0F);
    menu_scene::place(call, vectors, RD32(popup + popup::kArrowDownModel),
                      layout_.cards.more_below ? layout_.arrow_down
                                               : ScenePoint{kHidden, kHidden},
                      kModelDepth, layout_.frame_scale.x, layout_.frame_scale.x,
                      kPi);

    x2::native::PopupTouchDrawn &drawn = x2::native::popup_touch_drawn();
    drawn.shown = true;
    drawn.first_shown = page_first();
    drawn.first_entry = layout_.cards.first;
    const std::string_view title =
        x2::guest::guest_string(page, popup::kEntryBytes);
    if (!title.empty()) {
      text(call, vectors, box, layout_.title, layout_.title_scale,
           color(call, vectors, true, true), page);
    }
    const auto selected =
        static_cast<std::int8_t>(RD8(page + popup::kSelected));
    std::size_t bar = 0;
    for (std::size_t i = 0; i < layout_.cards.cards.size(); ++i) {
      const int entry = page_first() + static_cast<int>(i);
      const SceneBox &card = layout_.cards.cards[i];
      const bool enabled = entry_enabled(page, entry);
      place_bar(call, vectors, enabled ? bars_[bar++] : 0u, card);
      text(call, vectors, box, card_text_point(card), layout_.text_scale,
           color(call, vectors, entry == selected, enabled),
           page + popup::kEntries +
               static_cast<std::uint32_t>(entry) * popup::kEntryBytes);
      drawn.cards.push_back(card);
    }
    for (; bar < kCards; ++bar) {
      place_bar(call, vectors, bars_[bar], {});
    }
    for (std::size_t i = 0; i < layout_.buttons.size(); ++i) {
      const SceneBox &button = layout_.buttons[i];
      place_bar(call, vectors, bars_[bar++], button);
      const std::string words = prompt_words(prompts[i]);
      const x2::guest::GuestText label(words);
      if (label) {
        text(call, vectors, box, card_text_point(button), layout_.text_scale,
             color(call, vectors, true, true), label.address());
      }
      drawn.buttons.push_back({button, words});
      prompts_[i] = prompts[i];
    }
    for (; bar < bars_.size(); ++bar) {
      place_bar(call, vectors, bars_[bar], {});
    }
    return true;
  }

  /* Retail's own frame placement, when touch has left. */
  void restore_frame(const CPU &cpu, std::uint32_t popup) {
    if (!frame_moved_) {
      return;
    }
    Vectors vectors;
    if (!vectors) {
      return;
    }
    x2::guest::GuestCall call(cpu);
    menu_scene::place(call, vectors, RD32(popup + popup::kFrameModel),
                      {0.0F, 0.0F}, kFrameDepth, 1.0F, 1.0F, 0.0F);
    for (const std::uint32_t bar : bars_) {
      place_bar(call, vectors, bar, {});
    }
    frame_moved_ = false;
  }

  /* The finger on the touch layout; false when it is not shown. */
  bool pointer(std::uint32_t popup, ScenePoint point, std::uint32_t message) {
    if (!x2::native::popup_touch_drawn().shown) {
      return false;
    }
    const std::uint32_t page = page_of(popup);
    const auto count = static_cast<std::int8_t>(RD8(page + popup::kEntryCount));
    if (message == kButtonDown) {
      press_ = {true,
                false,
                point,
                layout_.cards.first,
                x2::input::box_hit(layout_.cards.cards, point),
                button_at(point)};
      return true;
    }
    if (!press_.held) {
      return message == kPointerMove;
    }
    const float dz = point.z - press_.start.z;
    if (!press_.dragging && std::fabs(dz) > kDragStart * layout_.cards.pitch) {
      press_.dragging = true;
    }
    if (press_.dragging) {
      show_from(page, x2::input::cards_scrolled(layout_.cards,
                                                press_.start_first, dz));
    }
    if (message != kButtonUp) {
      return true;
    }
    press_.held = false;
    if (press_.dragging) {
      return true;
    }
    const int card = x2::input::box_hit(layout_.cards.cards, point);
    if (card >= 0 && card == press_.card) {
      const int entry = page_first() + card;
      if (entry < count && entry_enabled(page, entry)) {
        WR8(page + popup::kSelected, static_cast<std::uint8_t>(entry));
        x2::native::post_menu_action(x2::native::MenuAction::accept);
      }
      return true;
    }
    const int button = button_at(point);
    if (button >= 0 && button == press_.button) {
      if (const auto action = x2::native::menu_token_action(
              prompts_[static_cast<std::size_t>(button)])) {
        x2::native::post_menu_action(*action);
      }
    }
    return true;
  }

private:
  struct Press {
    bool held = false;
    bool dragging = false;
    ScenePoint start;
    int start_first = 0;
    int card = -1;
    int button = -1;
  };

  /* The page entry the first card shows: Load Game keeps its window on the
     page from entry 0. */
  int page_first() const { return projected_ ? 0 : layout_.cards.first; }

  /* Shows `rows` entries of the list from `first`, through Load Game's
     window while it projects one. */
  void show_from(std::uint32_t page, int first) {
    layout_.cards.first = first;
    if (projected_) {
      x2::native::load_game_menu_show(
          static_cast<std::size_t>(first),
          static_cast<std::size_t>(layout_.cards.rows));
      return;
    }
    WR8(page + popup::kFirstShown, static_cast<std::uint8_t>(first));
    WR8(page + popup::kVisible, static_cast<std::uint8_t>(layout_.cards.rows));
  }

  int button_at(ScenePoint point) const {
    return x2::input::box_hit(layout_.buttons, point);
  }

  /* The text line call's z is the text's middle. */
  static ScenePoint card_text_point(const SceneBox &card) {
    return {card.left + kTextPad * card.height(),
            0.5F * (card.top + card.bottom)};
  }

  void load_bars(x2::guest::GuestCall &call) {
    if (bars_[0] != 0u) {
      return;
    }
    x2::guest::GuestBlock slot(4u);
    Vectors vectors;
    if (!slot || !vectors) {
      return;
    }
    for (std::uint32_t &bar : bars_) {
      WR32(slot.address(), 0u);
      call.thiscall(x2::native::exe_linked(kLoadModel), 0u,
                    {x2::native::exe_linked(kBarArtName), slot.address()});
      bar = RD32(slot.address());
      place_bar(call, vectors, bar, {});
    }
  }

  static void scene_call(x2::guest::GuestCall &call, Scene scene, bool attach,
                         std::uint32_t entity) {
    if (scene == Scene::menu) {
      if (attach) {
        menu_scene::attach(call, entity);
      } else {
        menu_scene::detach(call, entity);
      }
    } else {
      const std::uint32_t world =
          call.cdecl_call(x2::native::exe_linked(kWorldScene));
      call.virtual_call(world, attach ? kWorldAttach : kWorldDetach,
                        {entity, 0u});
    }
  }

  /* The bar art stretched over `box`; an empty box parks it. */
  static void place_bar(x2::guest::GuestCall &call, Vectors &vectors,
                        std::uint32_t bar, const SceneBox &box) {
    menu_scene::stretch(call, vectors, bar, kBarArt, box, kModelDepth);
  }

  static float line_height(x2::guest::GuestCall &call, std::uint32_t popup) {
    const std::uint32_t fonts = call.cdecl_call(x2::native::exe_linked(kFonts));
    const auto width =
        static_cast<std::int16_t>(RD16(popup + popup::kTextWidth));
    call.virtual_call(fonts, kTextHeight,
                      {static_cast<std::uint32_t>(width),
                       x2::native::exe_linked(kLineProbe), bits(1.0F), 0u, 1u});
    return static_cast<float>(call.x87_result());
  }

  static std::uint32_t color(x2::guest::GuestCall &call, Vectors &vectors,
                             bool selected, bool enabled) {
    if ((RD32(x2::native::exe_linked(kStyleReady)) & kMenuStyleReady) == 0u) {
      WR32(x2::native::exe_linked(kStyleReady),
           RD32(x2::native::exe_linked(kStyleReady)) | kMenuStyleReady);
      const std::uint32_t name = vectors.put(1, 0.0F, 0.0F, 0.0F);
      call.thiscall(x2::native::exe_linked(kNameHash), name,
                    {x2::native::exe_linked(kStyleName)});
      const std::uint32_t manager =
          call.cdecl_call(x2::native::exe_linked(kMenuManager));
      WR32(x2::native::exe_linked(kMenuStyle),
           call.virtual_call(manager, kStyleLookup, {name}));
    }
    return menu_scene::style_color(call, vectors,
                                   RD32(x2::native::exe_linked(kMenuStyle)),
                                   selected, enabled);
  }

  static void text(x2::guest::GuestCall &call, Vectors &, std::uint32_t box,
                   ScenePoint at, float scale, std::uint32_t colour,
                   std::uint32_t string) {
    call.cdecl_call(
        x2::native::exe_linked(kTextLine),
        {box + popup::kBoxLines,
         static_cast<std::uint32_t>(static_cast<std::int32_t>(at.x)),
         static_cast<std::uint32_t>(static_cast<std::int32_t>(at.z)),
         RD32(box + popup::kBoxWidth), RD32(box + popup::kBoxStyle),
         bits(scale), 0u, colour, x2::native::exe_linked(kLineFormat), string});
  }

  std::array<std::uint32_t, kCards + kButtons> bars_{};
  std::array<std::string, kPromptCount> prompts_{};
  x2::input::PopupTouchLayout layout_;
  Press press_;
  bool frame_moved_ = false;
  bool projected_ = false;
};

PopupTouch g_popup_touch;

bool on_touch() { return x2::input::touch_runtime_active() != 0; }

void scene_hook(CPU *cpu, std::uint32_t address, Scene scene, bool attach) {
  const std::uint32_t popup = cpu->reg[kX86pEcx];
  const bool was = (RD8(popup + kAttached) & 1u) != 0u;
  if (!attach && was) {
    g_popup_touch.detach(*cpu, scene);
  }
  x86_guest_body(cpu, "XMen2.exe", address);
  if (attach && !was && (RD8(popup + kAttached) & 1u) != 0u) {
    g_popup_touch.attach(*cpu, scene);
  }
}

} // namespace

extern "C" void x2_override_005ea9e0(CPU *cpu) {
  const std::uint32_t popup = cpu->reg[kX86pEcx];
  if (on_touch() && g_popup_touch.draw(*cpu, popup)) {
    cpu->reg[kX86pEsp] += 4u;
    return;
  }
  g_popup_touch.restore_frame(*cpu, popup);
  x86_guest_body(cpu, "XMen2.exe", kPopupDraw);
}

extern "C" void x2_override_005ea2c0(CPU *cpu) {
  const std::uint32_t esp = cpu->reg[kX86pEsp];
  const ScenePoint point{
      static_cast<float>(static_cast<std::int32_t>(RD32(esp + 4u))),
      static_cast<float>(static_cast<std::int32_t>(RD32(esp + 8u)))};
  if (on_touch() &&
      g_popup_touch.pointer(cpu->reg[kX86pEcx], point, RD32(esp + 16u))) {
    cpu->reg[kX86pEax] = 1u;
    cpu->reg[kX86pEsp] += 4u + 20u;
    return;
  }
  x86_guest_body(cpu, "XMen2.exe", kPopupOnMouse);
}

extern "C" void x2_override_005e91c0(CPU *cpu) {
  scene_hook(cpu, kAttachMenu, Scene::menu, true);
}

extern "C" void x2_override_005e90d0(CPU *cpu) {
  scene_hook(cpu, kAttachWorld, Scene::world, true);
}

extern "C" void x2_override_005e93b0(CPU *cpu) {
  scene_hook(cpu, kDetachMenu, Scene::menu, false);
}

extern "C" void x2_override_005e92d0(CPU *cpu) {
  scene_hook(cpu, kDetachWorld, Scene::world, false);
}

__attribute__((constructor)) static void x2_popup_touch_register(void) {
  x86_register_override("XMen2.exe", kPopupDraw, x2_override_005ea9e0);
  x86_register_override("XMen2.exe", kPopupOnMouse, x2_override_005ea2c0);
  x86_register_override("XMen2.exe", kAttachMenu, x2_override_005e91c0);
  x86_register_override("XMen2.exe", kAttachWorld, x2_override_005e90d0);
  x86_register_override("XMen2.exe", kDetachMenu, x2_override_005e93b0);
  x86_register_override("XMen2.exe", kDetachWorld, x2_override_005e92d0);
}
