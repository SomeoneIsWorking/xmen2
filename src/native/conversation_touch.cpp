/*
 * A conversation on touch (docs/touch-play.md): inside the game's own panel,
 * the speaker and the line in readable text, each response a finger-tall
 * card of the game's selection bar art that a tap picks and a drag scrolls.
 * The layout is layout_conversation_touch; this file draws it with the
 * conversation's own text boxes after retail's line draw has composed its
 * strings, and takes the conversation's pointer handler.
 */
#include "conversation_touch.hpp"

#include "../input/conversation_touch_layout.hpp"
#include "../input/menu_touch_gaps.hpp"
#include "../input/touch_runtime.h"
#include "conversation_panel.hpp"
#include "guest_body.h"
#include "guest_call.hpp"
#include "guest_modules.h"
#include "menu_action.hpp"
#include "menu_scene.hpp"
#include "menu_text.hpp"
#include "popup_dialog.hpp"
#include "touch_menu_source.hpp"
#include "x2_log.h"
#include "x86rt.h"
#include "x86rt_native.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <optional>
#include <string>
#include <string_view>

namespace {

namespace cv = x2::native::conversation;
namespace box = x2::native::popup;
namespace menu_scene = x2::native::menu_scene;
namespace menu_text = x2::native::menu_text;
using x2::input::SceneBox;
using x2::native::menu_scene::Vectors;
using x2::presentation::ScenePoint;

/* The line record's draw (vtable 0x00685d74 slot 0), which composes the line
   and, through FUN_0045b0a0, each response the player may pick (vtable
   0x00685d6c slot 0, FUN_0045b7b0), into the two text boxes. */
constexpr std::uint32_t kLineDraw = 0x0045beb0u;
/* A record's text with its %TOKEN%s replaced (record, out, scratch), ret 8;
   "%NAME%: " becomes the speaker's name and a line break. */
constexpr std::uint32_t kCompose = 0x0045a340u;
/* The conversation's onMouse(x, z, _, message, _), ret 0x14 (vtable slot
   0x00685e68). */
constexpr std::uint32_t kOnMouse = 0x00459e70u;
/* Its shutdown (vtable +8), which releases its models, and its show/hide
   (bool), which detaches them. */
constexpr std::uint32_t kShutdown = 0x00455d90u;
constexpr std::uint32_t kSetVisible = 0x004556b0u;
/* Where FUN_00458410 places the panel, at scale 1. */
constexpr ScenePoint kPanelAt{255.0F, 110.0F};
/* The world scene and its attach and detach (entity, 0). */
constexpr std::uint32_t kWorldScene = 0x0059ee20u;
constexpr std::uint32_t kWorldAttach = 0x24u;
constexpr std::uint32_t kWorldDetach = 0x28u;
/* A text box's reset of its lines (FUN_005ef1a0, on the lines), its line
   call (lines, x, z, width, style, scale, flags, colour, format, ...) and
   "%s". */
constexpr std::uint32_t kResetLines = 0x005ef1a0u;
constexpr std::uint32_t kTextLine = 0x005ef270u;
constexpr std::uint32_t kLineFormat = 0x00685ca4u;
constexpr std::uint32_t kBoxRows = 0x400u;
/* Retail's highlight on the response it would accept (FUN_0045b7b0). */
constexpr std::string_view kHighlight = "~8";
constexpr std::string_view kSelectionArt = "ui/hud/dialog_select";

constexpr std::uint32_t kButtonDown = 0x201u;
constexpr std::uint32_t kButtonUp = 0x202u;
constexpr std::uint32_t kPointerMove = 0x200u;

/* The cards sit in front of the panel (depth 0) and the portraits (-30). */
constexpr float kCardDepth = -60.0F;
/* A drag starts after this share of a card pitch; card text sits this far
   in from the card's left, in card heights; a response may take two
   lines. */
constexpr float kDragStart = 0.25F;
constexpr float kTextPad = 0.3F;
constexpr int kLabelLines = 2;
constexpr float kLabelFill = 0.8F;
constexpr int kCards = 8;

std::string trimmed(std::string_view text) {
  const std::size_t first = text.find_first_not_of(' ');
  if (first == std::string_view::npos) {
    return {};
  }
  const std::size_t last = text.find_last_not_of(' ');
  return std::string(text.substr(first, last - first + 1u));
}

/* A composed line: the speaker before the first break, when "%NAME%: " gave
   it one, and the words after. */
struct LineText {
  std::string speaker;
  std::string words;
};

LineText split_line(std::string_view composed) {
  const std::size_t cut = composed.find('\n');
  if (cut == std::string_view::npos) {
    return {{}, trimmed(composed)};
  }
  return {trimmed(composed.substr(0, cut)), trimmed(composed.substr(cut + 1u))};
}

int lines_in(std::string_view text) {
  return 1 + static_cast<int>(std::count(text.begin(), text.end(), '\n'));
}

/* `words` copied to `out` and broken by the font's wrap at `width` and
   `scale`; how many lines it took. */
int wrap_lines(x2::guest::GuestCall &call, std::uint32_t lines,
               std::string_view words, std::uint32_t out, float width,
               float scale) {
  const std::size_t length =
      std::min<std::size_t>(words.size(), menu_text::kLineBytes - 4u);
  std::memcpy(guest_memory_as<char>(out), words.data(), length);
  WR8(out + static_cast<std::uint32_t>(length), 0u);
  menu_text::wrap_in_place(call, lines, out, width, scale);
  return lines_in(x2::guest::guest_string(out, menu_text::kLineBytes));
}

class ConversationTouch {
public:
  /* Retail's line draw is about to compose the strings of `line`. */
  void begin(std::uint32_t line) {
    capturing_ = true;
    line_ = line;
    composed_line_.reset();
    responses_.clear();
  }

