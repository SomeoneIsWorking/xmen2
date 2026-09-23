/* Presentation for the action labels produced by XMen2.exe FUN_00619e30.
 *
 * The retail function returns "[NAME]". A pad name is already a complete
 * picture and loses the brackets. A keyboard name becomes a keycap run
 * (keycap_run.h): layout-only edges around the retail name, over which the
 * drawer paints the whole key from shared art. A rebind changes the prompt
 * immediately, and the keyboard costs two codepoints. A name that cannot be
 * lettered (keycap_labels.h reports why) keeps the game's own "[NAME]": a key
 * drawn half in shared art and half in the game's font is the defect this
 * replaced.
 */
#include "prompt_labels.h"
#include "x2_log.h"

#include "guest_heap.h"
#include "keycap_labels.h"
#include "keycap_run.h"
#include "pad_glyph_codes.h"
#include "pad_glyphs.h"
#include "prompt_action_labels.h"
#include "prompt_glyphs.h"
#include "x86rt.h"
#include "x86rt_native.h"

#include "guest_body.h"
#include <stdio.h>
#include <string.h>

#define LABEL_BUFFER_BYTES 512u
#define MAX_RETAIL_LABEL 127u
/* FUN_006281f0's device kinds; see input_bindings.h. */
#define KEYBOARD_DEVICE_KIND 1u

static unsigned long g_unchanged, g_pad_labels, g_keycap_labels;
static unsigned long g_buffer_failures;

static uint32_t g_styled_label;

uint32_t x2_prompt_label_buffer(void) { return g_styled_label; }

/* WHO asks for these labels? The composed label is handed back as a return
   value, and nothing in this file knows whether the caller draws it, stores
   it or throws it away -- which is exactly the gap that made "2,264 labels
   composed, 0 prompt codepoints at the glyph loop" (C267) unreadable: a
   label READ is not a label DRAWN. Recording the return address at entry
   names the consumer from a real run, the same way the getTexture probe
   named the glyph loop. Always on: it is a handful of compares against a
   tiny table, and a census nobody arms is a census nobody has. */
#define MAX_LABEL_SITES 16
static uint32_t g_sites[MAX_LABEL_SITES];
static unsigned long g_site_counts[MAX_LABEL_SITES];
static unsigned g_n_sites;
static unsigned long g_site_overflow;

static void note_caller(uint32_t ret) {
  unsigned i;
  for (i = 0; i < g_n_sites; i++)
    if (g_sites[i] == ret) {
      g_site_counts[i]++;
      return;
    }
  if (g_n_sites == MAX_LABEL_SITES) {
    g_site_overflow++;
    return;
  }
  g_sites[g_n_sites] = ret;
  g_site_counts[g_n_sites] = 1;
  g_n_sites++;
}

static int pad_glyph_byte(uint8_t value) {
  return value >= X2_PAD_GLYPH_FIRST && value <= X2_PAD_GLYPH_LAST;
}

static int keycap_glyphs_available(void) {
  return x2_prompt_glyph_available(X2_KEYCAP_GLYPH_LEFT) &&
         x2_prompt_glyph_available(X2_KEYCAP_GLYPH_RIGHT);
}

enum PromptLabelStyle prompt_label_rewrite(const uint8_t *input,
                                           uint8_t *output, size_t capacity) {
  size_t length, name_length, i;
  uint16_t run[X2_KEYCAP_NAME_MAX + 2u];

  if (!input || !output || !capacity)
    return PROMPT_LABEL_UNCHANGED;
  length = strlen((const char *)input);
  if (length == 3u && input[0] == '[' && pad_glyph_byte(input[1]) &&
      input[2] == ']') {
    if (capacity < 2u || !x2_prompt_glyph_available(input[1]))
      return PROMPT_LABEL_UNCHANGED;
    output[0] = input[1];
    output[1] = 0;
    return PROMPT_LABEL_PAD_GLYPH;
  }
  if (length < 3u || input[0] != '[' || input[length - 1u] != ']')
    return PROMPT_LABEL_UNCHANGED;
  if (!keycap_glyphs_available())
    return PROMPT_LABEL_UNCHANGED;
  name_length = length - 2u;
  if (name_length > X2_KEYCAP_NAME_MAX ||
      (name_length == 3u && memcmp(input + 1u, "???", 3u) == 0))
    return PROMPT_LABEL_UNCHANGED;
  run[0] = X2_KEYCAP_GLYPH_LEFT;
  for (i = 0; i < name_length; i++) {
    run[i + 1u] = input[i + 1u];
  }
  run[name_length + 1u] = X2_KEYCAP_GLYPH_RIGHT;
  /* Composed only as a run the drawer will take, and lettered now, so a key
     that cannot be drawn whole is never composed. */
  if (x2_keycap_run_length(run, (unsigned)name_length + 2u, 0u) !=
          name_length + 2u ||
      !x2_keycap_label_art(run + 1u, (unsigned)name_length))
    return PROMPT_LABEL_UNCHANGED;
  if (name_length + 3u > capacity)
    return PROMPT_LABEL_UNCHANGED;

  output[0] = X2_KEYCAP_GLYPH_LEFT;
  memcpy(output + 1u, input + 1u, name_length);
  output[name_length + 1u] = X2_KEYCAP_GLYPH_RIGHT;
  output[name_length + 2u] = 0;
  return PROMPT_LABEL_KEYCAP;
}

