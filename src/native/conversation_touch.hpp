#ifndef X2_CONVERSATION_TOUCH_HPP
#define X2_CONVERSATION_TOUCH_HPP

#include "../input/touch_cards.hpp"

#include <cstdint>
#include <string>
#include <vector>

/* A conversation on touch (docs/touch-play.md): what its last draw showed,
   for GET /menu and the conversation's own touch path. */
namespace x2::native {

struct ConversationTouchCard {
  input::SceneBox box;
  std::string text;
  /* The response's place among those drawn, its record id and the ids of
     the lines it leads to. */
  int tag = 0;
  std::uint32_t id = 0u;
  std::vector<std::uint32_t> next;
};

struct ConversationTouchDrawn {
  bool shown = false;
  /* A tap on a card picks it now; before, the update's gate drops it. */
  bool accepts = false;
  input::SceneBox panel;
  input::SceneBox text;
  std::string speaker;
  std::string line;
  std::vector<ConversationTouchCard> cards;
};

/* Written by the conversation's draw on the guest thread, read there; not
   shown once the conversation hides. */
const ConversationTouchDrawn &conversation_touch_drawn();

} // namespace x2::native

#endif
