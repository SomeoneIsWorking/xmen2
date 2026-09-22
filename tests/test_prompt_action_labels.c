/*
 * Recognising the port's own composed key cap inside a drawn string.
 *
 * Every mistake this test names was made against the running game. The first
 * matcher keyed on the exact bytes of a recently composed label and a screen
 * that composes a dozen prompts before drawing the first evicted them all.
 * The second required the cap to OPEN the string, and every menu footer in
 * the game reaches the glyph loop with the token marker its authored text
 * carried still in front of the cap -- so the one screen these prompts are
 * drawn on matched nothing while a dialog's bare "Esc Back" matched.
 */
#include "../src/native/prompt_action_labels.h"

#include "pad_glyph_codes.h"

#include <stdio.h>
#include <string.h>

static int g_checks;
static int g_failed;

#define CHECK(what, cond)                                                      \
  do {                                                                         \
    g_checks++;                                                                \
    if (!(cond)) {                                                             \
      g_failed++;                                                              \
      printf("    FAIL %s: %s\n", (what), #cond);                              \
    }                                                                          \
  } while (0)

/* The composition prompt_labels.c makes: left, middle x n, rewind x n, the
   name, right -- then whatever words follow it. */
static unsigned compose(uint16_t *out, unsigned at, const char *name,
                        const char *words) {
  const unsigned n = (unsigned)strlen(name);
  unsigned i;

  out[at++] = X2_KEYCAP_GLYPH_LEFT;
  for (i = 0; i < n; i++) {
    out[at++] = X2_KEYCAP_GLYPH_MIDDLE;
  }
  for (i = 0; i < n; i++) {
    out[at++] = X2_KEYCAP_GLYPH_REWIND;
  }
  for (i = 0; i < n; i++) {
    out[at++] = (uint16_t)name[i];
  }
  out[at++] = X2_KEYCAP_GLYPH_RIGHT;
  for (i = 0; words[i]; i++) {
    out[at++] = (uint16_t)words[i];
  }
  return at;
}

int main(void) {
  uint16_t drawn[64];
  X2PromptKeyCap cap;
  unsigned length;

  x2_prompt_action_labels_reset();
  x2_prompt_action_label_note((const uint8_t *)"Esc", 3u, 0x01u);
  x2_prompt_action_label_note((const uint8_t *)"Space", 5u, 0x39u);

  length = compose(drawn, 0, "Esc", " Back");
  CHECK("a cap that opens the string is claimed",
        x2_prompt_action_label_match(drawn, length, &cap));
  CHECK("with the key it named", cap.dik == 0x01u);
  CHECK("and the cap's own extent", cap.start == 0u && cap.end == 11u);

  /* The menu footer: the authored text's token marker precedes the cap. */
  drawn[0] = 0x3edu;
  length = compose(drawn, 1u, "Space", " Advanced Options");
  CHECK("a cap further in is claimed too",
        x2_prompt_action_label_match(drawn, length, &cap));
  CHECK("with its key", cap.dik == 0x39u);
  CHECK("and its extent measured from where it starts",
        cap.start == 1u && cap.end == 18u);

  length = compose(drawn, 0, "Esc", "");
  CHECK("a cap drawn alone IS the binding and is left alone",
        !x2_prompt_action_label_match(drawn, length, &cap));
  length = compose(drawn, 0, "Esc", "   ");
  CHECK("trailing spaces are not an action's words",
        !x2_prompt_action_label_match(drawn, length, &cap));

  length = compose(drawn, 0, "Tab", " Next");
  CHECK("a name no binding was retained under is not claimed",
        !x2_prompt_action_label_match(drawn, length, &cap));

  length = compose(drawn, 0, "Esc", " Back");
  drawn[length - 6u] = 'x';
  CHECK("a broken composition is not claimed",
        !x2_prompt_action_label_match(drawn, length, &cap));

  {
    const char *plain = "Advanced Options";
    unsigned i;
    for (i = 0; plain[i]; i++) {
      drawn[i] = (uint16_t)plain[i];
    }
    CHECK("ordinary words carry no cap",
          !x2_prompt_action_label_match(drawn, i, &cap));
  }

  /* The eviction that started this: many names retained, the first still
     answerable. */
  {
    unsigned i;
    for (i = 0; i < X2_PROMPT_ACTION_NAMES - 2u; i++) {
      char name[8];
      snprintf(name, sizeof name, "K%u", i);
      x2_prompt_action_label_note((const uint8_t *)name, (unsigned)strlen(name),
                                  0x20u + i);
    }
    length = compose(drawn, 0, "Esc", " Back");
    CHECK("a name retained before a screenful of others still answers",
          x2_prompt_action_label_match(drawn, length, &cap) &&
              cap.dik == 0x01u);
  }

  printf("test_prompt_action_labels: %d check(s), %d failed\n", g_checks,
         g_failed);
  return g_failed ? 1 : 0;
}
