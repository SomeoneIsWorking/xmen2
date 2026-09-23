/*
 * The prompt-glyph string transaction, run against both classes.
 *
 * FUN_005ee780 owns the whole-string preconditions; FUN_005ee400 harvests a
 * private glyph and then super-calls it as a zero-area retail rectangle. The
 * retained retail call is essential: a one-glyph string otherwise produces no
 * stock vertex, hence no Alchemy drawNonIndexed finalizer for the native art.
 *
 * It drives x2_override_005ee780 itself rather than only the pure helper: the
 * shipping path is the override, and a test of helpers beside it would leave
 * the guest stack ABI, batch-colour pre-read and collapse-plus-super path
 * untested.
 */
#include "guest_memory.h"
#include "keycap_labels.h"
#include "keycap_run.h"
#include "pad_glyph_codes.h"
#include "prompt_glyph_atlas.h"
#include "prompt_glyph_draw.h"
#include "prompt_glyph_quads.h"
#include "prompt_string_census.h"
#include "runtime_cvars.h"
#include "x86rt.h"
#include "x86rt_native.h"

#include <stdint.h>
#include <stdlib.h>

/*
 * The touch-prompt rewrite is a SEPARATE owner with its own test
 * (test_prompt_touch_buttons). This test is about the glyph loop's own
 * behaviour, so it answers "not a touch prompt" to every string: begin()
 * returning 0 is the same answer the shipping owner gives whenever touch
 * play is not active, which is the state this test is measuring.
 */
unsigned x2_prompt_touch_begin(uint32_t string_guest, unsigned length) {
  (void)string_guest;
  (void)length;
  return 0;
}

int x2_prompt_touch_glyph(unsigned emit_index, float *x0, float *y0, float *x1,
                          float *y1) {
  (void)emit_index;
  (void)x0;
  (void)y0;
  (void)x1;
  (void)y1;
  abort(); /* Never armed: begin() above never claims a string. */
}

void x2_prompt_touch_end(void) { abort(); }

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>

int native_stubs_registered(const char *module, uint32_t linked_ep);

void x2_override_005ee780(CPU *C);
void x2_override_005ee400(CPU *C);

/* The strings live at real guest addresses. On hosts with an identity-mapped
   guest they are low host addresses too; Apple Silicon translates them into
   the reserved 4 GB guest arena. Nothing about the walk is simulated. */
#define GUEST_PAGE 0x70000000u

/* Guest-memory peek used by the shipping interception. The test maps a real
   page at a fixed low address, so string and batch-colour reads are real
   rather than simulated: an address inside succeeds and anything else fails,
   which is exactly the runtime peek contract. */

static int failures;
static uint32_t g_next;

static void fail(const char *what) {
  printf("  FAIL  %s\n", what);
  failures++;
}

static void ok(const char *what) { printf("  pass  %s\n", what); }

/* Write a NUL-terminated wide string into guest memory, return its address. */
static uint32_t guest_wide(const uint16_t *codes, unsigned n) {
  uint32_t at = g_next;
  unsigned i;
  for (i = 0; i < n; i++)
    *(uint16_t *)guest_memory_pointer(at + i * 2u) = codes[i];
  *(uint16_t *)guest_memory_pointer(at + n * 2u) = 0;
  g_next += (n + 1) * 2u;
  return at;
}

/* How many times each retail body was entered. Whole strings always super;
   native emitter calls super with collapsed geometry. */
static unsigned long g_super_calls;
static unsigned long g_emitter_calls;
static uint32_t g_batch;
static float g_emitted_rects[1024][4];

static uint32_t float_bits(float value) {
  uint32_t bits;
  memcpy(&bits, &value, sizeof bits);
  return bits;
}

static float bits_float(uint32_t bits) {
  float value;
  memcpy(&value, &bits, sizeof value);
  return value;
}

/* The real emitter owns RET 0x20. Recording its received rectangle proves the
   override did not imitate that ABI by manually moving ESP. */
static void guest_body_005ee400(CPU *C) {
  unsigned i;
  if (g_emitter_calls < 1024u)
    for (i = 0; i < 4u; i++)
      g_emitted_rects[g_emitter_calls][i] =
          bits_float(RD32(C->reg[kX86pEsp] + (i + 1u) * 4u));
  g_emitter_calls++;
  C->reg[kX86pEsp] += 4u + 0x20u;
}

