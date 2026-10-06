/* The shipping Alchemy draw override, with its collaborators observed and
   the real prompt-quad store underneath it. */
#include "prompt_glyph_batch.h"
#include "prompt_glyph_quads.h"

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

int native_stubs_registered(const char *module, uint32_t linked_ep);

static int failures;
static int transform_ok = 1;
static int gpu_ok = 1;
static int super_runs_finalizer = 1;
static int primitives_readable = 1;
static uint32_t draw_primitives;
static unsigned transform_calls, gpu_calls, super_calls;
static unsigned gpu_count;
static uint16_t gpu_first_codepoint;
static uint32_t transform_context;
static unsigned event_count;
static char events[12];

static void note(char event) {
  if (event_count < sizeof events)
    events[event_count++] = event;
}

static void check(int condition, const char *what) {
  if (!condition) {
    printf("  FAIL  %s (events \"%s\")\n", what, events);
    failures++;
  } else {
    printf("  pass  %s\n", what);
  }
}

int x2_ui_transform_current(uint32_t context, float mvp[16]) {
  unsigned i;
  note('T');
  transform_calls++;
  transform_context = context;
  if (!transform_ok)
    return 0;
  for (i = 0; i < 16; i++)
    mvp[i] = (float)(i + 1u);
  return 1;
}

int gpu_prompt_glyphs_render(const struct X2PromptQuad *quads, unsigned count,
                             const float mvp[16]) {
  unsigned i;
  note('G');
  gpu_calls++;
  gpu_count = count;
  gpu_first_codepoint = count ? quads[0].codepoint : 0u;
  for (i = 0; i < 16; i++)
    if (mvp[i] != (float)(i + 1u)) {
      failures++;
      printf("  FAIL  GPU received a matrix other than the engine "
             "snapshot\n");
      break;
    }
  return gpu_ok;
}

static void guest_body_100352d0(CPU *C) {
  super_calls++;
  if (super_runs_finalizer)
    x2_prompt_glyph_batch_update_context_state(C);
  note('D'); /* The original body submits to D3D after its finalizer. */
}

static void guest_body_10034e60(CPU *C) {
  note('U');
  /* ECX is caller-saved. The wrapper must retain the visual-context key it
     received rather than asking the transformed CPU state after super. */
  C->reg[kX86pEcx] = 0xeeeeeeeeu;
}

/* The vertex array the drawing context submits from, and the one other
   text batches write into. */
#define ARRAY_TEXT 0x01f29db8u
#define ARRAY_OTHER 0x01f40000u
#define CONTEXT 0x12345678u
static uint32_t draw_start;
static uint32_t context_array = ARRAY_TEXT;

/* Retain `native` quads of ours for a glyph the engine wrote at `vertex` of
   `array`, the way prompt_glyph_draw.c does. */
static void put(uint32_t array, uint32_t vertex, unsigned native,
                uint16_t codepoint) {
  struct X2PromptQuad quads[8];
  const struct X2PromptVertexKey key = {array, vertex};
  unsigned i;
  memset(quads, 0, sizeof quads);
  for (i = 0; i < native; i++)
    quads[i].codepoint = codepoint;
  if (!x2_prompt_quads_put(key, quads, native)) {
    printf("  FAIL  the store refused a quad\n");
    failures++;
  }
}

/* The primitive count a strip of `glyphs` whole glyphs declares, and the
   first vertex of glyph `index` in the batch. */
static uint32_t primitives_for(unsigned glyphs) { return glyphs * 6u - 2u; }
static uint32_t glyph(unsigned index) { return index * 6u; }

static void draw(uint32_t start, unsigned glyphs, CPU *cpu) {
  draw_start = start;
  draw_primitives = primitives_for(glyphs);
  cpu->reg[kX86pEcx] = CONTEXT;
  x2_prompt_glyph_batch_draw_nonindexed(cpu);
}

