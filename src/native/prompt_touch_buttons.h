/* Retail action prompts, rewritten and published as touch controls. */
#ifndef X2_PROMPT_TOUCH_BUTTONS_H
#define X2_PROMPT_TOUCH_BUTTONS_H

#include "../presentation/aspect_fit.h"
#include "../presentation/touch_layout.h"

#include <stdint.h>

/*
 * "Esc Back" NAMES A KEY A PHONE DOES NOT HAVE.
 *
 * Every screen before gameplay -- the menus, the options, the load and save
 * screens -- draws its actions as a key and a word. In touch play this owner
 * takes the key off what is drawn, slides the remaining word into the space
 * it left, and publishes where that word landed as a control a finger can
 * press. Nothing about the retail layout, font or wording changes; the key
 * simply stops being how the action is offered.
 *
 * It sits on the text renderer because that is where both facts are: which
 * glyph belongs to the key (prompt_action_labels.c retained the composition)
 * and where the engine put every glyph. The rectangle is published at the
 * batch's finalized transform, exactly as the prompt art is drawn.
 */

/* Does this drawn string carry a composed keyboard prompt that touch play
   should rewrite? Returns the number of glyph-emitting characters the key
   occupies, 0 for every other string. Arms the accumulation below; the caller
   then hands every emitting glyph to x2_prompt_touch_glyph in order. */
unsigned x2_prompt_touch_begin(uint32_t string_guest, unsigned length);

/* What the emitter should do with the glyph at `emit_index` (counting only
   the characters that emit a quad). The suppressed head is collapsed; the
   rest slides left by the width the head occupied. Returns 1 when the
   rectangle was changed. */
int x2_prompt_touch_glyph(unsigned emit_index, float *x0, float *y0, float *x1,
                          float *y1);

/* The armed string has finished drawing: retain its rectangle for the batch's
   finalizer. */
void x2_prompt_touch_end(void);

/* Pure: an engine text-plane rectangle through this batch's finalized
   row-vector MVP into output pixels, inside the letterboxed `frame`. Returns
   0 for a non-finite or behind-the-eye result, leaving `out` untouched. */
int x2_prompt_touch_project(const float mvp[16], X2AspectRect frame,
                            X2Rect engine, X2Rect *out);

/*
 * A draw is finalizing: publish the retained prompt it is submitting, if any.
 *
 * `primitives` is that draw's own count, and it is how a prompt is matched to
 * the transform that places it. A frame lays every prompt out first and only
 * then draws them, one element per draw with its own world matrix, so the
 * finalizer cannot be asked "which prompt is this?" by order alone: emptying
 * the queue into the first finalizer's transform drew "Back" on top of
 * "Advanced Options", and taking one per draw in turn moved a dialog's second
 * prompt onto another element's line.
 *
 * The count answers it. A string's glyphs occupy six vertices each and the
 * draw declares two fewer primitives than vertices, so a draw of a 15-glyph
 * prompt declares 88 -- measured against a footer whose "Back" is 15 glyphs
 * and whose "Advanced Options" is 32. A draw whose count is not of that shape
 * belongs to no retained prompt and is left alone.
 */
/* How the caller obtains the finalized transform, asked for only once a
   retained prompt has been matched to this draw. Every non-indexed draw
   finalizes and almost none of them carry a prompt, so resolving the matrix
   first would multiply two 4x4s for every line of text on the screen. */
typedef int (*X2PromptTransform)(void *owner, float mvp[16]);

void x2_prompt_touch_publish(X2PromptTransform transform, void *owner,
                             uint32_t primitives);

void x2_prompt_touch_report(void);

#endif
