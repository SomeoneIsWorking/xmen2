/* The shipping Alchemy draw override, with its collaborators observed. */
#include "prompt_glyph_batch.h"
#include "prompt_glyph_quads.h"

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

int native_stubs_registered(const char *module, uint32_t linked_ep);

static int failures;
static unsigned pending;
static int transform_ok = 1;
static int gpu_ok = 1;
static int super_runs_finalizer = 1;
static unsigned transform_calls, gpu_calls, consume_calls, super_calls;
static uint32_t transform_context;
static unsigned event_count;
static char events[12];

static void note(char event) {
  if (event_count < sizeof events)
    events[event_count++] = event;
}

static void check(int condition, const char *what) {
  if (!condition) {
    printf("  FAIL  %s\n", what);
    failures++;
  } else {
    printf("  pass  %s\n", what);
  }
}

const struct X2PromptQuad *x2_prompt_quads(unsigned *count) {
  static struct X2PromptQuad unused;
  if (count)
    *count = pending;
  return &unused;
}

void x2_prompt_quads_consume(void) {
  note('C');
  consume_calls++;
  pending = 0;
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

int gpu_prompt_glyphs_render(const float mvp[16]) {
  unsigned i;
  note('G');
  gpu_calls++;
  for (i = 0; i < 16; i++)
    if (mvp[i] != (float)(i + 1u)) {
      failures++;
      printf("  FAIL  GPU received a matrix other than the engine "
             "snapshot\n");
      break;
    }
  if (gpu_ok)
    pending = 0;
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
  C->ecx = 0xeeeeeeeeu;
}

static void reset_case(unsigned count) {
  pending = count;
  transform_ok = 1;
  gpu_ok = 1;
  super_runs_finalizer = 1;
  transform_calls = gpu_calls = consume_calls = super_calls = 0;
  transform_context = 0;
  event_count = 0;
  memset(events, 0, sizeof events);
}

int main(void) {
  CPU cpu;
  memset(&cpu, 0, sizeof cpu);

  check(native_stubs_registered("libIGGfx.dll", 0x100352d0u),
        "the override registers the RE'd Alchemy drawNonIndexed entry");
  check(native_stubs_registered("libIGGfx.dll", 0x10034e60u),
        "the override registers Alchemy's context-state finalizer");

  reset_case(0);
  x2_prompt_glyph_batch_draw_nonindexed(&cpu);
  check(!strcmp(events, "UD"),
        "a draw with no prompt batch is an untouched super-call");
  check(transform_calls == 0 && gpu_calls == 0 && consume_calls == 0,
        "an empty draw does not snapshot, submit, or consume");

  reset_case(7);
  cpu.ecx = 0x12345678u;
  x2_prompt_glyph_batch_draw_nonindexed(&cpu);
  check(!strcmp(events, "UTGD"),
        "prompt art draws after finalization and before the stock batch");
  check(transform_calls == 1 && gpu_calls == 1 && super_calls == 1,
        "the engine matrix, GPU path, and super each run once");
  check(transform_context == 0x12345678u,
        "the matrix lookup stays keyed to the finalizer's input context");

  reset_case(3);
  transform_ok = 0;
  x2_prompt_glyph_batch_draw_nonindexed(&cpu);
  check(!strcmp(events, "UTCD"),
        "an unavailable engine transform refuses and discards this batch");
  check(gpu_calls == 0 && consume_calls == 1 && super_calls == 1,
        "transform refusal never submits and still super-calls");

  reset_case(5);
  gpu_ok = 0;
  x2_prompt_glyph_batch_draw_nonindexed(&cpu);
  check(!strcmp(events, "UTGCD"),
        "a GPU refusal cannot leak prompt quads into a later draw");
  check(consume_calls == 1 && super_calls == 1,
        "GPU refusal discards once and still super-calls");

  reset_case(4);
  x2_prompt_glyph_batch_update_context_state(&cpu);
  check(!strcmp(events, "U"),
        "the finalizer alone never mistakes an indexed draw for text");
  check(transform_calls == 0 && gpu_calls == 0 && consume_calls == 0,
        "only the bracketed non-indexed batch can consume prompt quads");

  reset_case(6);
  super_runs_finalizer = 0;
  x2_prompt_glyph_batch_draw_nonindexed(&cpu);
  check(!strcmp(events, "DC"),
        "a draw that returns without finalization discards its orphaned batch");
  check(pending == 0 && consume_calls == 1 && transform_calls == 0 &&
            gpu_calls == 0 && super_calls == 1,
        "unfinalized quads cannot leak into a later unrelated draw");

  x2_prompt_glyph_batch_update_context_state(&cpu);
  check(!strcmp(events, "DCU") && consume_calls == 1,
        "the later unrelated finalizer finds no leaked prompt batch");

  printf("  the report reads:\n");
  x2_prompt_glyph_batch_report();
  printf("\ntest_prompt_glyph_batch: %d failure(s)\n", failures);
  return failures ? 1 : 0;
}

/*
 * The retail bodies these tests super-call into. Production reaches them
 * through x86_guest_body, so the test models the same seam rather than a
 * symbol per function -- and an entry point this test does not model is a
 * FAILURE that names itself, never a silent return.
 */
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
