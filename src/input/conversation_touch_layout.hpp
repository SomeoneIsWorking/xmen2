#ifndef X2_CONVERSATION_TOUCH_LAYOUT_HPP
#define X2_CONVERSATION_TOUCH_LAYOUT_HPP

#include "touch_cards.hpp"

#include <vector>

/* A conversation on touch: inside the game's panel at its retail placement,
   the speaker and the line in readable text at the top and each response a
   finger-tall card at the bottom, in two columns when there are more than
   three, rows a drag scrolls when they do not all fit. Scene units, z up
   (docs/touch-play.md). */
namespace x2::input {

struct ConversationTouchInput {
  presentation::RetailScenePlane plane;
  /* ui/hud/convo_background's extents at its retail placement. */
  SceneBox panel;
  /* Retail's text boxes' left and width; the speaker's portrait sits left of
     the left. */
  float text_left = 0.0F;
  float text_width = 0.0F;
  /* One line of the panel's font at scale 1, and a finger. */
  float line = 0.0F;
  float finger = 0.0F;
  bool speaker = false;
  int responses = 0;
  /* The first row of cards shown. */
  int first_row = 0;
  /* The line wrapped at conversation_touch_text's width and `text_scale`:
     its height; 0 for one line at the nominal scale. */
  float text_scale = 0.0F;
  float text_height = 0.0F;
};

/* Where the line is wrapped: its width, its nominal scale, and the tallest
   it may be beside the speaker and the cards. */
struct ConversationTouchText {
  float width = 0.0F;
  float scale = 1.0F;
  float room = 0.0F;
};

ConversationTouchText
conversation_touch_text(const ConversationTouchInput &input);

struct ConversationTouchLayout {
  /* Text is placed by its left end's middle. */
  presentation::ScenePoint speaker;
  float speaker_scale = 1.0F;
  presentation::ScenePoint text_at;
  /* The span the line is wrapped into: across the panel, its lines down. */
  SceneBox text;
  float text_scale = 1.0F;
  /* The tallest a response's text may be, a line at most two. */
  float card_text_scale = 1.0F;
  int columns = 1;
  /* The rows of cards, a row per entry. */
  CardStack rows;
  /* The shown cards, left to right and down, from response `first`. */
  std::vector<SceneBox> cards;
  int first = 0;
};

ConversationTouchLayout
layout_conversation_touch(const ConversationTouchInput &input);

} // namespace x2::input

#endif
