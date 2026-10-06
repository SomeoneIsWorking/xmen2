/* Runtime prompt metrics use the shipping font's baseline authority. */
#include "guest_memory.h"
#include "pad_glyph_codes.h"
#include "prompt_glyph_atlas.h"
#include "prompt_glyph_metrics.h"
#include "prompt_glyphs.h"
#include "x86rt.h"

#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <sys/mman.h>

#define GUEST_BASE 0x71000000u
#define MAP_BYTES 0x6000u
#define GLYPH_FIRST 0x18u
#define GLYPH_STRIDE 0x1cu
#define GL_WIDTH 0x00u
#define GL_HEIGHT 0x02u
#define GL_ADVANCE 0x04u
#define GL_OFFSET 0x06u
#define GL_BASELINE 0x08u

static int failures;

static uint32_t glyph(uint32_t font, unsigned code) {
  return font + GLYPH_FIRST + code * GLYPH_STRIDE;
}

static void check(int condition, const char *what) {
  if (!condition) {
    printf("  FAIL  %s\n", what);
    failures++;
  } else {
    printf("  pass  %s\n", what);
  }
}

static void stock_glyph(uint32_t font, unsigned code, unsigned height,
                        int baseline) {
  uint32_t g = glyph(font, code);
  WR16(g + GL_WIDTH, 14u);
  WR16(g + GL_HEIGHT, (uint16_t)height);
  WR16(g + GL_ADVANCE, 15u);
  WR32(g + GL_BASELINE, (uint32_t)baseline);
}