void x2_override_00619e30(CPU *C) {
  uint8_t retail[MAX_RETAIL_LABEL + 1u];
  uint8_t styled[LABEL_BUFFER_BYTES];
  uint32_t out, kind = 0, code = 0;
  size_t length, name_length;
  enum PromptLabelStyle style;

  /* Before the super-call: the retail body pops its own return address. */
  note_caller(RD32(C->reg[kX86pEsp]));
  x86_guest_body(C, "XMen2.exe", 0x00619e30u);
  out = C->reg[kX86pEax];
  if (!out || !x2_prompt_glyphs_enabled()) {
    g_unchanged++;
    return;
  }
  for (length = 0; length < MAX_RETAIL_LABEL; length++) {
    retail[length] = RD8(out + (uint32_t)length);
    if (!retail[length])
      break;
  }
  if (length == MAX_RETAIL_LABEL) {
    g_unchanged++;
    return;
  }
  /* Kept before `length` is reused for the styled bytes: the name inside the
     cap is what the drawn string is recognised by. */
  name_length = length > 2u ? length - 2u : 0u;
  style = prompt_label_rewrite(retail, styled, sizeof styled);
  if (style == PROMPT_LABEL_UNCHANGED) {
    g_unchanged++;
    return;
  }
  if (!g_styled_label)
    g_styled_label = guest_malloc(LABEL_BUFFER_BYTES);
  if (!g_styled_label) {
    g_buffer_failures++;
    return;
  }
  length = strlen((const char *)styled) + 1u;
  for (size_t i = 0; i < length; i++)
    WR8(g_styled_label + (uint32_t)i, styled[i]);
  C->reg[kX86pEax] = g_styled_label;
  if (style == PROMPT_LABEL_PAD_GLYPH) {
    g_pad_labels++;
    return;
  }
  g_keycap_labels++;
  /* A keycap is the only label a finger can be offered instead of: a pad
     glyph already names a device the player is holding. Retained with the
     binding the namer just used, so the drawn string can be matched back to
     the key it describes. */
  if (x2_pad_glyph_last_named(&kind, &code) && kind == KEYBOARD_DEVICE_KIND)
    x2_prompt_action_label_note(retail + 1u, (unsigned)name_length, code);
}

__attribute__((constructor)) static void
x2_prompt_labels_register_override(void) {
  x86_register_override("XMen2.exe", 0x00619e30, x2_override_00619e30);
}

void prompt_labels_report(void) {
  static int done;
  unsigned i;
  if (done++)
    return;
  x2_log_info("  Prompt labels: %lu keycap, %lu pad, %lu unchanged; %lu guest "
              "buffer allocation failure(s)\n",
              g_keycap_labels, g_pad_labels, g_unchanged, g_buffer_failures);

  if (!g_n_sites) {
    x2_log_info("        asked for by NOBODY in this run -- 0 call(s) "
                "reached FUN_00619e30, so nothing here is evidence about "
                "where a label goes.\n");
    return;
  }
  x2_log_info("        asked for from %u distinct call site(s)%s:\n", g_n_sites,
              g_site_overflow ? " (TABLE FULL -- later sites went unrecorded)"
                              : "");
  for (i = 0; i < g_n_sites; i++)
    x2_log_info("           return to 0x%08x  x%lu\n", g_sites[i],
                g_site_counts[i]);
  if (g_site_overflow)
    x2_log_info("           %lu call(s) from sites past the table\n",
                g_site_overflow);
}
