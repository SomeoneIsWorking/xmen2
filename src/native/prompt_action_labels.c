#include "prompt_action_labels.h"

#include "keycap_run.h"

#include <string.h>

/*
 * A binding name is a handful of characters and a run uses a handful of them
 * -- Esc, Enter, Space, a letter. Keyed by name rather than by the composed
 * label, so a screen that composes twelve prompts before drawing the first
 * still knows what the first one said.
 */
struct Name {
  uint8_t text[X2_PROMPT_ACTION_NAME_BYTES];
  unsigned length;
  unsigned dik;
};

static struct Name g_names[X2_PROMPT_ACTION_NAMES];
static unsigned g_next;

void x2_prompt_action_labels_reset(void) {
  memset(g_names, 0, sizeof g_names);
  g_next = 0;
}

void x2_prompt_action_label_note(const uint8_t *name, unsigned length,
                                 unsigned dik) {
  unsigned i;
  struct Name *slot;

  if (!name || !length || !dik || length >= X2_PROMPT_ACTION_NAME_BYTES) {
    return;
  }
  for (i = 0; i < X2_PROMPT_ACTION_NAMES; i++) {
    if (g_names[i].length == length &&
        memcmp(g_names[i].text, name, length) == 0) {
      g_names[i].dik = dik;
      return;
    }
  }
  slot = &g_names[g_next];
  g_next = (g_next + 1u) % X2_PROMPT_ACTION_NAMES;
  memcpy(slot->text, name, length);
  slot->text[length] = 0;
  slot->length = length;
  slot->dik = dik;
}

/* The key retained for this name, or 0. */
static unsigned dik_of(const uint16_t *wide, unsigned at, unsigned length) {
  unsigned i, j;
  for (i = 0; i < X2_PROMPT_ACTION_NAMES; i++) {
    if (g_names[i].length != length) {
      continue;
    }
    for (j = 0; j < length; j++) {
      if (wide[at + j] != (uint16_t)g_names[i].text[j]) {
        break;
      }
    }
    if (j == length) {
      return g_names[i].dik;
    }
  }
  return 0;
}

/* Is there a visible character after `at`? A trailing run of spaces is not an
   action's name, and a cap followed by nothing is the binding itself. */
static int has_words_after(const uint16_t *wide, unsigned length, unsigned at) {
  unsigned i;
  for (i = at; i < length; i++) {
    if (wide[i] != ' ' && wide[i] != '\t') {
      return 1;
    }
  }
  return 0;
}

int x2_prompt_action_label_match(const uint16_t *wide, unsigned length,
                                 X2PromptKeyCap *out) {
  unsigned at;

  if (!wide || !out) {
    return 0;
  }
  for (at = 0; at + 3u <= length; at++) {
    unsigned cap, name_length, key;

    cap = x2_keycap_run_length(wide, length, at);
    if (!cap) {
      continue;
    }
    name_length = cap - 2u;
    if (!has_words_after(wide, length, at + cap)) {
      continue;
    }
    key = dik_of(wide, at + 1u, name_length);
    if (!key) {
      continue;
    }
    out->start = at;
    out->end = at + cap;
    out->dik = key;
    return 1;
  }
  return 0;
}
