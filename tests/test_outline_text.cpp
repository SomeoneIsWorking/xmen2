/* Outline text: a retail font record's face, scale and replacement quads,
 * against the shipped typefaces. */
#include "guest_memory.h"
#include "outline_text.hpp"
#include "x86rt.h"

#include "platform_mman.h"
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <vector>

namespace {

constexpr uint32_t kGuestBase = 0x71000000u;
constexpr uint32_t kMapBytes = 0x4000u;
constexpr uint32_t kGlyphFirst = 0x18u;
constexpr uint32_t kGlyphStride = 0x1cu;
constexpr uint32_t kStringAt = kGuestBase + 0x3000u;

int failures;

void check(bool condition, const char *what) {
  std::printf("  %s  %s\n", condition ? "pass" : "FAIL", what);
  failures += condition ? 0 : 1;
}

/* The medium HD font's own metrics: capitals on baseline 16, small capitals
   on 13, a padded box and a tight advance. */
/* The emitter's box for a glyph whose pen is at x on the line at top y. */
void box_at(float x, float y, float out[4]) {
  out[0] = x - 1.0f; /* the record's offset is -1 */
  out[1] = y;
  out[2] = out[0] + 21.0f;
  out[3] = y + 18.0f;
}

void put_string(uint32_t at, const char *text) {
  unsigned i = 0;
  for (; text[i]; i++) {
    WR16(at + i * 2u, (uint16_t)(unsigned char)text[i]);
  }
  WR16(at + i * 2u, 0);
}

void retail_glyph(uint32_t font, unsigned code, int width, int height,
                  int advance, int offset, int baseline) {
  const uint32_t g = font + kGlyphFirst + code * kGlyphStride;
  WR16(g + 0u, (uint16_t)width);
  WR16(g + 2u, (uint16_t)height);
  WR16(g + 4u, (uint16_t)advance);
  WR16(g + 6u, (uint16_t)offset);
  WR32(g + 8u, (uint32_t)baseline);
}

} // namespace

uint32_t x86_module_base(const char *) { return 0x00400000u; }

