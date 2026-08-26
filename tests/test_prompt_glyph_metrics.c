/* Runtime prompt metrics use the shipping font's baseline authority. */
#include "pad_glyph_codes.h"
#include "prompt_glyph_metrics.h"
#include "prompt_glyphs.h"
#include "x86rt.h"

#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <sys/mman.h>

#define GUEST_BASE    0x71000000u
#define MAP_BYTES     0x6000u
#define GLYPH_FIRST   0x18u
#define GLYPH_STRIDE  0x1cu
#define GL_WIDTH      0x00u
#define GL_HEIGHT     0x02u
#define GL_ADVANCE    0x04u
#define GL_OFFSET     0x06u
#define GL_BASELINE   0x08u

static int failures;

static uint32_t glyph(uint32_t font, unsigned code)
{
    return font + GLYPH_FIRST + code * GLYPH_STRIDE;
}

static void check(int condition, const char *what)
{
    if (!condition) {
        printf("  FAIL  %s\n", what);
        failures++;
    } else {
        printf("  pass  %s\n", what);
    }
}

static void stock_glyph(uint32_t font, unsigned code, int baseline)
{
    uint32_t g = glyph(font, code);
    WR16(g + GL_WIDTH, 14u);
    WR16(g + GL_HEIGHT, 13u);
    WR16(g + GL_ADVANCE, 15u);
    WR32(g + GL_BASELINE, (uint32_t)baseline);
}

int main(void)
{
    uint32_t font = GUEST_BASE;
    uint32_t empty_font = GUEST_BASE + 0x2000u;
    uint32_t later_font = GUEST_BASE + 0x4000u;
    uint32_t face_b, rewind, occupied, empty_cell;
    void *page = mmap((void *)(uintptr_t)GUEST_BASE, MAP_BYTES,
                      PROT_READ | PROT_WRITE,
                      MAP_PRIVATE | MAP_ANONYMOUS | MAP_FIXED_NOREPLACE,
                      -1, 0);
    if (page != (void *)(uintptr_t)GUEST_BASE) {
        fprintf(stderr, "test_prompt_glyph_metrics: could not map guest "
                        "memory at 0x%08x\n", GUEST_BASE);
        return 1;
    }
    memset(page, 0, MAP_BYTES);

    /* Two retail glyphs establish 29 as the mode; one outlier proves this is
       not merely the first drawing record. ui_text_scale has already run, so
       29 is copied as-is even though the new design metrics use scale 2. */
    stock_glyph(font, 'A', 29);
    stock_glyph(font, 'B', 7);
    stock_glyph(font, 'C', 29);
    occupied = glyph(font, X2_PROMPT_GLYPH_FIRST);
    WR16(occupied + GL_WIDTH, 1u);
    WR16(occupied + GL_HEIGHT, 2u);
    WR16(occupied + GL_ADVANCE, 3u);
    WR16(occupied + GL_OFFSET, 4u);
    WR32(occupied + GL_BASELINE, 5u);
    x2_prompt_glyph_publish_metrics(font, 2.0f);

    face_b = glyph(font, X2_PROMPT_GLYPH_FIRST + 1u);
    check((int16_t)RD16(face_b + GL_WIDTH) == 36 &&
          (int16_t)RD16(face_b + GL_HEIGHT) == 36 &&
          (int16_t)RD16(face_b + GL_ADVANCE) == 38,
          "design dimensions and advance are scaled once");
    check((int32_t)RD32(face_b + GL_BASELINE) == 29,
          "the modal retail baseline is copied without double scaling");
    check((int16_t)RD16(face_b + GL_OFFSET) == 0,
          "the prompt glyph keeps a zero horizontal offset");
    check(RD16(occupied + GL_WIDTH) == 1u &&
          RD16(occupied + GL_HEIGHT) == 2u &&
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
    stock_glyph(later_font, 'A', 17);
    x2_prompt_glyph_publish_metrics(later_font, 1.0f);
    check(RD16(glyph(later_font, X2_PROMPT_GLYPH_FIRST) + GL_WIDTH) == 0u,
          "a globally unavailable codepoint is not published in later fonts");
    check(RD16(glyph(later_font, X2_PROMPT_GLYPH_FIRST + 1u) + GL_WIDTH) != 0u,
          "other private codepoints still publish in later fonts");

    rewind = glyph(font, X2_KEYCAP_GLYPH_REWIND);
    check(RD16(rewind + GL_WIDTH) == 0u &&
          RD16(rewind + GL_HEIGHT) == 0u &&
          (int16_t)RD16(rewind + GL_ADVANCE) == -16 &&
          (int32_t)RD32(rewind + GL_BASELINE) == 29,
          "the invisible rewind retains its negative advance and baseline");

    WR16(glyph(empty_font, X2_PROMPT_GLYPH_LAST) + GL_WIDTH, 9u);
    x2_prompt_glyph_publish_metrics(empty_font, 1.0f);
    empty_cell = glyph(empty_font, X2_PROMPT_GLYPH_FIRST + 1u);
    check(RD16(empty_cell + GL_WIDTH) == 0u &&
          RD32(empty_cell + GL_BASELINE) == 0u,
          "a font without an evidenced baseline is left untouched");
    check(!x2_prompt_glyph_available(X2_PROMPT_GLYPH_LAST),
          "occupancy is authoritative even in a font without a baseline");

    printf("  the report reads:\n");
    x2_prompt_glyph_metrics_report();
    printf("\ntest_prompt_glyph_metrics: %d failure(s)\n", failures);
    return failures ? 1 : 0;
}