/* A narrow model of the retail loop: one emitter call for each drawable
   wchar, with distinct non-zero rectangles. The production override, not a
   test copy, decides which of those calls is harvested and collapsed. */
static void guest_body_005ee780(CPU *C) {
  uint32_t s = RD32(C->reg[kX86pEsp] + 4u);
  unsigned i;
  g_super_calls++;
  for (i = 0; i < 512u; i++) {
    CPU emitter = {0};
    uint16_t c = RD16(s + i * 2u);
    uint32_t stack;
    float x0, y0, x1, y1;
    if (!c)
      break;
    if (c >= 256u || c == ' ' || c == '\t')
      continue;
    stack = GUEST_PAGE + 0x800u;
    x0 = 10.0f + (float)i * 20.0f;
    y0 = 20.0f;
    x1 = x0 + 18.0f;
    y1 = 38.0f;
    WR32(stack, 0xcafef00du);
    WR32(stack + 4u, float_bits(x0));
    WR32(stack + 8u, float_bits(y0));
    WR32(stack + 12u, float_bits(x1));
    WR32(stack + 16u, float_bits(y1));
    WR32(stack + 20u, float_bits(0.1f));
    WR32(stack + 24u, float_bits(0.2f));
    WR32(stack + 28u, float_bits(0.3f));
    WR32(stack + 32u, float_bits(0.4f));
    emitter.reg[kX86pEsp] = stack;
    /* Deliberately not the batch: colour must have been pre-read from
       FUN_005ee780 arg2+8 before this loop was armed. */
    emitter.reg[kX86pEcx] = 0x12345678u;
    x2_override_005ee400(&emitter);
    if (emitter.reg[kX86pEsp] != stack + 0x24u)
      fail("the retail emitter did not own its RET 0x20 stack effect");
  }
  C->reg[kX86pEsp] += 4u + 0x1cu;
}

/* Bind the argument the way the GUEST does, not the way the override happens
   to read it. FUN_005ee780 takes the wide string as its first STACK
   argument, so the test builds a real guest stack -- return address at ESP,
   string pointer at ESP+4. Handing it over in a register instead is what let
   the override read C->reg[kX86pEdx] for a whole investigation while every run
   it measured reported a zero it could not have contradicted. */
#define GUEST_STACK_TOP (GUEST_PAGE + 0xf00u)
static uint32_t g_stack;

static void call_glyph_loop(CPU *cpu, uint32_t wide_string) {
  memset(cpu, 0, sizeof *cpu);
  g_stack -= 32u;
  memset(guest_memory_pointer(g_stack), 0, 32u);
  WR32(g_stack, 0xdeadbeefu); /* return address */
  WR32(g_stack + 4u, wide_string);
  WR32(g_stack + 8u, g_batch);
  cpu->reg[kX86pEsp] = g_stack;
  x2_override_005ee780(cpu);
}

static int rect_collapsed(unsigned index) {
  return g_emitted_rects[index][0] == g_emitted_rects[index][2] &&
         g_emitted_rects[index][1] == g_emitted_rects[index][3];
}

static int rect_has_area(unsigned index) {
  return g_emitted_rects[index][0] != g_emitted_rects[index][2] &&
         g_emitted_rects[index][1] != g_emitted_rects[index][3];
}

static void expect(const uint16_t *codes, unsigned n, int want,
                   const char *what) {
  uint32_t at = guest_wide(codes, n);
  int got = x2_string_has_prompt_glyph(at, 512u);
  if (got != want) {
    printf("  FAIL  %s: classifier said %d, expected %d\n", what, got, want);
    failures++;
  } else {
    ok(what);
  }
}

