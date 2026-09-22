#ifndef X2_TOUCH_PROMPT_BUTTONS_H
#define X2_TOUCH_PROMPT_BUTTONS_H

#include "../presentation/touch_layout.h"

#include <stddef.h>

#ifdef __cplusplus
#include <array>
#include <cstddef>
#include <cstdint>
#include <set>

#include <lucent/touch.h>
#endif

#ifdef __cplusplus
extern "C" {
#endif

/*
 * THE RETAIL UI'S OWN FOOTER PROMPTS, AS THINGS A FINGER CAN PRESS.
 *
 * "Esc Back" is drawn by the game on almost every screen before gameplay, and
 * on a phone it names a key nobody has. In touch play the port drops the key
 * from what is drawn and publishes where the remaining words landed; a
 * contact inside that control presses the key the prompt named.
 *
 * Published from the draw that produced it, once per frame the prompt is
 * drawn, so a screen that stops drawing one stops offering it. `now` is the
 * guest clock both here and at the hit test: an entry not refreshed within
 * X2_TOUCH_PROMPT_LIFETIME seconds is gone, which is what makes a screen
 * change take its buttons with it without anyone having to say so.
 */
/*
 * Alchemy lays text out when it CHANGES, not once a frame: a menu that has
 * been sitting still republishes its footer only as often as it rebuilds it,
 * and a run measured 1.054s between two publications of the same prompt. A
 * quarter of a second -- the first guess here -- therefore made every button
 * expire between redraws and answered "nothing is pressable" on a screen
 * plainly showing two prompts. This is that measurement with room over it.
 */
#define X2_TOUCH_PROMPT_LIFETIME 2.0
#define X2_TOUCH_PROMPTS_MAX 6u

/* `drawn` is the output-pixel rectangle of the prompt's remaining words;
   `dik` is the DirectInput key it named. A prompt already published for that
   key is moved rather than duplicated. */
void x2_touch_prompt_publish(X2Rect drawn, unsigned dik, double now);

/*
 * How a pressed prompt reaches the game's keyboard.
 *
 * The keyboard owner registers itself here rather than this owner reaching
 * into DirectInput: a prompt button knows WHICH key it names and nothing
 * about how a key is delivered, and the dependency the other way round would
 * drag the whole guest keyboard into every test of a rectangle. Returns
 * non-zero when the key is now held. Unregistered, a press is refused and
 * counted, never silently dropped.
 */
typedef int (*X2TouchKeyPress)(unsigned dik, double now);
void x2_touch_prompt_key_press(X2TouchKeyPress press);

/* One published prompt, for whoever has to look at it from outside: the
   control channel's listing, so a run can be driven to press the thing the
   player would press instead of a coordinate guessed before it started. */
typedef struct X2TouchPromptInfo {
  X2Rect target;
  unsigned dik;
} X2TouchPromptInfo;

/* How many prompts are live now, filling up to `capacity` of them. */
size_t x2_touch_prompts_live(X2TouchPromptInfo *out, size_t capacity);

#ifdef __cplusplus
}

namespace x2::input {

/* One published prompt: where a finger presses it, and what that presses. */
struct PromptButton {
  X2Rect target{};
  unsigned dik = 0;
  double at = 0.0;
};

class PromptButtons {
public:
  /* `drawn` is the text's own rectangle; the control grown around it is the
     layout's to decide, so both the hit test and the drawing use that one
     rectangle. Ignored when there is no viewport to grow it against. */
  void publish(X2LayoutViewport viewport, X2Rect drawn, unsigned dik,
               double now);
  /* The key a contact here would press, or 0. */
  unsigned key_at(float x, float y, double now) const;
  /*
   * A contact, if one of these buttons wants it.
   *
   * True means this owner took it, and the caller must not route it anywhere
   * else: a press has already been made, and the same finger arriving at the
   * retail pointer would also click whatever the prompt's words are drawn
   * over, then drag the cursor with it as it moves. The contact stays taken
   * until it lifts.
   */
  bool press(std::int64_t contact_id, float x, float y,
             lucent::touch::Phase phase, double now);
  /* Let go of every contact this owner is holding. */
  void release();
  /* The live buttons, newest publication order preserved. */
  std::size_t live(PromptButton *out, std::size_t capacity, double now) const;
  void clear();

private:
  std::array<PromptButton, X2_TOUCH_PROMPTS_MAX> buttons_{};
  std::set<std::int64_t> holding_;
};

/* The one set the draw path publishes into and the runtime routes against.
   It is owned here rather than by the runtime because the publisher is the
   text renderer, not the event pump: the runtime is a reader of it like the
   overlay is. */
PromptButtons &prompt_buttons();

} // namespace x2::input
#endif

#endif /* X2_TOUCH_PROMPT_BUTTONS_H */
