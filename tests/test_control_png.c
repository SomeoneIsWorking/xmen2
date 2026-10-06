/*
 * The PNG encoder, checked by DECODING what it produced -- not by looking at
 * it, and not by re-implementing the encoder in the test.
 *
 * A hand-written encoder that emits a nearly-valid file is the dangerous case:
 * the bytes appear, the size looks right, and nothing fails until someone
 * tries to open the picture. So this writes a known image, then hands it to
 * tools/check_png.py, which decodes it with Python's own zlib and compares
 * every pixel against the ones that went in.
 *
 * It also feeds the encoder the case that MUST fail (a zero-sized image) and
 * requires NULL, so a "pass" cannot come from an encoder that returns
 * something for everything.
 */
#include "control_png.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define W                                                                      \
  61 /* deliberately not a multiple of anything: a stride bug                  \
        in the filter loop shows up as a skew, and a square                    \
        power-of-two image is exactly where it would not */
#define H 37

int main(void) {
  unsigned char *bgra = malloc((size_t)W * H * 4);
  unsigned char *png;
  size_t len = 0;
  unsigned x, y;
  FILE *f;
  int rc;

  if (!bgra)
    return 1;

  /* A gradient with all three channels different, so a B/R swap or a
     dropped channel cannot survive the comparison. */
  for (y = 0; y < H; y++)
    for (x = 0; x < W; x++) {
      unsigned char *p = bgra + ((size_t)y * W + x) * 4;
      p[0] = (unsigned char)(x * 4); /* B */
      p[1] = (unsigned char)(y * 7); /* G */
      p[2] = (unsigned char)(x + y); /* R */
      p[3] = 0xff;                   /* A, dropped by design */
    }

  /* The negative FIRST: an empty image must produce nothing at all. */
  if (control_png_from_bgra(bgra, 0, H, &len) != NULL) {
    fprintf(stderr, "FAIL: a 0-wide image produced a PNG. An encoder that "
                    "answers for every input cannot fail the real test "
                    "either.\n");
    return 1;
  }
  if (control_png_from_bgra(bgra, W, 0, &len) != NULL) {
    fprintf(stderr, "FAIL: a 0-high image produced a PNG.\n");
    return 1;
  }

  png = control_png_from_bgra(bgra, W, H, &len);
  if (!png || !len) {
    fprintf(stderr, "FAIL: encoding %dx%d produced nothing.\n", W, H);
    return 1;
  }
  if (len < 8 || memcmp(png, "\x89PNG\r\n\x1a\n", 8) != 0) {
    fprintf(stderr, "FAIL: no PNG signature in %zu bytes.\n", len);
    return 1;
  }

  f = fopen("control_png_test.png", "wb");
  if (!f) {
    fprintf(stderr, "FAIL: cannot write the test PNG.\n");
    return 1;
  }
  fwrite(png, 1, len, f);
  fclose(f);
  printf("encoded %dx%d into %zu bytes; decoding it back...\n", W, H, len);
  fflush(stdout);

  /* The independent reader. Its exit code is this test's verdict. */
  rc = system("python3 " CHECK_PNG " control_png_test.png");
  free(png);
  free(bgra);
  return rc == 0 ? 0 : 1;
}