int main(void) {
  /* The feature gate caches on first read, so it is set before anything calls
     into the subsystem. Without it the override is inert by design and the
     whole test would pass while measuring nothing. */
  setenv("X2_PROMPT_GLYPHS", "1", 1);
  x2_runtime_config_init(0, NULL);

  if (guest_memory_init() != 0 ||
      guest_memory_map_fixed(GUEST_PAGE, 0x1000, PROT_READ | PROT_WRITE) != 0) {
    fprintf(stderr,
            "test_prompt_glyph_draw: could not map guest page "
            "0x%08x in the guest address space.\n",
            GUEST_PAGE);
    return 1;
  }
  g_next = GUEST_PAGE;
  g_stack = GUEST_STACK_TOP;
  g_batch = GUEST_PAGE + 0x780u;
  WR32(g_batch + 8u, 0x7f2468acu);

  if (!native_stubs_registered("XMen2.exe", 0x005ee780))
    fail("the constructor did not register the glyph-loop override");
  else
    ok("the constructor registers XMen2.exe 0x005ee780");

  {
    /* THE POSITIVE the real run never produced: a composed keycap label,
       exactly the shape prompt_labels.c builds -- cap left, the binding
       letter, cap right. */
    static const uint16_t label[] = {
        X2_KEYCAP_GLYPH_LEFT,
        'E',
        X2_KEYCAP_GLYPH_RIGHT,
    };
    expect(label, 3, 1, "a composed keycap label is DETECTED");
  }
  {
    static const uint16_t pad[] = {
        'P', 'r', 'e', 's', 's', ' ', X2_PAD_GLYPH_FACE_A};
    expect(pad, 7, 1, "a pad face-button label is DETECTED");
  }
  {
    static const uint16_t lo[] = {X2_PROMPT_GLYPH_FIRST};
    static const uint16_t hi[] = {X2_PROMPT_GLYPH_LAST};
    expect(lo, 1, 1, "the first codepoint of the range is detected");
    expect(hi, 1, 1, "the last codepoint of the range is detected");
  }
  {
    /* And the NEGATIVES, either side of the range and from the real run.
       0x00bd is the copyright glyph on the legal screen, 0x01f2/0x9d28 are
       the engine's own above-256 control words -- all three arrived at the
       glyph loop in the tutorial run and none is ours. */
    static const uint16_t below[] = {X2_PROMPT_GLYPH_FIRST - 1};
    static const uint16_t above[] = {X2_PROMPT_GLYPH_LAST + 1};
    static const uint16_t plain[] = {'N', 'E', 'W', ' ', 'G', 'A', 'M', 'E'};
    static const uint16_t seen[] = {0x9d28, 0x01f2, 0x08e2};
    static const uint16_t legal[] = {'C', 'o', 'p', 'y', 'r',   'i',
                                     'g', 'h', 't', ' ', 0x00bd};
    expect(below, 1, 0, "0x7f, just below the range, is not ours");
    expect(above, 1, 0, "0x94, just above the range, is not ours");
    expect(plain, 8, 0, "ordinary menu text is not ours");
    expect(seen, 3, 0, "the engine's above-256 control words are not ours");
    expect(legal, 11, 0, "the legal screen's copyright glyph is not ours");
  }
  {
    /* A NUL inside the buffer ends the string: bytes past it belong to
       whatever the engine put there and must not be classified. */
    uint32_t at = g_next;
    *(uint16_t *)guest_memory_pointer(at + 0) = 'A';
    *(uint16_t *)guest_memory_pointer(at + 2) = 0;
    *(uint16_t *)guest_memory_pointer(at + 4) = X2_PAD_GLYPH_FACE_A;
    *(uint16_t *)guest_memory_pointer(at + 6) = 0;
    g_next += 8;
    if (x2_string_has_prompt_glyph(at, 512u))
      fail("the walk read past the string's own NUL");
    else
      ok("the walk stops at the string's NUL");
  }
  if (x2_string_has_prompt_glyph(0, 512u))
    fail("a null string pointer was classified as carrying a prompt");
  else
    ok("a null string pointer is not classified as a prompt");

  {
    /* The shipping path reads the first stack argument and super-calls
       the complete retail string in both cases. */
    static const uint16_t label[] = {
        X2_KEYCAP_GLYPH_LEFT,
        'E',
        X2_KEYCAP_GLYPH_RIGHT,
    };
    static const uint16_t plain[] = {'O', 'K'};
    CPU cpu;
    unsigned long before = g_super_calls;

    call_glyph_loop(&cpu, guest_wide(label, 3));
    call_glyph_loop(&cpu, guest_wide(plain, 2));

    if (g_super_calls != before + 2)
      fail("the override did not super-call for every string");
    else
      ok("the override super-calls for every string, prompt or not");
  }

  {
    /* The root-cause regression: even a string made of ONE native glyph
       must still run one retail emitter call, but with zero stock area.
       The native quad retains its original rectangle and the colour read
       from glyph-loop arg2+8, not the emitter's unrelated ECX. */
    static const uint16_t one[] = {X2_PAD_GLYPH_FACE_A};
    CPU cpu;
    struct X2PromptQuad quads[X2_PROMPT_QUADS_MAX];
    unsigned count;
    unsigned long emit_before = g_emitter_calls;
    uint32_t entry;

    x2_prompt_quads_reset();
    entry = g_stack - 32u;
    call_glyph_loop(&cpu, guest_wide(one, 1));
    /* One emitted glyph: the draw that submits it declares 6*1-2. */
    count = x2_prompt_quads_take_run(x2_prompt_draw_glyphs(4u), quads);
    if (cpu.reg[kX86pEsp] != entry + 32u)
      fail("the retail glyph loop did not own its RET 0x1c ABI");
    else
      ok("the retail glyph loop owns its RET 0x1c ABI");
    if (g_emitter_calls != emit_before + 1u ||
        !rect_collapsed((unsigned)emit_before))
      fail("a pure one-glyph prompt did not collapse and super-call");
    else
      ok("a pure one-glyph prompt retains one collapsed retail vertex path");
    if (count != 1u || quads[0].x0 != 10.0f || quads[0].x1 != 28.0f ||
        quads[0].color != 0x7f2468acu)
      fail("the one-glyph native quad lost geometry or pre-read color");
    else
      ok("the one-glyph native quad keeps geometry and arg2+8 color");
  }

  {
    /* The key the player sees: "ESC Back". Every stock rectangle inside the
       key -- both edges and the name's letters -- collapses, the key is drawn
       whole from shared art over the span the edges reserved, and the words
       keep their stock rectangles. */
    static const uint16_t esc_back[] = {X2_KEYCAP_GLYPH_LEFT,
                                        'E',
                                        'S',
                                        'C',
                                        X2_KEYCAP_GLYPH_RIGHT,
                                        ' ',
                                        'B',
                                        'a',
                                        'c',
                                        'k'};
    static const uint16_t esc[] = {'E', 'S', 'C'};
    const struct x2_keycap_art *art = x2_keycap_label_art(esc, 3u);
    struct X2PromptQuad quads[X2_PROMPT_QUADS_MAX];
    CPU cpu;
    unsigned i, count, collapsed = 0, stock = 0;
    unsigned long emit_before = g_emitter_calls;

    x2_prompt_quads_reset();
    call_glyph_loop(&cpu, guest_wide(esc_back, 10));
    count =
        x2_prompt_quads_take_run(x2_prompt_draw_glyphs(6u * 9u - 2u), quads);
    for (i = 0; i < 9u; i++) {
      collapsed +=
          (unsigned)(i < 5u && rect_collapsed((unsigned)emit_before + i));
      stock += (unsigned)(i >= 5u && rect_has_area((unsigned)emit_before + i));
    }
    if (g_emitter_calls != emit_before + 9u || collapsed != 5u || stock != 4u)
      fail("a keycap run left a stock pixel inside the key, or took one "
           "from the words after it");
    else
      ok("every stock glyph inside the key collapses; the words stay stock");
    /* Emitted glyph i sits at x 10+20i .. 28+20i: the left edge is glyph 0
       and the right edge glyph 4. */
    if (!art || count != X2_KEYCAP_QUADS || quads[0].x0 != 10.0f ||
        quads[2].x1 != 108.0f || quads[1].x0 != quads[0].x1 ||
        quads[1].x1 != quads[2].x0 || quads[3].u0 != art->u0 ||
        quads[3].u1 != art->u1 || quads[3].x0 + quads[3].x1 != 118.0f ||
        quads[3].x0 <= quads[0].x0 || quads[3].x1 >= quads[2].x1 ||
        quads[0].color != 0x7f2468acu)
      fail("the key was not drawn as a frame over its span with the "
           "shared ESC label centred on it");
    else
      ok("the key is one shared-art frame over its span, ESC centred on it");
  }

  {
    /* Negatives: a name that is not one printable word, and a right edge
       that closes no key, keep the whole string stock rather than half a
       key. */
    static const uint16_t spaced[] = {X2_KEYCAP_GLYPH_LEFT, 'N', ' ', 'P',
                                      X2_KEYCAP_GLYPH_RIGHT};
    static const uint16_t stray[] = {'A', X2_KEYCAP_GLYPH_RIGHT};
    CPU cpu;
    unsigned count = 99u;
    unsigned long emit_before = g_emitter_calls;

    x2_prompt_quads_reset();
    call_glyph_loop(&cpu, guest_wide(spaced, 5));
    call_glyph_loop(&cpu, guest_wide(stray, 2));
    x2_prompt_quads_pending(&count);
    if (count || g_emitter_calls != emit_before + 6u ||
        !rect_has_area((unsigned)emit_before + 1u) ||
        !rect_has_area((unsigned)emit_before + 5u))
      fail("a spaced name or an unopened keycap produced native art");
    else
      ok("a spaced name or a stray edge keeps its string stock");
  }

  {
    /* Colour refusal is decided before the cursor is armed. Both glyphs
       must remain stock, rather than the first becoming native before a
       later failure is discovered. */
    static const uint16_t two[] = {
        X2_PAD_GLYPH_FACE_A,
        X2_PAD_GLYPH_FACE_B,
    };
    CPU cpu;
    unsigned count = 99u;
    unsigned long emit_before = g_emitter_calls;
    uint32_t good_batch = g_batch;
    g_batch = 0x23456780u;
    x2_prompt_quads_reset();
    call_glyph_loop(&cpu, guest_wide(two, 2));
    x2_prompt_quads_pending(&count);
    if (count || g_emitter_calls != emit_before + 2u ||
        !rect_has_area((unsigned)emit_before) ||
        !rect_has_area((unsigned)emit_before + 1u))
      fail("unreadable color produced a partial native/stock string");
    else
      ok("unreadable color keeps the whole string on the stock path");
    g_batch = good_batch;
  }

  {
    /* Queue capacity is likewise reserved for the whole string. Leave one
       slot and ask for two glyphs: neither retail rectangle may collapse
       and the existing queue count must not change. */
    static const uint16_t two[] = {
        X2_PAD_GLYPH_FACE_A,
        X2_PAD_GLYPH_FACE_B,
    };
    struct X2PromptQuad filler = {0};
    CPU cpu;
    unsigned i, count = 0;
    unsigned long emit_before;
    x2_prompt_quads_reset();
    if (!x2_prompt_quads_begin_run(1u, X2_PROMPT_QUADS_MAX - 1u))
      fail("the store refused a filler run");
    for (i = 0; i + 1u < X2_PROMPT_QUADS_MAX; i++)
      if (!x2_prompt_quads_add(&filler))
        fail("the queue refused a filler before its stated capacity");
    x2_prompt_quads_end_run();
    emit_before = g_emitter_calls;
    call_glyph_loop(&cpu, guest_wide(two, 2));
    x2_prompt_quads_pending(&count);
    if (count != X2_PROMPT_QUADS_MAX - 1u ||
        g_emitter_calls != emit_before + 2u ||
        !rect_has_area((unsigned)emit_before) ||
        !rect_has_area((unsigned)emit_before + 1u))
      fail("short queue capacity produced a partial native/stock string");
    else
      ok("short queue capacity keeps the whole string on the stock path");
    x2_prompt_quads_reset();
  }

  /* The report itself runs, so a change that breaks its format is caught
     here rather than in a run log nobody diffs. */
  printf("  the report reads:\n");
  x2_prompt_draw_report();

  printf("\ntest_prompt_glyph_draw: %d failure(s)\n", failures);
  return failures ? 1 : 0;
}

/*
 * The retail bodies these tests super-call into. Production reaches them
 * through x86_guest_body, so the test models the same seam rather than a
 * symbol per function -- and an entry point this test does not model is a
 * FAILURE that names itself, never a silent return.
 */
void x86_guest_body(CPU *C, const char *module, uint32_t linked_ep) {
  if (linked_ep == 0x005ee400u && !strcmp(module, "XMen2.exe")) {
    guest_body_005ee400(C);
    return;
  }
  if (linked_ep == 0x005ee780u && !strcmp(module, "XMen2.exe")) {
    guest_body_005ee780(C);
    return;
  }
  fprintf(stderr,
          "%s: x86_guest_body(%s, 0x%08x) is not modelled by this test.\n",
          "test_prompt_glyph_draw.c", module, linked_ep);
  abort();
}