int main(void) {
  uint32_t font = GUEST_BASE;
  uint32_t empty_font = GUEST_BASE + 0x2000u;
  uint32_t later_font = GUEST_BASE + 0x4000u;
  uint32_t face_b, edge, occupied, empty_cell, retail;
  int key_h;
  void *page;
  if (guest_memory_init() != 0 ||
      guest_memory_map_fixed(GUEST_BASE, MAP_BYTES, PROT_READ | PROT_WRITE) !=
          0) {
    fprintf(stderr,
            "test_prompt_glyph_metrics: could not map guest "
            "memory at 0x%08x\n",
            GUEST_BASE);
    return 1;
  }
  page = guest_memory_pointer(GUEST_BASE);
  memset(page, 0, MAP_BYTES);

  /* Two retail glyphs establish capitals 36 tall on baseline 29; one
     outlier of each proves this is not merely the first drawing record.
     ui_text_scale has already run, so 36 capitals ARE the design's 18: the
     cells publish at scale 2 and 29 is not scaled again. */
  stock_glyph(font, 'A', 36u, 29);
  stock_glyph(font, 'B', 20u, 7);
  stock_glyph(font, 'C', 36u, 29);
  /* Taller accented capitals outnumbering A..Z do not set the size. */
  stock_glyph(font, 0xc0u, 44u, 29);
  stock_glyph(font, 0xc1u, 44u, 29);
  stock_glyph(font, 0xc2u, 44u, 29);
  occupied = glyph(font, X2_PROMPT_GLYPH_FIRST);
  WR16(occupied + GL_WIDTH, 1u);
  WR16(occupied + GL_HEIGHT, 2u);
  WR16(occupied + GL_ADVANCE, 3u);
  WR16(occupied + GL_OFFSET, 4u);
  WR32(occupied + GL_BASELINE, 5u);
  /* A byte the manifest leaves to the retail font (0x99, the trademark
     sign) draws in this font by design: it has no published cell, so it is
     neither a collision nor overwritten -- #184. */
  retail = glyph(font, 0x99u);
  WR16(retail + GL_WIDTH, 6u);
  x2_prompt_glyph_publish_metrics(font);
  check(RD16(retail + GL_WIDTH) == 6u && RD16(retail + GL_ADVANCE) == 0u &&
            !x2_prompt_glyph_cell(0x99u) && !x2_prompt_glyph_cell(0x8Cu) &&
            !x2_prompt_glyph_cell(0x9Cu),
        "retail-font bytes inside the run carry no port cell");
  check(X2_KEYCAP_GLYPH_LEFT != 0x99u && X2_KEYCAP_GLYPH_LEFT != 0x9Cu &&
            X2_PAD_GLYPH_DPAD_UP != 0x8Cu && X2_KEYCAP_GLYPH_RIGHT != 0x99u &&
            X2_KEYCAP_GLYPH_RIGHT != 0x9Cu && x2_prompt_glyph_cell(0x8Du) &&
            x2_prompt_glyph_cell(X2_KEYCAP_GLYPH_RIGHT),
        "the codepoint assignment skips the retail bytes");

  face_b = glyph(font, X2_PROMPT_GLYPH_FIRST + 1u);
  check((int16_t)RD16(face_b + GL_WIDTH) == 38 &&
            (int16_t)RD16(face_b + GL_HEIGHT) == 38 &&
            (int16_t)RD16(face_b + GL_ADVANCE) == 38,
        "pad art fills its reserved advance and scales once");
  check((int32_t)RD32(face_b + GL_BASELINE) == 30,
        "pad art is centred on the capitals' unscaled modal baseline");
  check((int16_t)RD16(face_b + GL_OFFSET) == 0,
        "the prompt glyph keeps a zero horizontal offset");
  check(RD16(occupied + GL_WIDTH) == 1u && RD16(occupied + GL_HEIGHT) == 2u &&
            RD16(occupied + GL_ADVANCE) == 3u &&
            RD16(occupied + GL_OFFSET) == 4u &&
            RD32(occupied + GL_BASELINE) == 5u,
        "an occupied shipped codepoint is not overwritten");
  check(!x2_prompt_glyph_available(X2_PROMPT_GLYPH_FIRST) &&
            !x2_prompt_glyph_cell(X2_PROMPT_GLYPH_FIRST),
        "an occupied codepoint becomes globally unavailable to native art");

  /* Discovery in one font governs all later fonts. Publishing metrics into
     a later blank record would make the same byte mean native art in one
     font and foreign retail art in another. */
  stock_glyph(later_font, 'A', 18u, 17);
  x2_prompt_glyph_publish_metrics(later_font);
  check(RD16(glyph(later_font, X2_PROMPT_GLYPH_FIRST) + GL_WIDTH) == 0u,
        "a globally unavailable codepoint is not published in later fonts");
  /* Each font sizes its prompts from its own capitals: half-height capitals
     give half-size art, not the first font's. */
  check(RD16(glyph(later_font, X2_PROMPT_GLYPH_FIRST + 1u) + GL_WIDTH) == 19u,
        "a font with smaller capitals publishes proportionally smaller art");

  edge = glyph(font, X2_KEYCAP_GLYPH_LEFT);
  key_h = 2 * x2_prompt_glyph_cell(X2_KEYCAP_GLYPH_LEFT)->design_h;
  check(key_h > 36 && RD16(edge + GL_WIDTH) == 10u &&
            (int)RD16(edge + GL_HEIGHT) == key_h &&
            (int16_t)RD16(edge + GL_ADVANCE) == 8 &&
            (int32_t)RD32(edge + GL_BASELINE) == 29 + (key_h - 36) / 2,
        "a keycap edge stands taller than the capitals, centred on them");

  WR16(glyph(empty_font, X2_PROMPT_GLYPH_LAST) + GL_WIDTH, 9u);
  x2_prompt_glyph_publish_metrics(empty_font);
  empty_cell = glyph(empty_font, X2_PROMPT_GLYPH_FIRST + 1u);
  check(RD16(empty_cell + GL_WIDTH) == 0u &&
            RD32(empty_cell + GL_BASELINE) == 0u,
        "a font without evidenced capitals is left untouched");
  check(!x2_prompt_glyph_available(X2_PROMPT_GLYPH_LAST),
        "occupancy is authoritative even in a font without a baseline");

  printf("  the report reads:\n");
  x2_prompt_glyph_metrics_report();
  printf("\ntest_prompt_glyph_metrics: %d failure(s)\n", failures);
  return failures ? 1 : 0;
}
