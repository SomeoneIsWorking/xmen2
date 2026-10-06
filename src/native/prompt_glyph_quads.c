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
 * Which DRAW places a quad is the other half (issue #184). A text pass lays
 * strings out before drawing them, draws them in another order, and one draw
 * may submit several adjacent strings with its own world matrix. Matching a
 * draw's glyph count to a window of laid-out strings guessed wrong whenever
 * two windows had the same size: a pause-menu row once took the footer's key
 * ("Blink [POR]tal", then "Objec[TIV]es"). The engine answers exactly
 * instead: every glyph is written at a known index of the batch's vertex
 * array, and every draw submits a known range of that array. A quad is keyed
 * by where its collapsed glyph went, and a draw takes the quads inside its
 * range.
 */
#include "prompt_glyph_quads.h"
#include "x2_log.h"

#include <string.h>

/* A frame's text: a few dozen strings, a handful of them prompts of a dozen
   or so pieces. The cap is a real limit and overflow is COUNTED, never
   silently dropped -- a truncated harvest would draw a partial keycap and
   look like a rendering bug rather than a full buffer. */
static struct X2PromptQuad g_quads[X2_PROMPT_QUADS_MAX];
static struct X2PromptVertexKey g_keys[X2_PROMPT_QUADS_MAX];
static unsigned g_count;
static int g_frame_had_any;
static unsigned long g_total, g_overflow, g_frames;
static unsigned long g_taken, g_draws, g_overwritten, g_undrawn;

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

void x2_prompt_quads_reset(void) {
  if (g_frame_had_any)
    g_frames++;
  g_undrawn += g_count;
  g_count = 0;
  g_frame_had_any = 0;
}

unsigned x2_prompt_quads_available(void) {
  return X2_PROMPT_QUADS_MAX - g_count;
}

/* Keep the quads for which `drop` is false, in order; return how many went. */
static unsigned drop_where(int (*drop)(const struct X2PromptVertexKey *,
                                       const void *),
                           const void *arg, struct X2PromptQuad *out) {
  unsigned i, kept = 0, dropped = 0;
  for (i = 0; i < g_count; i++) {
    if (drop(&g_keys[i], arg)) {
      if (out)
        out[dropped] = g_quads[i];
      dropped++;
      continue;
    }
    g_quads[kept] = g_quads[i];
    g_keys[kept] = g_keys[i];
    kept++;
  }
  g_count = kept;
  return dropped;
}

static int same_key(const struct X2PromptVertexKey *key, const void *arg) {
  const struct X2PromptVertexKey *other = arg;
  return key->vertex_array == other->vertex_array &&
         key->vertex == other->vertex;
}

int x2_prompt_quads_put(struct X2PromptVertexKey key,
                        const struct X2PromptQuad *quads, unsigned count) {
  unsigned i;
  if (!quads)
    return 0;
  g_overwritten += drop_where(same_key, &key, NULL);
  g_total += count;
  if (count > X2_PROMPT_QUADS_MAX - g_count) {
    g_overflow += count;
    return 0;
  }
  for (i = 0; i < count; i++) {
    g_quads[g_count] = quads[i];
    g_keys[g_count] = key;
    g_count++;
  }
  g_frame_had_any = 1;
  return 1;
}

struct Range {
  uint32_t vertex_array, start, vertices;
};

static int in_range(const struct X2PromptVertexKey *key, const void *arg) {
  const struct Range *range = arg;
  return key->vertex_array == range->vertex_array &&
         key->vertex - range->start < range->vertices;
}

unsigned x2_prompt_quads_take_range(uint32_t vertex_array, uint32_t start,
                                    uint32_t vertices,
                                    struct X2PromptQuad *out) {
  const struct Range range = {vertex_array, start, vertices};
  unsigned taken;
  if (!out || !vertices)
    return 0;
  taken = drop_where(in_range, &range, out);
  if (taken) {
    g_draws++;
    g_taken += taken;
  }
  return taken;
}

unsigned x2_prompt_quads_pending(void) { return g_count; }

void x2_prompt_quads_report(void) {
  x2_log_info("  Prompt quads: %lu harvested over %lu frame(s) that had any"
              "; %lu dropped past the %u cap\n",
              g_total, g_frames, g_overflow, X2_PROMPT_QUADS_MAX);
  x2_log_info("        %lu placed by the %lu draw(s) whose vertex range held "
              "them, %lu replaced because the engine wrote over their "
              "vertices first, %lu never drawn before the frame ended\n",
              g_taken, g_draws, g_overwritten, g_undrawn);
  if (!g_total)
    x2_log_info("        NONE harvested -- either no label was drawn or the "
                "emitter interception never fired; the port has nothing to "
                "draw either way.\n");
  if (g_overflow)
    x2_log_info("        the preflight capacity contract was violated; native "
                "interception refuses to continue after this condition.\n");
}