int main() {
  const uint32_t font = kGuestBase;
  if (guest_memory_init() != 0 ||
      guest_memory_map_fixed(kGuestBase, kMapBytes,
                             x2::native::kProtRead | x2::native::kProtWrite) !=
          0) {
    std::fprintf(stderr, "test_outline_text: could not map guest memory\n");
    return 1;
  }
  std::memset(guest_memory_pointer(kGuestBase), 0, kMapBytes);
  for (unsigned c = 'A'; c <= 'Z'; c++) {
    retail_glyph(font, c, 21, 18, 16, -1, 16);
  }
  for (unsigned c = 'a'; c <= 'z'; c++) {
    retail_glyph(font, c, 19, 15, 15, -1, 13);
  }
  retail_glyph(font, 'L', 21, 18, 24, -1, 16);  /* wider than the face's L */
  retail_glyph(font, 0xa4u, 22, 22, 22, 0, 18); /* an icon in a Latin-1 slot */

  x2::native::outline_text_font_loaded(font, "X2F_med_HD");
  x2::native::OutlinePlaneScale plane{1.0f, 1.0f};
  const float box[4] = {100.0f, 50.0f, 121.0f, 68.0f};

  check(x2::native::outline_text_replaces(
            {font, x2::native::OutlineFace::Italic, 1, 1, 1}, 'H'),
        "a capital the record draws is replaced");
  check(!x2::native::outline_text_replaces(
            {font, x2::native::OutlineFace::Italic, 1, 1, 1}, 0xa4u),
        "an icon in a Latin-1 slot stays retail");
  check(!x2::native::outline_text_replaces(
            {font, x2::native::OutlineFace::Italic, 1, 1, 1}, '5'),
        "a glyph the record leaves empty stays retail");

  /* Scales the loader derived: capitals fill 14 of 16 rows. */
  const float caps = 16.0f * 14.0f / 16.0f / x2::native::kOutlineCapPx;
  const float smalls = 13.0f * 14.0f / 16.0f / x2::native::kOutlineCapPx;
  const x2::native::OutlineFont med{font, x2::native::OutlineFace::Italic, caps,
                                    smalls, 1.0f};
  const x2::native::PromptVertexKey key{0x1000u, 0u};
  x2::native::OutlinePen pen;
  float at[4];

  put_string(kStringAt, "Hh\xa4");
  pen.begin(kStringAt);
  check(pen.add(med, 0, box, plane, key, 0xff00ff00u),
        "a capital is queued for the outline face");
  check(pen.add(med, 1, box, plane, key, 0xff00ff00u),
        "a small capital is queued");
  check(!pen.add(med, 2, box, plane, key, 0xff00ff00u), "an icon is not");
  pen.close();
  check(pen.placed().size() == 2u, "the icon closed the run of two");
  const x2::native::PromptQuad cap = pen.placed()[0].quad;
  const x2::native::PromptQuad small = pen.placed()[1].quad;
  pen.clear_placed();
  check(cap.sheet == x2::native::kOutlineTextSheet && cap.color == 0xff00ff00u,
        "the quad samples the outline atlas in the glyph's colour");
  check(small.u0 == cap.u0 && small.v0 == cap.v0,
        "a small capital is the capital's own glyph");
  /* Baseline at box top + 16; the capital's rim reaches one row past it. */
  const float line = box[1] + 16.0f;
  check(cap.y1 > line && cap.y1 < line + 1.5f && cap.y0 < line - 13.0f &&
            cap.y0 > line - 18.0f,
        "the capital stands on the record's baseline at its height");
  check(small.y1 - small.y0 < cap.y1 - cap.y0, "a small capital is smaller");

  /* One line, "HH HH HH": retail capitals 16 apart, an 8-unit space. */
  put_string(kStringAt, "HH HH HH");
  pen.begin(kStringAt);
  float x = 200.0f;
  for (unsigned i = 0; i < 8; i++) {
    box_at(x, 50.0f, at);
    if (i != 2u && i != 5u) {
      pen.add(med, i, at, plane, key, 0u);
    }
    x += i == 2u || i == 5u ? 8.0f : 16.0f;
  }
  const float retail_end = x;
  pen.close();
  const std::vector<x2::native::OutlinePlaced> &line1 = pen.placed();
  check(line1.size() == 6u, "glyphs and spaces on one line make one run");
  const float letter_gap = line1[1].quad.x0 - line1[0].quad.x1;
  const float space1 = line1[2].quad.x0 - line1[1].quad.x1;
  const float space2 = line1[4].quad.x0 - line1[3].quad.x1;
  check(std::fabs(line1[0].quad.x0 - 200.0f) < 3.0f,
        "the run starts at its first glyph's retail pen");
  check(std::fabs(space1 - space2) < 0.01f &&
            std::fabs(line1[4].quad.x0 - line1[3].quad.x1 - space1) < 0.01f,
        "spaces within a run are equal");
  check(space1 > 2.0f * letter_gap,
        "a narrow retail space still reads wider than a letter gap");
  check(line1[5].quad.x1 < retail_end + 0.5f &&
            retail_end - line1[5].quad.x1 < 0.25f * 16.0f,
        "the run ends where retail's advances end it");
  pen.clear_placed();

  /* "HH \x80 HH": a keycap mid-line, at retail x 248. The draw owner closes
     the run when the key arrives; the run after starts at its own pen. */
  put_string(kStringAt, "HH \x80 HH");
  pen.begin(kStringAt);
  box_at(200.0f, 50.0f, at);
  pen.add(med, 0, at, plane, key, 0u);
  box_at(216.0f, 50.0f, at);
  pen.add(med, 1, at, plane, key, 0u);
  pen.close();
  const float before_key = pen.placed()[1].quad.x1;
  pen.clear_placed();
  box_at(280.0f, 50.0f, at);
  pen.add(med, 5, at, plane, key, 0u);
  box_at(296.0f, 50.0f, at);
  pen.add(med, 6, at, plane, key, 0u);
  pen.close();
  check(before_key < 232.5f, "the run before the key ends at retail's end");
  check(std::fabs(pen.placed()[0].quad.x0 - 280.0f) < 3.0f,
        "the run after the key starts at its retail pen, not the face's");
  pen.clear_placed();

  /* A glyph that skips a non-space, or moves to a new line, starts a run. */
  put_string(kStringAt, "HHxHH");
  pen.begin(kStringAt);
  box_at(200.0f, 50.0f, at);
  pen.add(med, 0, at, plane, key, 0u);
  box_at(500.0f, 50.0f, at);
  pen.add(med, 3, at, plane, key, 0u);
  box_at(600.0f, 90.0f, at);
  pen.add(med, 4, at, plane, key, 0u);
  pen.close();
  check(std::fabs(pen.placed()[1].quad.x0 - 500.0f) < 3.0f &&
            std::fabs(pen.placed()[2].quad.x0 - 600.0f) < 3.0f,
        "a skipped glyph or a new line starts a new run");
  pen.clear_placed();

  /* Kerning: the face tucks A under T's bar. */
  const float kern = x2::native::outline_glyph_kerning(
      x2::native::OutlineFace::Italic, 'T', 'A');
  check(kern < -1.0f, "the face's GPOS kerns T before A");
  put_string(kStringAt, "HTAH");
  pen.begin(kStringAt);
  for (unsigned i = 0; i < 4; i++) {
    box_at(500.0f + 16.0f * (float)i, 50.0f, at);
    pen.add(med, i, at, plane, key, 0u);
  }
  pen.close();
  const std::vector<x2::native::OutlinePlaced> &hta = pen.placed();
  const x2::native::OutlineCell *h =
      x2::native::outline_glyph_cell(x2::native::OutlineFace::Italic, 'H');
  const x2::native::OutlineCell *t =
      x2::native::outline_glyph_cell(x2::native::OutlineFace::Italic, 'T');
  const x2::native::OutlineCell *a =
      x2::native::outline_glyph_cell(x2::native::OutlineFace::Italic, 'A');
  const auto kerning = [](char l, char r) {
    return x2::native::outline_glyph_kerning(
        x2::native::OutlineFace::Italic, (std::uint16_t)l, (std::uint16_t)r);
  };
  /* The run's face length against retail's 64 units gives its stretch. */
  const float run = (2.0f * h->advance + t->advance + a->advance +
                     kerning('H', 'T') + kern + kerning('A', 'H')) *
                    caps;
  const float fit = 64.0f / run;
  const float step =
      hta[2].quad.x0 - hta[1].quad.x0 - (a->left - t->left) * caps;
  check(std::fabs(step - (t->advance + kern) * caps * fit) < 0.05f,
        "the pen steps from T to A by the kerned advance");
  pen.clear_placed();

  const x2::native::OutlinePlaneScale flipped{2.0f, -2.0f};
  const float up[4] = {100.0f, 50.0f, 142.0f, 14.0f};
  put_string(kStringAt, "H");
  pen.begin(kStringAt);
  pen.add(med, 0, up, flipped, key, 0u);
  pen.close();
  const x2::native::PromptQuad scaled = pen.placed()[0].quad;
  check(scaled.y1 < scaled.y0 &&
            std::fabs(scaled.x1 - scaled.x0 - 2.0f * (cap.x1 - cap.x0)) < 0.01f,
        "the plane scale and its direction carry through");

  std::printf("%s: %d failure(s)\n", failures ? "FAIL" : "PASS", failures);
  return failures ? 1 : 0;
}