static void reset_case(uint32_t primitives) {
  x2_prompt_quads_reset();
  draw_primitives = primitives;
  draw_start = 0;
  context_array = ARRAY_TEXT;
  primitives_readable = 1;
  transform_ok = 1;
  gpu_ok = 1;
  super_runs_finalizer = 1;
  transform_calls = gpu_calls = super_calls = 0;
  gpu_count = 0;
  gpu_first_codepoint = 0;
  transform_context = 0;
  event_count = 0;
  memset(events, 0, sizeof events);
}

static unsigned pending(void) { return x2_prompt_quads_pending(); }

int main(void) {
  CPU cpu;
  memset(&cpu, 0, sizeof cpu);

  check(native_stubs_registered("libIGGfx.dll", 0x100352d0u),
        "the override registers the RE'd Alchemy drawNonIndexed entry");
  check(native_stubs_registered("libIGGfx.dll", 0x10034e60u),
        "the override registers Alchemy's context-state finalizer");

  reset_case(0);
  draw(glyph(0), 3u, &cpu);
  check(!strcmp(events, "UD"),
        "a draw with no prompt run is an untouched super-call");
  check(transform_calls == 0 && gpu_calls == 0,
        "an empty draw does not snapshot or submit");

  /* A footer: three elements laid out one after another in the batch, THEN
     drawn one per draw. Each draw takes the element it submits and leaves
     the others for theirs. */
  reset_case(0);
  put(ARRAY_TEXT, glyph(0), 1u, 0x81u);  /* "<B> Back", glyphs 0..8   */
  put(ARRAY_TEXT, glyph(9), 4u, 0x90u);  /* "[Up] [Down] Scroll", 9.. */
  put(ARRAY_TEXT, glyph(25), 2u, 0x91u); /* "[J][L] Rotate", 25..36   */
  draw(glyph(9), 16u, &cpu);
  check(!strcmp(events, "UTGD"),
        "prompt art draws after finalization and before the stock batch");
  check(gpu_count == 4u && gpu_first_codepoint == 0x90u,
        "the draw takes exactly the quads in the vertex range it submits");
  check(transform_context == CONTEXT,
        "the matrix lookup stays keyed to the finalizer's input context");
  check(pending() == 3u, "the other elements wait for their own draws");

  gpu_calls = 0;
  draw(glyph(0), 9u, &cpu);
  check(gpu_calls == 1u && gpu_count == 1u && gpu_first_codepoint == 0x81u &&
            pending() == 2u,
        "a later draw in the same pass places its own element");

  gpu_calls = 0;
  draw(glyph(37), 9u, &cpu);
  check(gpu_calls == 0 && pending() == 2u,
        "a draw of text past every prompt takes nothing");

  /* The range is half-open: a glyph that starts where the draw ends is the
     next draw's. */
  draw(glyph(16), 9u, &cpu);
  check(gpu_calls == 0 && pending() == 2u,
        "a glyph starting one past the draw's last vertex stays pending");

  context_array = ARRAY_OTHER;
  draw(glyph(25), 12u, &cpu);
  check(gpu_calls == 0 && pending() == 2u,
        "the same range of another vertex array takes nothing");
  context_array = ARRAY_TEXT;

  primitives_readable = 0;
  draw(glyph(25), 12u, &cpu);
  check(gpu_calls == 0 && pending() == 2u,
        "an unreadable draw argument places nothing rather than the wrong "
        "string");
  primitives_readable = 1;

  /* A conversation: name, line and response button laid out in that order
     and drawn together as one range. */
  reset_case(0);
  put(ARRAY_TEXT, glyph(66), 1u, 0x80u); /* "<A>" after 7 + 59 glyphs */
  draw(glyph(0), 7u + 59u + 1u, &cpu);
  check(gpu_count == 1u && gpu_first_codepoint == 0x80u && pending() == 0u,
        "a draw of adjacent strings takes the prompt inside its range");

  /* The pause menu (issue #184). The row "Blink Portal (down)" is 17 plain
     glyphs, and the footer's "[Esc] Ready" (10 glyphs) and "Players" (7)
     after it are 17 too. A glyph-count matcher gave the row the footer's key
     ("Blink [POR]tal"); the row's own range holds no quad of ours. */
  reset_case(0);
  put(ARRAY_TEXT, glyph(17 + 4), 5u, 0x9au); /* the key's right edge */
  draw(glyph(0), 17u, &cpu);
  check(gpu_calls == 0 && pending() == 5u,
        "a plain row whose glyph count equals the footer's places no key");
  draw(glyph(17), 17u, &cpu);
  check(gpu_calls == 1u && gpu_count == 5u && gpu_first_codepoint == 0x9au,
        "the footer's key is placed by the draw that submits it");

  /* The engine writing a glyph over a slot whose quad was never drawn: the
     vertices are the latest string's, and so is the art. */
  reset_case(0);
  put(ARRAY_TEXT, glyph(3), 1u, 0x81u);
  put(ARRAY_TEXT, glyph(3), 1u, 0x82u);
  check(pending() == 1u, "a glyph written over replaces its pending quad");
  draw(glyph(0), 9u, &cpu);
  check(gpu_first_codepoint == 0x82u, "the draw places the latest glyph");

  reset_case(0);
  put(ARRAY_TEXT, glyph(0), 3u, 0x81u);
  transform_ok = 0;
  draw(glyph(0), 9u, &cpu);
  check(!strcmp(events, "UTD") && gpu_calls == 0 && pending() == 0u,
        "an unavailable engine transform refuses and discards its quads");

  reset_case(0);
  put(ARRAY_TEXT, glyph(0), 3u, 0x81u);
  gpu_ok = 0;
  draw(glyph(0), 9u, &cpu);
  check(!strcmp(events, "UTGD") && pending() == 0u,
        "a GPU refusal discards its quads instead of leaking them to a "
        "later draw");

  reset_case(0);
  put(ARRAY_TEXT, glyph(0), 3u, 0x81u);
  cpu.reg[kX86pEcx] = CONTEXT;
  x2_prompt_glyph_batch_update_context_state(&cpu);
  check(!strcmp(events, "U") && transform_calls == 0 && gpu_calls == 0 &&
            pending() == 3u,
        "the finalizer alone never mistakes an indexed draw for text");

  reset_case(0);
  put(ARRAY_TEXT, glyph(0), 3u, 0x81u);
  super_runs_finalizer = 0;
  draw(glyph(0), 9u, &cpu);
  check(!strcmp(events, "D") && gpu_calls == 0,
        "a draw that never finalized places nothing");

  x2_prompt_quads_reset();
  check(pending() == 0u, "a new frame drops every undrawn quad");

  printf("  the report reads:\n");
  x2_prompt_glyph_batch_report();
  x2_prompt_quads_report();
  printf("\ntest_prompt_glyph_batch: %d failure(s)\n", failures);
  return failures ? 1 : 0;
}

