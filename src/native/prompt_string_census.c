/* What the retail glyph loop is asked to draw, for the run report.
 *
 * Every string entering FUN_005ee780 is counted, and every DISTINCT string is
 * dumped once as raw codepoints. prompt_glyph_draw.c owns what happens to a
 * string; this owns only the evidence of what arrived, so that "no prompt
 * reached the renderer" can be told apart from "nothing was drawn" and from
 * "prompts arrived remapped".
 */
#include "prompt_string_census.h"

#include "guest_memory.h"
#include "pad_glyph_codes.h" /* GENERATED: the published run */
#include "prompt_glyphs.h"
#include "x2_log.h"
#include "x86rt.h"

static unsigned long g_strings, g_with_prompts, g_prompt_codepoints;
/* The NEAR MISS denominator. A prompt codepoint is inside the byte range,
   not up in a private plane -- so "no prompt codepoint arrived" and "no
   non-ASCII arrived at all" are different answers and the report has to be
   able to tell them apart. Without this, a label whose codepoints were
   remapped, narrowed or shifted on the way here reads exactly like a label
   that was never drawn. */
static unsigned long g_with_non_ascii;

/* Sample by DISTINCT CONTENT, not by arrival order. A flat "first N" cap
   spends its whole budget on the legal screen and the engine's repeated
   control words -- the run that mattered dumped `9d28 01f2` eight times and
   never showed the one string shape worth seeing. Keyed on a hash of the
   wchars, so a string repeated 2,000 times costs one slot. */
#define DISTINCT_SLOTS 96u
static uint32_t g_seen_hash[DISTINCT_SLOTS];
static unsigned g_distinct;
static unsigned long g_distinct_dropped;

int x2_prompt_codepoint(uint16_t c) {
  /* The pad-only bound would classify a keyboard key as ordinary text. */
  return c >= X2_PROMPT_GLYPH_FIRST && c <= X2_PROMPT_GLYPH_LAST;
}

uint32_t x2_prompt_string_hash(uint32_t s_guest, unsigned *length_out) {
  uint32_t h = 2166136261u;
  unsigned i;
  for (i = 0; i < X2_PROMPT_WALK_MAX; i++) {
    uint16_t c = RD16(s_guest + (uint32_t)i * 2u);
    if (!c) {
      break;
    }
    h = (h ^ c) * 16777619u;
  }
  if (length_out) {
    *length_out = i;
  }
  return h ? h : 1u;
}

/* First sighting of this exact content? Records it if there is room. */
static int first_sighting(uint32_t hash) {
  unsigned i;
  for (i = 0; i < g_distinct; i++) {
    if (g_seen_hash[i] == hash) {
      return 0;
    }
  }
  if (g_distinct == DISTINCT_SLOTS) {
    g_distinct_dropped++;
    return 0;
  }
  g_seen_hash[g_distinct++] = hash;
  return 1;
}

int x2_string_has_prompt_glyph(uint32_t s_guest, unsigned max) {
  unsigned i;
  if (!s_guest) {
    return 0;
  }
  for (i = 0; i < max; i++) {
    uint16_t c = RD16(s_guest + (uint32_t)i * 2u);
    if (!c) {
      return 0;
    }
    if (x2_prompt_codepoint(c)) {
      return 1;
    }
  }
  return 0;
}

static void log_example(uint32_t where) {
  char buf[X2_PROMPT_WALK_MAX + 1];
  unsigned i;
  x2_log_error("PROMPT DRAW: string at guest 0x%08x wchars:", where);
  for (i = 0; i < X2_PROMPT_WALK_MAX; i++) {
    uint16_t c = RD16(where + (uint32_t)i * 2u);
    if (!c) {
      break;
    }
    buf[i] = (c >= 0x20 && c < 0x7f)  ? (char)c
             : x2_prompt_codepoint(c) ? '#'
                                      : '?';
    x2_log_error(" %04x", c);
  }
  buf[i] = 0;
  x2_log_error("  = \"%s\"\n", buf);
}

void x2_prompt_string_census(uint32_t s_guest) {
  unsigned i;
  int non_ascii = 0;

  g_strings++;
  if (!s_guest) {
    return;
  }
  if (x2_prompt_glyphs_enabled() &&
      x2_string_has_prompt_glyph(s_guest, X2_PROMPT_WALK_MAX)) {
    g_with_prompts++;
    for (i = 0; i < X2_PROMPT_WALK_MAX; i++) {
      uint16_t c = RD16(s_guest + (uint32_t)i * 2u);
      if (!c) {
        break;
      }
      if (x2_prompt_codepoint(c)) {
        g_prompt_codepoints++;
      }
    }
  } else {
    /* WHAT is being drawn, if not prompts? The boring case is what gets
       capped: a control word repeated two thousand times costs one slot, so
       the budget survives long enough to reach whatever draws late. */
    for (i = 0; i < X2_PROMPT_WALK_MAX; i++) {
      uint16_t c = RD16(s_guest + (uint32_t)i * 2u);
      if (!c) {
        break;
      }
      if (c >= 0x80) {
        non_ascii = 1;
        break;
      }
    }
    if (non_ascii) {
      g_with_non_ascii++;
    }
  }
  /* Every DISTINCT string, prompt or not: the first eight of a run were eight
     copies of the loading screen's Enter cap, and the footers a player asks
     about were never shown. */
  if (first_sighting(x2_prompt_string_hash(s_guest, NULL))) {
    log_example(s_guest);
  }
}

void x2_prompt_string_census_report(void) {
  x2_log_error("PROMPT DRAW: %lu string(s) reached the glyph loop, %lu "
               "carried prompt codepoint(s) (0x%02x..0x%02x), %lu "
               "codepoint(s) total, %lu carried some other non-ASCII "
               "wchar\n",
               g_strings, g_with_prompts, X2_PROMPT_GLYPH_FIRST,
               X2_PROMPT_GLYPH_LAST, g_prompt_codepoints, g_with_non_ascii);
  x2_log_error("PROMPT DRAW: %u distinct string(s) dumped%s\n", g_distinct,
               g_distinct_dropped ? " (SLOTS FULL -- later distinct strings "
                                    "went undumped, so this list is not the "
                                    "whole set)"
                                  : "");
  if (!g_strings) {
    x2_log_error("PROMPT DRAW: ZERO strings seen -- either nothing "
                 "drew text in this run or the override never armed.\n");
  } else if (!x2_prompt_glyphs_enabled()) {
    x2_log_error("PROMPT DRAW: native prompt glyphs were DISABLED for this "
                 "run (X2_PROMPT_GLYPHS=0), so no string could "
                 "have carried a prompt codepoint. This is not "
                 "evidence about the draw path.\n");
  } else if (!g_with_prompts) {
    x2_log_error("PROMPT DRAW: native prompt glyphs were enabled and "
                 "text drew, yet "
                 "no prompt codepoint reached the glyph loop. Compare "
                 "against the composed-label count in the prompt-label "
                 "report: labels composed but never arriving here means "
                 "they were not drawn in this run OR they reach the "
                 "screen by some other path.\n");
  }
}
