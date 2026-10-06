#ifndef X2_TOUCH_SKIP_BUTTON_H
#define X2_TOUCH_SKIP_BUTTON_H

#include "../presentation/touch_layout.h"

#include <cstdint>
#include <optional>

#include <lucent/touch.h>

namespace x2::input {

/*
 * THE SKIP BUTTON A PHONE NEEDS WHERE A KEYBOARD HAS ESCAPE.
 *
 * Shown while the cutscene player offers a skip (cutscene_skip.h), which is
 * exactly while an authored sequence holds the player's controls. The
 * gameplay controls are hidden then, so this is the only touch control on a
 * cinematic and it owns every contact that lands on it.
 *
 * Pressing it requests the offered skip once, when the finger comes down: the
 * cutscene player's next input poll runs the same owned sequence completion
 * Escape runs. The contact is then held until it lifts, so the same finger
 * cannot fall through to the retail pointer and click whatever is beneath.
 */
class SkipButton {
public:
  /* Where the button sits in this viewport, in output pixels: the top-right
     corner inside the safe area, sized from the viewport's height so it is
     the same physical fraction of the screen on every display. */
  static X2Rect place(X2LayoutViewport viewport);

  /* Is a skip offered, so that the button is drawn and pressable? */
  static bool offered();

  /* A contact. True means this button took it and it must go nowhere else. */
  bool press(std::int64_t contact_id, float x, float y,
             lucent::touch::Phase phase, X2LayoutViewport viewport);

  /* Is a finger on it now? */
  bool held() const { return contact_.has_value(); }

  /* Let go of a held contact. */
  void release() { contact_.reset(); }

private:
  std::optional<std::int64_t> contact_;
};

} // namespace x2::input

#endif /* X2_TOUCH_SKIP_BUTTON_H */
