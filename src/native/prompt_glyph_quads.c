/*
 * Where the port's prompt glyphs belong on screen, decided by the ENGINE.
 *
 * The alternative was for the port to lay the label out itself, which the
 * measurements ruled out: the engine's text run is right-anchored, so the
 * origin depends on the string's own content (C269 and docs/RE/text.md).
 * Instead the stock loop lays everything out with the port's published
 * metrics, and its own quad for each of our codepoints is intercepted at
 * FUN_005ee400 -- recorded here, and never emitted, so nothing of ours is
 * drawn out of the game's font atlas.
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
#define MAX_QUADS 512u

static struct X2PromptQuad g_quads[MAX_QUADS];
static unsigned g_count;
static unsigned long g_total, g_overflow, g_frames;

void x2_prompt_quads_reset(void)
{
    if (g_count) g_frames++;
    g_count = 0;
}

void x2_prompt_quads_add(const struct X2PromptQuad *quad)
{
    if (!quad) return;
    g_total++;
    if (g_count == MAX_QUADS) { g_overflow++; return; }
    g_quads[g_count++] = *quad;
}

const struct X2PromptQuad *x2_prompt_quads(unsigned *count)
{
    if (count) *count = g_count;
    return g_quads;
}

void x2_prompt_quads_report(void)
{
    printf("  Prompt quads: %lu harvested over %lu frame(s) that had any"
           "; %lu dropped past the %u cap\n",
           g_total, g_frames, g_overflow, MAX_QUADS);
    if (!g_total)
        printf("        NONE harvested -- either no label was drawn or the "
               "emitter interception never fired; the port has nothing to "
               "draw either way.\n");
    if (g_overflow)
        printf("        the cap was hit, so some prompts drew incomplete.\n");
}
