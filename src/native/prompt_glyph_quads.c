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
 * This file is deliberately dumb storage. It is a frame's worth of
 * rectangles, and it says so when the harvest is empty.
 */
#include "prompt_glyph_quads.h"

#include <stdio.h>

/* A frame's labels: a handful of prompts, each a dozen or so pieces. The cap
   is a real limit and overflow is COUNTED, never silently dropped -- a
   truncated harvest would draw a partial keycap and look like a rendering
   bug rather than a full buffer. */
static struct X2PromptQuad g_quads[X2_PROMPT_QUADS_MAX];
static unsigned g_count;
static int g_frame_had_any;
static unsigned long g_total, g_overflow, g_frames;

void x2_prompt_quads_reset(void) {
  if (g_frame_had_any)
    g_frames++;
  g_count = 0;
  g_frame_had_any = 0;
}

void x2_prompt_quads_consume(void) { g_count = 0; }

unsigned x2_prompt_quads_available(void) {
  return X2_PROMPT_QUADS_MAX - g_count;
}

int x2_prompt_quads_add(const struct X2PromptQuad *quad) {
  if (!quad)
    return 0;
  g_total++;
  if (g_count == X2_PROMPT_QUADS_MAX) {
    g_overflow++;
    return 0;
  }
  g_quads[g_count++] = *quad;
  g_frame_had_any = 1;
  return 1;
}

const struct X2PromptQuad *x2_prompt_quads(unsigned *count) {
  if (count)
    *count = g_count;
  return g_quads;
}

void x2_prompt_quads_report(void) {
  printf("  Prompt quads: %lu harvested over %lu frame(s) that had any"
         "; %lu dropped past the %u cap\n",
         g_total, g_frames, g_overflow, X2_PROMPT_QUADS_MAX);
  if (!g_total)
    printf("        NONE harvested -- either no label was drawn or the "
           "emitter interception never fired; the port has nothing to "
           "draw either way.\n");
  if (g_overflow)
    printf("        the preflight capacity contract was violated; native "
           "interception refuses to continue after this condition.\n");
}
