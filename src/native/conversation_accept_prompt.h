/* Whether the conversation panel draws its accept prompt. */
#ifndef X2_CONVERSATION_ACCEPT_PROMPT_H
#define X2_CONVERSATION_ACCEPT_PROMPT_H

#include <stdint.h>

/* The conversation singleton's flag byte (+0x21b24): the panel is showing. */
#define X2_CONVERSATION_VISIBLE 0x2u

/*
 * THE "Enter" BESIDE A REPLY IS THE CONVERSATION'S OWN "$MENU_ACCEPT" ICON.
 *
 * The conversation's per-frame update (FUN_0045d1a0, conversation.c) queues a
 * 32x32 "$MENU_ACCEPT" icon 36 px left of the highlighted reply while the
 * panel is visible. On the PC build that token expands to the accept action's
 * keycap -- "Enter" -- a cap with no words after it, so the prompt rewrite
 * (prompt_touch_buttons.c) rightly leaves it alone: the same shape on the
 * controls rebinding screen IS the binding.
 *
 * In touch play there is no Enter key to press; the reply line itself is the
 * control (a tap selects it through the retail mouse path). So the owner that
 * asks for the icon stops asking. Nothing downstream guesses from the drawn
 * string or its position, and every other lone cap is untouched.
 */
static inline int x2_conversation_draws_accept_prompt(uint8_t flags,
                                                      int touch_play) {
  return (flags & X2_CONVERSATION_VISIBLE) != 0u && !touch_play;
}

#endif