  void composed(std::uint32_t record, std::uint32_t text) {
    if (!capturing_) {
      return;
    }
    const std::string value(x2::guest::guest_string(text, 0x200u));
    if (record == line_ && !composed_line_) {
      composed_line_ = value;
    } else if (record != line_) {
      responses_.push_back({record, value});
    }
  }

  void end() { capturing_ = false; }

  /* The touch draw over retail's; false leaves the panel to retail. */
  bool draw(const CPU &cpu) {
    drawn_.shown = false;
    const std::uint32_t self = RD32(x2::native::exe_linked(cv::kSingleton));
    const auto plane = x2::native::live_scene_plane();
    Vectors vectors;
    if (self == 0u || (RD8(self + cv::kFlags) & cv::kVisible) == 0u ||
        !composed_line_ || !plane || !vectors) {
      return false;
    }
    const auto tags = static_cast<std::int16_t>(RD16(self + cv::kTagCount));
    if (static_cast<std::size_t>(tags) != responses_.size()) {
      return false;
    }
    x2::guest::GuestCall call(cpu);
    load_bars(call, vectors);
    const std::uint32_t panel = RD32(self + cv::kBackground);
    if (bars_[0] == 0u || panel == 0u) {
      return false;
    }
    if (panel != panel_entity_) {
      panel_entity_ = panel;
      panel_art_ = menu_scene::bounds(call, vectors, panel);
      menu_scene::place(call, vectors, panel, kPanelAt, 0.0F, 1.0F, 1.0F);
    }
    const std::uint32_t line_box = RD32(self + cv::kLineBox);
    const std::uint32_t response_box = RD32(self + cv::kResponseBox);
    const LineText line = split_line(*composed_line_);

    x2::input::ConversationTouchInput input;
    input.plane = *plane;
    input.panel = {kPanelAt.x + panel_art_.left, kPanelAt.z + panel_art_.bottom,
                   kPanelAt.x + panel_art_.right, kPanelAt.z + panel_art_.top};
    input.text_left = static_cast<float>(
        static_cast<std::int32_t>(RD32(line_box + box::kBoxLeft)));
    input.text_width = static_cast<float>(
        static_cast<std::int32_t>(RD32(line_box + box::kBoxWidth)));
    input.line = menu_text::line_height(call, line_box + box::kBoxLines);
    input.finger = x2::input::menu_finger_units(*plane);
    input.speaker = !line.speaker.empty();
    input.responses = std::min<int>(tags, kCards);
    input.first_row = first_row_;
    x2::guest::GuestBlock words(menu_text::kLineBytes);
    x2::guest::GuestBlock label(menu_text::kLineBytes);
    if (!words || !label) {
      return false;
    }
    fit_words(call, line_box + box::kBoxLines, line.words, words.address(),
              input);
    layout_ = x2::input::layout_conversation_touch(input);
    if (layout_.cards.empty() && tags > 0) {
      return false;
    }
    first_row_ = layout_.rows.first;

    attach(call);
    clear_box(call, line_box);
    clear_box(call, response_box);
    const std::uint32_t line_colour =
        menu_scene::pack_color(call, line_box + box::kBoxColor);
    const std::uint32_t response_colour =
        menu_scene::pack_color(call, response_box + box::kBoxColor);
    if (!line.speaker.empty()) {
      const x2::guest::GuestText speaker(line.speaker);
      if (speaker) {
        text(call, line_box, layout_.speaker, layout_.speaker_scale,
             line_colour, speaker.address());
      }
    }
    if (!line.words.empty()) {
      text(call, line_box, layout_.text_at, layout_.text_scale, line_colour,
           words.address());
    }

    drawn_.panel = input.panel;
    drawn_.text = layout_.text;
    drawn_.speaker = line.speaker;
    drawn_.line = line.words;
    drawn_.cards.clear();
    drawn_.accepts = cv::takes_accept(cpu, self);
    const auto highlighted =
        static_cast<std::int16_t>(RD16(self + cv::kTagIndex));
    std::size_t bar = 0;
    for (std::size_t i = 0; i < layout_.cards.size(); ++i) {
      const int tag = layout_.first + static_cast<int>(i);
      const Response &response = responses_[static_cast<std::size_t>(tag)];
      const SceneBox &card = layout_.cards[i];
      menu_scene::stretch(call, vectors, bars_[bar++], bar_art_, card,
                          kCardDepth);
      const std::string label_words = trimmed(response.text);
      draw_label(call, response_box, label_words, card, tag == highlighted,
                 response_colour, label.address());
      drawn_.cards.push_back({card, label_words, tag,
                              RD32(response.record + cv::kRecordId),
                              children(response.record)});
    }
    for (; bar < bars_.size(); ++bar) {
      menu_scene::stretch(call, vectors, bars_[bar], bar_art_, {}, kCardDepth);
    }
    drawn_.shown = true;
    return true;
  }

