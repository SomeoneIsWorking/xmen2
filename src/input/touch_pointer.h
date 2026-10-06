#ifndef X2_TOUCH_POINTER_H
#define X2_TOUCH_POINTER_H

#include "touch_controls.h"
#include "touch_runtime.h"

#include <cstdint>
#include <deque>

namespace x2::input {

/*
 * RETAIL'S ONE MOUSE POINTER, AS TOUCH MOVES IT.
 *
 * Two things a finger does reach the retail GUI rather than the pad: a
 * portrait tap during gameplay, which selects a hero, and a tap anywhere on a
 * screen that draws no control -- the intro, the main menu, the load and
 * pause screens. Both become Win32 mouse messages through the same queue the
 * guest's own message loop drains, and both obey the same rule, because
 * retail draws one cursor and has one button.
 *
 * Contacts on the screens with no drawn control used to be counted and
 * discarded, which is what left a phone player on the title screen with no
 * way off it: SDL's touch-to-mouse synthesis is deliberately off, so the
 * finger had no other route either.
 *
 * This owns the queue, who holds the button and where, so that a button
 * pressed by a finger is always released by something -- a lifted finger, a
 * lost window, or gameplay starting underneath it.
 */
class RetailPointer {
public:
  // A contact on a screen with no drawn control, at its own position. False
  // when another finger already holds the button; that contact presses
  // nothing rather than dragging the cursor out from under the first.
  bool contact(std::int64_t contact_id, lucent::touch::Point at,
               lucent::touch::Phase phase);

  // A tap the layout has already resolved -- a portrait, whose position is
  // the drawn portrait's centre and not the finger's. Ownership is arbitrated
  // by the caller's own PortraitPointer, so this only queues it.
  void resolved(lucent::touch::Point at, lucent::touch::Phase phase);

  // Lets the button go where it was pressed. True when something was held:
  // the caller counts that as a pointer event, because it is one.
  bool release_if_held();

  // The oldest queued event, for the Win32 pump. False when there is none.
  bool take(X2TouchPointer &out);

private:
  void queue(lucent::touch::Point at, int button_change);

  PointerOwner owner_;
  lucent::touch::Point at_{};
  std::deque<X2TouchPointer> pending_;
};

} // namespace x2::input

#endif /* X2_TOUCH_POINTER_H */
