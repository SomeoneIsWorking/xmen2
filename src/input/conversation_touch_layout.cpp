#include "conversation_touch_layout.hpp"

#include <algorithm>

namespace x2::input {
namespace {

/* In fingers: the panel's inner margin, the speaker's and the line's text
   height and the gap under the speaker. */
constexpr float kInset = 0.3F;
constexpr float kSpeakerText = 0.45F;
constexpr float kLineText = 0.4F;
constexpr float kSpeakerGap = 0.1F;
/* Lines of text kept before the cards scroll; the most responses in one
   column, and the gap between two columns. */
constexpr float kTextFloor = 2.0F;
constexpr int kOneColumn = 3;
constexpr float kColumnGap = 0.2F;

bool usable(const ConversationTouchInput &input) {
  return input.panel.width() > 0.0F && input.panel.height() > 0.0F &&
         input.line > 0.0F && input.finger > 0.0F && input.text_width > 0.0F;
}

int columns(const ConversationTouchInput &input) {
  return input.responses > kOneColumn ? 2 : 1;
}

int row_count(const ConversationTouchInput &input) {
  const int across = columns(input);
  return (std::max(0, input.responses) + across - 1) / across;
}

float above_text(const ConversationTouchInput &input) {
  return kInset * input.finger +
         (input.speaker ? (kSpeakerText + kSpeakerGap) * input.finger : 0.0F);
}

float below_text(const ConversationTouchInput &input, int rows) {
  const float finger = input.finger;
  const float pitch = (kCardHeight + kCardGap) * finger;
  return static_cast<float>(rows) * pitch + kInset * finger;
}

} // namespace

ConversationTouchText
conversation_touch_text(const ConversationTouchInput &input) {
  ConversationTouchText out;
  if (!usable(input)) {
    return out;
  }
  out.width = input.text_width;
  out.scale = kLineText * input.finger / input.line;
  /* Every row fits beside the text when it can; a long line keeps two lines
     of room and one row, and the rows scroll. */
  const float free = input.panel.height() - above_text(input);
  const int rows = row_count(input);
  const float one_row = free - below_text(input, std::min(rows, 1));
  const float all_rows = free - below_text(input, rows);
  const float floor = std::min(kTextFloor * kLineText * input.finger, one_row);
  out.room = std::max(0.0F, std::clamp(all_rows, floor, one_row));
  return out;
}

ConversationTouchLayout
layout_conversation_touch(const ConversationTouchInput &input) {
  ConversationTouchLayout out;
  if (!usable(input)) {
    return out;
  }
  const float finger = input.finger;
  out.speaker_scale = kSpeakerText * finger / input.line;
  out.text_scale = input.text_scale > 0.0F ? input.text_scale
                                           : kLineText * finger / input.line;
  out.card_text_scale = kCardText * finger / input.line;
  const float line = input.line * out.text_scale;
  const float text_height = std::max(input.text_height, line);

  const float top = input.panel.top - kInset * finger;
  const float left = input.text_left;
  const float right = input.text_left + input.text_width;
  out.speaker = {left, top - 0.5F * kSpeakerText * finger};
  const float text_top =
      top - (input.speaker ? (kSpeakerText + kSpeakerGap) * finger : 0.0F);
  out.text = {left, text_top - text_height, right, text_top};
  out.text_at = {left, text_top - 0.5F * line};

  /* The rows sit at the panel's bottom, under the thumbs. */
  const float bottom = input.panel.bottom + kInset * finger;
  const SceneBox room{left, bottom, right, out.text.bottom - kCardGap * finger};
  const int rows = row_count(input);
  out.rows = stack_cards(room, finger, rows, input.first_row, rows);
  const float drop =
      out.rows.cards.empty() ? 0.0F : out.rows.cards.back().bottom - bottom;
  for (SceneBox &row : out.rows.cards) {
    row.bottom -= drop;
    row.top -= drop;
  }
  out.columns = columns(input);
  out.first = out.rows.first * out.columns;
  const float gap = kColumnGap * finger;
  const float width =
      (input.text_width - gap * static_cast<float>(out.columns - 1)) /
      static_cast<float>(out.columns);
  for (const SceneBox &row : out.rows.cards) {
    for (int column = 0; column < out.columns; ++column) {
      if (out.first + static_cast<int>(out.cards.size()) >= input.responses) {
        break;
      }
      const float at = left + static_cast<float>(column) * (width + gap);
      out.cards.push_back({at, row.bottom, at + width, row.top});
    }
  }
  return out;
}

} // namespace x2::input