/*
 * The retail bodies these tests super-call into. Production reaches them
 * through x86_guest_body, so the test models the same seam rather than a
 * symbol per function -- and an entry point this test does not model is a
 * FAILURE that names itself, never a silent return.
 */
/* The draw's modelled arguments are its primitive count at [ESP+8] and its
   start vertex at [ESP+0xc]; the context's current vertex array is at
   VC+0x1f0. Any other read is not modelled and fails. */
int guest_memory_try_read(uint32_t address, void *destination, size_t size) {
  const uint32_t *value = NULL;
  if (size != sizeof(uint32_t))
    return 0;
  if (address == 8u && primitives_readable)
    value = &draw_primitives;
  else if (address == 0xcu && primitives_readable)
    value = &draw_start;
  else if (address == CONTEXT + 0x1f0u)
    value = &context_array;
  if (!value)
    return 0;
  memcpy(destination, value, size);
  return 1;
}

void x86_guest_body(CPU *C, const char *module, uint32_t linked_ep) {
  if (linked_ep == 0x100352d0u && !strcmp(module, "libIGGfx.dll")) {
    guest_body_100352d0(C);
    return;
  }
  if (linked_ep == 0x10034e60u && !strcmp(module, "libIGGfx.dll")) {
    guest_body_10034e60(C);
    return;
  }
  fprintf(stderr,
          "%s: x86_guest_body(%s, 0x%08x) is not modelled by this test.\n",
          "test_prompt_glyph_batch.c", module, linked_ep);
  abort();
}
