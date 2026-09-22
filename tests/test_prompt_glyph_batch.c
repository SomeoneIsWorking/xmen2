/* The shipping Alchemy draw override, with its collaborators observed and
   the real prompt-quad store underneath it. */
#include "prompt_glyph_batch.h"
#include "prompt_glyph_quads.h"
#include "prompt_touch_buttons.h"

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

/* The touch-prompt owner is tested separately; what matters at this boundary
   is that the draw's own primitive count reaches it, because that is how a
   prompt is matched to the draw that places it. */
static unsigned long touch_publishes;
static uint32_t touch_primitives;

void x2_prompt_touch_publish(X2PromptTransform transform, void *owner,
                             uint32_t primitives) {
  (void)transform;
  (void)owner;
  touch_publishes++;
  touch_primitives = primitives;
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

/* Lay out one string the way prompt_glyph_draw.c does: `native` quads of
   ours among `emitted` glyphs, identified by `identity`. */
static void lay_out(uint32_t identity, unsigned emitted, unsigned native,
                    uint16_t codepoint) {
  struct X2PromptQuad quad;
  unsigned i;
  memset(&quad, 0, sizeof quad);
  quad.codepoint = codepoint;
  if (!x2_prompt_quads_begin_run(identity, emitted)) {
    printf("  FAIL  the store refused a run\n");
    failures++;
    return;
  }
  for (i = 0; i < native; i++)
    (void)x2_prompt_quads_add(&quad);
  x2_prompt_quads_end_run();
}

/* The primitive count a draw of `glyphs` whole glyphs declares. */
static uint32_t primitives_for(unsigned glyphs) { return glyphs * 6u - 2u; }

static void reset_case(uint32_t primitives) {
  x2_prompt_quads_reset();
  draw_primitives = primitives;
  primitives_readable = 1;
  transform_ok = 1;
  gpu_ok = 1;
  super_runs_finalizer = 1;
  transform_calls = gpu_calls = super_calls = 0;
  gpu_count = 0;
  gpu_first_codepoint = 0;
  touch_publishes = 0;
  touch_primitives = 0;
  transform_context = 0;
  event_count = 0;
  memset(events, 0, sizeof events);
}

static unsigned pending_runs(void) { return x2_prompt_quads_pending(NULL); }

int main(void) {
  CPU cpu;
  memset(&cpu, 0, sizeof cpu);

  check(native_stubs_registered("libIGGfx.dll", 0x100352d0u),
        "the override registers the RE'd Alchemy drawNonIndexed entry");
  check(native_stubs_registered("libIGGfx.dll", 0x10034e60u),
        "the override registers Alchemy's context-state finalizer");

  reset_case(primitives_for(3));
  x2_prompt_glyph_batch_draw_nonindexed(&cpu);
  check(!strcmp(events, "UD"),
        "a draw with no prompt run is an untouched super-call");
  check(transform_calls == 0 && gpu_calls == 0,
        "an empty draw does not snapshot or submit");
  check(touch_publishes == 1 && touch_primitives == primitives_for(3),
        "every finalized draw offers its own primitive count to the touch "
        "prompts, whether or not it carries prompt art");

  /* A footer: three elements laid out, THEN drawn one per draw. Each draw
     must take the element it submits and leave the others for theirs. */
  reset_case(primitives_for(16));
  lay_out(0xa1u, 9u, 1u, 0x81u);  /* "<B> Back"        */
  lay_out(0xa2u, 16u, 4u, 0x90u); /* "[Up] [Down] Scroll" */
  lay_out(0xa3u, 12u, 2u, 0x91u); /* "[J][L] Rotate"   */
  cpu.reg[kX86pEcx] = 0x12345678u;
  x2_prompt_glyph_batch_draw_nonindexed(&cpu);
  check(!strcmp(events, "UTGD"),
        "prompt art draws after finalization and before the stock batch");
  check(gpu_count == 4u && gpu_first_codepoint == 0x90u,
        "the draw takes exactly the run its primitive count submits");
  check(transform_context == 0x12345678u,
        "the matrix lookup stays keyed to the finalizer's input context");
  check(pending_runs() == 2u,
        "the other elements' runs wait for their own draws");

  event_count = 0;
  memset(events, 0, sizeof events);
  draw_primitives = primitives_for(9);
  x2_prompt_glyph_batch_draw_nonindexed(&cpu);
  check(gpu_count == 1u && gpu_first_codepoint == 0x81u && pending_runs() == 1u,
        "a later draw in the same pass places its own element");

  draw_primitives = primitives_for(5);
  gpu_calls = 0;
  x2_prompt_glyph_batch_draw_nonindexed(&cpu);
  check(gpu_calls == 0 && pending_runs() == 1u,
        "a draw of text that is no pending string takes nothing");

  draw_primitives = 7u; /* not a whole number of glyphs */
  x2_prompt_glyph_batch_draw_nonindexed(&cpu);
  check(gpu_calls == 0 && pending_runs() == 1u,
        "a draw that is not whole glyphs takes nothing");

  primitives_readable = 0;
  x2_prompt_glyph_batch_draw_nonindexed(&cpu);
  check(gpu_calls == 0 && pending_runs() == 1u,
        "an unreadable primitive count places nothing rather than the "
        "wrong string");

  /* A conversation: name, line and response button laid out in that order
     and drawn together as one window, then the response text alone. */
  reset_case(primitives_for(7 + 59 + 1));
  lay_out(0xb1u, 7u, 0u, 0u);    /* "Cyclops"            */
  lay_out(0xb2u, 59u, 0u, 0u);   /* the spoken line      */
  lay_out(0xb3u, 1u, 1u, 0x80u); /* "<A>"               */
  lay_out(0xb4u, 11u, 0u, 0u);   /* "continue..."        */
  x2_prompt_glyph_batch_draw_nonindexed(&cpu);
  check(gpu_count == 1u && gpu_first_codepoint == 0x80u && pending_runs() == 0u,
        "a draw of adjacent strings takes the prompt inside its window");
  gpu_calls = 0;
  draw_primitives = primitives_for(1);
  x2_prompt_glyph_batch_draw_nonindexed(&cpu);
  check(gpu_calls == 0,
        "a one-glyph draw cannot take a button already placed by its own "
        "window");

  reset_case(primitives_for(9));
  lay_out(0xa1u, 9u, 1u, 0x81u);
  lay_out(0xa1u, 9u, 1u, 0x82u);
  check(pending_runs() == 1u,
        "a re-measurement of the same string replaces the pending run");
  x2_prompt_glyph_batch_draw_nonindexed(&cpu);
  check(gpu_first_codepoint == 0x82u, "the draw places the latest measurement");

  reset_case(primitives_for(9));
  lay_out(0xa1u, 9u, 3u, 0x81u);
  transform_ok = 0;
  x2_prompt_glyph_batch_draw_nonindexed(&cpu);
  check(!strcmp(events, "UTD") && gpu_calls == 0 && pending_runs() == 0u,
        "an unavailable engine transform refuses and discards its own run");

  reset_case(primitives_for(9));
  lay_out(0xa1u, 9u, 3u, 0x81u);
  gpu_ok = 0;
  x2_prompt_glyph_batch_draw_nonindexed(&cpu);
  check(!strcmp(events, "UTGD") && pending_runs() == 0u,
        "a GPU refusal discards its run instead of leaking it to a later "
        "draw");

  reset_case(primitives_for(9));
  lay_out(0xa1u, 9u, 3u, 0x81u);
  x2_prompt_glyph_batch_update_context_state(&cpu);
  check(!strcmp(events, "U") && transform_calls == 0 && gpu_calls == 0 &&
            pending_runs() == 1u,
        "the finalizer alone never mistakes an indexed draw for text");

  reset_case(primitives_for(9));
  lay_out(0xa1u, 9u, 3u, 0x81u);
  super_runs_finalizer = 0;
  x2_prompt_glyph_batch_draw_nonindexed(&cpu);
  check(!strcmp(events, "D") && gpu_calls == 0 && touch_publishes == 0,
        "a draw that never finalized places nothing and offers nothing");

  x2_prompt_quads_reset();
  check(pending_runs() == 0u, "a new frame drops every undrawn run");

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
/* The draw's one modelled argument is its primitive count at [ESP+8]. */
int guest_memory_try_read(uint32_t address, void *destination, size_t size) {
  if (!primitives_readable || address != 8u || size != sizeof draw_primitives)
    return 0;
  memcpy(destination, &draw_primitives, size);
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