  /* Touch has left with the conversation up: retail draws it again. */
  void restore(const CPU &cpu) {
    drawn_.shown = false;
    x2::guest::GuestCall call(cpu);
    detach(call);
  }

  /* The conversation hid: nothing of the touch draw stays up. */
  void hidden(const CPU &cpu) {
    drawn_ = {};
    first_row_ = 0;
    press_ = {};
    restore(cpu);
  }

  /* The conversation's models go; the cards go with them. */
  void release(const CPU &cpu) {
    hidden(cpu);
    x2::guest::GuestCall call(cpu);
    for (std::uint32_t &bar : bars_) {
      menu_scene::release(call, bar);
      bar = 0u;
    }
    panel_entity_ = 0u;
  }

  /* The finger on the touch layout; false when it is not shown. */
  bool pointer(ScenePoint point, std::uint32_t message) {
    if (!drawn_.shown) {
      return false;
    }
    if (message == kButtonDown) {
      press_ = {true, false, point, first_row_,
                x2::input::box_hit(layout_.cards, point)};
      return true;
    }
    if (!press_.held) {
      return message == kPointerMove || message == kButtonUp;
    }
    const float dz = point.z - press_.start.z;
    if (!press_.dragging && std::fabs(dz) > kDragStart * layout_.rows.pitch) {
      press_.dragging = true;
    }
    if (press_.dragging) {
      first_row_ =
          x2::input::cards_scrolled(layout_.rows, press_.start_row, dz);
    }
    if (message != kButtonUp) {
      return true;
    }
    press_.held = false;
    const int card = x2::input::box_hit(layout_.cards, point);
    if (press_.dragging || !drawn_.accepts || card < 0 || card != press_.card ||
        static_cast<std::size_t>(card) >= drawn_.cards.size()) {
      return true;
    }
    const x2::native::ConversationTouchCard &picked =
        drawn_.cards[static_cast<std::size_t>(card)];
    const std::uint32_t self = RD32(x2::native::exe_linked(cv::kSingleton));
    WR16(self + cv::kTagIndex, static_cast<std::uint16_t>(picked.tag));
    x2::native::post_menu_action(x2::native::MenuAction::accept);
    x2_log_info("conversation touch: a tap picks response %d of %zu, id %u, "
                "\"%s\"\n",
                picked.tag, responses_.size(), picked.id, picked.text.c_str());
    return true;
  }

