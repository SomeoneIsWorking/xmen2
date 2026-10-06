/* platform_mman.h's contract on whatever host builds it: a no-access
   reservation, pages made accessible inside it, contents kept across a
   protection round trip, a collision refused, and the whole released. */
#include "platform_mman.h"

#include <errno.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

static int g_checks;
static int g_failed;

static void check(int condition, const char *what) {
  g_checks++;
  if (!condition) {
    g_failed++;
    printf("  FAIL: %s\n", what);
  }
}

int main(void) {
  const long page = x2_page_size();
  check(page >= 4096 && (page & (page - 1)) == 0,
        "the host page size is a power of two of at least 4 KiB");
  if (page <= 0) {
    return 1;
  }
  const size_t size = (size_t)page * 16u;
  uint8_t *base =
      static_cast<uint8_t *>(x2_map_anonymous(NULL, size, PROT_NONE));
  check(base != X2_MAP_FAILED && base != NULL, "reserves 16 no-access pages");
  if (base == X2_MAP_FAILED || base == NULL) {
    return 1;
  }
  uint8_t *const second = base + page * 2;
  check(x2_protect(second, (size_t)page, PROT_READ | PROT_WRITE) == 0,
        "makes one page inside the reservation writable");
  memset(second, 0xa5, (size_t)page);
  check(second[0] == 0xa5 && second[page - 1] == 0xa5,
        "the page holds what was written");
  check(x2_protect(second, (size_t)page, PROT_NONE) == 0,
        "takes the page's access away");
  check(x2_protect(second, (size_t)page, PROT_READ) == 0,
        "gives read access back");
  check(second[0] == 0xa5 && second[page - 1] == 0xa5,
        "a protection round trip keeps the contents");
  const int span =
      x2_protect(base + page * 4, (size_t)page * 3, PROT_READ | PROT_WRITE);
  check(span == 0, "makes a three-page span writable");
  base[page * 6] = 7;
  check(base[page * 6] == 7, "the span's last page is writable");

  void *const collision = x2_map_anonymous(base, (size_t)page, PROT_READ);
  check(collision == X2_MAP_FAILED || collision != base,
        "a mapping onto a live reservation is refused, never placed there");
  if (collision != X2_MAP_FAILED && collision != NULL) {
    (void)x2_unmap(collision, (size_t)page);
  }

  check(x2_unmap(base, size) == 0, "releases the whole reservation");
  printf("platform mman: %d check(s), %d failure(s)\n", g_checks, g_failed);
  return g_failed ? 1 : 0;
}
