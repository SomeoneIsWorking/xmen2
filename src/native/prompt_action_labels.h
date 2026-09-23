/* The keys the port's composed action labels name. */
#ifndef X2_PROMPT_ACTION_LABELS_H
#define X2_PROMPT_ACTION_LABELS_H

#include <stdint.h>

/*
 * WHICH KEY DOES A DRAWN PROMPT NAME?
 *
 * The retail UI draws "Esc Back" as one wide string: the port's composed
 * keycap around the binding's own name, then the action's words. By the time
 * that string reaches the glyph loop the binding it describes is gone -- only
 * the letters remain, and reading a key out of them would mean a second,
 * guessing copy of the exe's name table, wrong for every rebinding and every
 * language.
 *
 * So the pairing is retained where it is still a fact. prompt_labels.c
 * composes the keycap immediately after FUN_006281f0 named the binding's
 * (device kind, physical code), and hands the NAME and the code here. A drawn
 * string is then recognised by the composition's own shape and its key looked
 * up by that name -- not by the exact bytes of one recent label, which a
 * screen composing a dozen prompts evicts before it draws the first.
 */
#define X2_PROMPT_ACTION_NAMES 32u
#define X2_PROMPT_ACTION_NAME_BYTES 32u

/* Retain `name` (the retail binding name, as it appears inside the cap) as
   DirectInput key `dik`. A name already retained is refreshed. */
void x2_prompt_action_label_note(const uint8_t *name, unsigned length,
                                 unsigned dik);

/* Where a composed keycap sits inside a drawn string, and the key it names.
   `start` and `end` are wide-character indices, `end` exclusive. */
typedef struct X2PromptKeyCap {
  unsigned start;
  unsigned end;
  unsigned dik;
} X2PromptKeyCap;

/*
 * Does this drawn wide string carry a composed keycap whose key is known?
 *
 * The composition is keycap_run.h's `left, name, right`, so its shape is
 * self-describing. It is searched for at any offset rather than only at the
 * front: a menu footer reaches the glyph loop with the token marker its
 * authored text carried still in front of the cap, and requiring the cap to
 * open the string left every footer in the game -- the one place these prompts
 * are drawn -- unclaimed while the difficulty dialog's bare "Esc Back" matched.
 *
 * Answers only for a cap that CONTINUES with a visible character: "Esc Back"
 * is an action a finger could press, a cap drawn by itself on the rebinding
 * screen IS the binding and must be left alone.
 */
int x2_prompt_action_label_match(const uint16_t *wide, unsigned length,
                                 X2PromptKeyCap *out);

/* Drop everything retained. The run's own reset; also used by the test. */
void x2_prompt_action_labels_reset(void);

#endif