  const x2::native::ConversationTouchDrawn &drawn() const { return drawn_; }

private:
  struct Response {
    std::uint32_t record = 0u;
    std::string text;
  };

  struct Press {
    bool held = false;
    bool dragging = false;
    ScenePoint start;
    int start_row = 0;
    int card = -1;
  };

  /* The line's words wrapped across the text span at the largest scale up
     to the nominal one at which they fit beside the speaker and the cards,
     as the popup fits its message. */
  static void fit_words(x2::guest::GuestCall &call, std::uint32_t lines,
                        const std::string &words, std::uint32_t out,
                        x2::input::ConversationTouchInput &input) {
    const x2::input::ConversationTouchText fit =
        x2::input::conversation_touch_text(input);
    const auto height_at = [&](float scale) {
      return static_cast<float>(
                 wrap_lines(call, lines, words, out, fit.width, scale)) *
             input.line * scale;
    };
    input.text_scale = menu_text::fitted_scale(
        fit.scale, [&](float scale) { return height_at(scale) <= fit.room; });
    input.text_height = height_at(input.text_scale);
  }

  /* A response's words on one line across its card, shrunk to fit; when
     even the smallest does not fit, on two lines a card holds. The lines
     sit centred on the card, the highlighted response in retail's
     highlight. */
  void draw_label(x2::guest::GuestCall &call, std::uint32_t text_box,
                  const std::string &words, const SceneBox &card, bool lit,
                  std::uint32_t colour, std::uint32_t out) const {
    const std::uint32_t lines = text_box + box::kBoxLines;
    const float width = card.width() - 2.0F * kTextPad * card.height();
    const float line = menu_text::line_height(call, lines);
    const auto lines_at = [&](float scale) {
      return wrap_lines(call, lines, words, out, width, scale);
    };
    float scale = menu_text::fitted_scale(
        layout_.card_text_scale, [&](float at) { return lines_at(at) == 1; });
    if (lines_at(scale) > 1) {
      const float two = std::min(layout_.card_text_scale,
                                 kLabelFill * card.height() /
                                     (static_cast<float>(kLabelLines) * line));
      scale = menu_text::fitted_scale(
          two, [&](float at) { return lines_at(at) <= kLabelLines; });
    }
    const int count = lines_at(scale);
    const ScenePoint at{card.left + kTextPad * card.height(),
                        0.5F * (card.top + card.bottom) +
                            0.5F * static_cast<float>(count - 1) * line *
                                scale};
    const std::string shown =
        (lit ? std::string(kHighlight) : std::string()) +
        std::string(x2::guest::guest_string(out, menu_text::kLineBytes));
    const x2::guest::GuestText drawn_text(shown);
    if (drawn_text) {
      text(call, text_box, at, scale, colour, drawn_text.address());
    }
  }

  static std::vector<std::uint32_t> children(std::uint32_t record) {
    std::vector<std::uint32_t> out;
    const std::uint32_t count =
        std::min(RD32(record + cv::kRecordChildCount), cv::kMaxChildren);
    for (std::uint32_t i = 0; i < count; ++i) {
      out.push_back(RD32(record + cv::kRecordChildren + i * 4u));
    }
    return out;
  }

  /* Both boxes keep only what the touch draw puts in them, as retail empties
     them before its own (FUN_0045beb0). */
  static void clear_box(x2::guest::GuestCall &call, std::uint32_t text_box) {
    WR32(text_box + kBoxRows, 0u);
    WR32(text_box + box::kBoxPen, 0u);
    call.thiscall(x2::native::exe_linked(kResetLines),
                  text_box + box::kBoxLines);
  }

  void load_bars(x2::guest::GuestCall &call, Vectors &vectors) {
    if (bars_[0] != 0u) {
      return;
    }
    for (std::uint32_t &bar : bars_) {
      bar = menu_scene::load_model(call, kSelectionArt);
    }
    bar_art_ = menu_scene::bounds(call, vectors, bars_[0]);
    for (const std::uint32_t bar : bars_) {
      menu_scene::stretch(call, vectors, bar, bar_art_, {}, kCardDepth);
    }
  }

  void attach(x2::guest::GuestCall &call) {
    if (attached_) {
      return;
    }
    world_call(call, kWorldAttach);
    attached_ = true;
  }

