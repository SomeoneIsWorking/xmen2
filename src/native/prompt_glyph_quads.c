/*
 * Where the port's prompt glyphs belong on screen, decided by the ENGINE.
 *
 * The alternative was for the port to lay the label out itself, which the
 * measurements ruled out: the engine's text run is right-anchored, so the
 * origin depends on the string's own content (C269 and docs/RE/text.md).
 * Instead the stock loop lays everything out with the port's published
 * metrics, and its own quad for each of our codepoints is intercepted at
 * FUN_005ee400. The retail emitter still receives a collapsed zero-area
 * rectangle so its batch-finalization contract survives, while nothing of
 * ours is drawn out of the game's font atlas.
 *
 * Which DRAW places a quad is the other half (issue #184). Handing the whole
 * harvest to the first draw put every element of a menu footer on one
 * element. Measured in the pause menu, the footer's "<B> Back" (5 glyphs) is
 * laid out among nine menu rows and drawn ninth of ten, alone, as a 5-glyph
 * draw; in a conversation the speaker's name (7), line (59) and response
 * button (1) are laid out in that order and drawn together as one 67-glyph
 * draw. A draw is therefore a contiguous window of the layout record whose
 * glyphs sum to the draw's own count, and draw order need not be layout
 * order.
 */
#include "prompt_glyph_quads.h"
#include "x2_log.h"

#include <stdio.h>
#include <string.h>

struct Run {
  uint32_t identity;
  unsigned emitted;
  unsigned first;
  unsigned count;
};

/* A frame's text: a few dozen strings, a handful of them prompts of a dozen
   or so pieces. The caps are real limits and overflow is COUNTED, never
   silently dropped -- a truncated harvest would draw a partial keycap and
   look like a rendering bug rather than a full buffer. */
static struct X2PromptQuad g_quads[X2_PROMPT_QUADS_MAX];
static unsigned g_count;
static struct Run g_runs[X2_PROMPT_RUNS_MAX];
static unsigned g_nruns;
static int g_open;
static int g_frame_had_any;
static unsigned long g_total, g_overflow, g_frames;
static unsigned long g_prompt_runs, g_taken, g_remeasured, g_undrawn;
static unsigned long g_evicted;

/* Six vertices a glyph is the engine's own accounting: the text sink's write
   cursor at [ecx+4] advances by exactly six across every glyph
   x2_override_005ee400 emits. Two fewer primitives than vertices is the
   triangle strip these draws declare, read off consecutive draws in one text
   pass, each of which began where the last one ended plus two: 96..156,
   180..216, 216..258, 258..318 (issue #180). */
unsigned x2_prompt_draw_glyphs(uint32_t primitives) {
  const uint32_t vertices = primitives + 2u;
  return vertices % 6u ? 0u : vertices / 6u;
}

/* Remove runs [at, at + n) and their quads, keeping the rest in order. */
static void remove_runs(unsigned at, unsigned n) {
  unsigned first = g_runs[at].first, quads = 0, i;
  for (i = at; i < at + n; i++)
    quads += g_runs[i].count;
  memmove(&g_quads[first], &g_quads[first + quads],
          (g_count - first - quads) * sizeof g_quads[0]);
  g_count -= quads;
  memmove(&g_runs[at], &g_runs[at + n], (g_nruns - at - n) * sizeof g_runs[0]);
  g_nruns -= n;
  for (i = at; i < g_nruns; i++)
    g_runs[i].first -= quads;
}

static unsigned count_undrawn(void) {
  unsigned i, n = 0;
  for (i = 0; i < g_nruns; i++)
    if (g_runs[i].count)
      n++;
  return n;
}

void x2_prompt_quads_reset(void) {
  if (g_frame_had_any)
    g_frames++;
  g_undrawn += count_undrawn();
  g_count = 0;
  g_nruns = 0;
  g_open = 0;
  g_frame_had_any = 0;
}

unsigned x2_prompt_quads_available(void) {
  return X2_PROMPT_QUADS_MAX - g_count;
}

int x2_prompt_quads_begin_run(uint32_t identity, unsigned emitted) {
  unsigned i;
  if (g_open)
    return 0;
  for (i = 0; i < g_nruns; i++)
    if (g_runs[i].identity == identity) {
      if (g_runs[i].count)
        g_remeasured++;
      remove_runs(i, 1u);
      break;
    }
  if (g_nruns == X2_PROMPT_RUNS_MAX) {
    if (g_runs[0].count)
      g_evicted++;
    remove_runs(0, 1u);
  }
  g_runs[g_nruns].identity = identity;
  g_runs[g_nruns].emitted = emitted;
  g_runs[g_nruns].first = g_count;
  g_runs[g_nruns].count = 0;
  g_nruns++;
  g_open = 1;
  return 1;
}

int x2_prompt_quads_add(const struct X2PromptQuad *quad) {
  if (!quad || !g_open)
    return 0;
  g_total++;
  if (g_count == X2_PROMPT_QUADS_MAX) {
    g_overflow++;
    return 0;
  }
  g_quads[g_count++] = *quad;
  g_runs[g_nruns - 1u].count++;
  g_frame_had_any = 1;
  return 1;
}

void x2_prompt_quads_end_run(void) {
  if (g_open && g_runs[g_nruns - 1u].count)
    g_prompt_runs++;
  g_open = 0;
}

unsigned x2_prompt_quads_take_run(unsigned glyphs, struct X2PromptQuad *out) {
  unsigned start, end, sum, quads, first;
  if (!glyphs || !out || !g_count || g_open)
    return 0;
  for (start = 0; start < g_nruns; start++) {
    sum = 0;
    quads = 0;
    for (end = start; end < g_nruns && sum < glyphs; end++) {
      sum += g_runs[end].emitted;
      quads += g_runs[end].count;
    }
    if (sum != glyphs || !quads)
      continue;
    first = g_runs[start].first;
    memcpy(out, &g_quads[first], quads * sizeof out[0]);
    remove_runs(start, end - start);
    g_taken++;
    return quads;
  }
  return 0;
}

unsigned x2_prompt_quads_pending(unsigned *quads) {
  if (quads)
    *quads = g_count;
  return count_undrawn();
}

void x2_prompt_quads_report(void) {
  x2_log_info("  Prompt quads: %lu harvested over %lu frame(s) that had any"
              "; %lu dropped past the %u cap\n",
              g_total, g_frames, g_overflow, X2_PROMPT_QUADS_MAX);
  x2_log_info("        %lu prompt string(s) laid out: %lu window(s) taken by "
              "the draw that submits them, %lu replaced by a re-measurement "
              "of the same string, %lu evicted from a full layout record, "
              "%lu never drawn before the frame ended\n",
              g_prompt_runs, g_taken, g_remeasured, g_evicted, g_undrawn);
  if (!g_total)
    x2_log_info("        NONE harvested -- either no label was drawn or the "
                "emitter interception never fired; the port has nothing to "
                "draw either way.\n");
  if (g_overflow)
    x2_log_info("        the preflight capacity contract was violated; native "
                "interception refuses to continue after this condition.\n");
}