  void detach(x2::guest::GuestCall &call) {
    if (!attached_) {
      return;
    }
    world_call(call, kWorldDetach);
    attached_ = false;
  }

  void world_call(x2::guest::GuestCall &call, std::uint32_t slot) {
    const std::uint32_t world =
        call.cdecl_call(x2::native::exe_linked(kWorldScene));
    for (const std::uint32_t bar : bars_) {
      if (bar != 0u) {
        call.virtual_call(world, slot, {bar, 0u});
      }
    }
  }

  static void text(x2::guest::GuestCall &call, std::uint32_t text_box,
                   ScenePoint at, float scale, std::uint32_t colour,
                   std::uint32_t string) {
    call.cdecl_call(
        x2::native::exe_linked(kTextLine),
        {text_box + box::kBoxLines,
         static_cast<std::uint32_t>(static_cast<std::int32_t>(at.x)),
         static_cast<std::uint32_t>(static_cast<std::int32_t>(at.z)),
         RD32(text_box + box::kBoxWidth), RD32(text_box + box::kBoxStyle),
         x2::guest::float_bits(scale), 0u, colour,
         x2::native::exe_linked(kLineFormat), string});
  }

  bool capturing_ = false;
  std::uint32_t line_ = 0u;
  std::optional<std::string> composed_line_;
  std::vector<Response> responses_;
  std::array<std::uint32_t, kCards> bars_{};
  SceneBox bar_art_;
  std::uint32_t panel_entity_ = 0u;
  SceneBox panel_art_;
  x2::input::ConversationTouchLayout layout_;
  x2::native::ConversationTouchDrawn drawn_;
  Press press_;
  int first_row_ = 0;
  bool attached_ = false;
};

ConversationTouch g_conversation_touch;

bool on_touch() { return x2::input::touch_runtime_active() != 0; }

} // namespace

namespace x2::native {

const ConversationTouchDrawn &conversation_touch_drawn() {
  return g_conversation_touch.drawn();
}

} // namespace x2::native

extern "C" void x2_override_0045beb0(CPU *cpu) {
  g_conversation_touch.begin(cpu->reg[kX86pEcx]);
  x86_guest_body(cpu, "XMen2.exe", kLineDraw);
  g_conversation_touch.end();
  if (!on_touch() || !g_conversation_touch.draw(*cpu)) {
    g_conversation_touch.restore(*cpu);
  }
}

extern "C" void x2_override_0045a340(CPU *cpu) {
  const std::uint32_t record = cpu->reg[kX86pEcx];
  const std::uint32_t out = RD32(cpu->reg[kX86pEsp] + 4u);
  x86_guest_body(cpu, "XMen2.exe", kCompose);
  g_conversation_touch.composed(record, out);
}

extern "C" void x2_override_00459e70(CPU *cpu) {
  const std::uint32_t esp = cpu->reg[kX86pEsp];
  const ScenePoint point{
      static_cast<float>(static_cast<std::int32_t>(RD32(esp + 4u))),
      static_cast<float>(static_cast<std::int32_t>(RD32(esp + 8u)))};
  if (on_touch() && g_conversation_touch.pointer(point, RD32(esp + 16u))) {
    cpu->reg[kX86pEax] = 1u;
    cpu->reg[kX86pEsp] += 4u + 20u;
    return;
  }
  x86_guest_body(cpu, "XMen2.exe", kOnMouse);
}

extern "C" void x2_override_004556b0(CPU *cpu) {
  const std::uint32_t self = cpu->reg[kX86pEcx];
  x86_guest_body(cpu, "XMen2.exe", kSetVisible);
  if ((RD8(self + cv::kFlags) & cv::kVisible) == 0u) {
    g_conversation_touch.hidden(*cpu);
  }
}

extern "C" void x2_override_00455d90(CPU *cpu) {
  g_conversation_touch.release(*cpu);
  x86_guest_body(cpu, "XMen2.exe", kShutdown);
}

__attribute__((constructor)) static void x2_conversation_touch_register(void) {
  x86_register_override("XMen2.exe", kLineDraw, x2_override_0045beb0);
  x86_register_override("XMen2.exe", kCompose, x2_override_0045a340);
  x86_register_override("XMen2.exe", kOnMouse, x2_override_00459e70);
  x86_register_override("XMen2.exe", kSetVisible, x2_override_004556b0);
  x86_register_override("XMen2.exe", kShutdown, x2_override_00455d90);
}
