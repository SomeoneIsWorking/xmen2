/* The export-name search against a synthetic sorted name table: every name is
   found at its own index, and names before, between and after them are not. */
#include "pe_export_search.h"

#include <stdio.h>
#include <string.h>

static int failures;

static void check(int ok, const char *what) {
  if (!ok) {
    printf("  FAIL  %s\n", what);
    failures++;
  } else {
    printf("  pass  %s\n", what);
  }
}

int main(void) {
  /* Sorted by byte value, as a linker writes them: '?' (0x3f) sorts before
     upper case, and upper case before '_' and lower case. */
  static const char *const names[] = {
      "??0igObject@@QAE@XZ", "Alloc", "Free", "_ftol", "getElapsed", "reset"};
  enum { kCount = sizeof names / sizeof names[0], kTable = 0x10 };
  unsigned char image[512];
  unsigned i, at = kTable + kCount * 4u, all_found = 1;

  memset(image, 0, sizeof image);
  for (i = 0; i < kCount; i++) {
    const size_t length = strlen(names[i]) + 1u;
    image[kTable + i * 4u] = (unsigned char)at;
    image[kTable + i * 4u + 1u] = (unsigned char)(at >> 8);
    memcpy(image + at, names[i], length);
    at += (unsigned)length;
  }
  for (i = 0; i < kCount; i++) {
    all_found &=
        pe_export_name_index(image, kTable, kCount, names[i]) == (long)i;
  }
  check(all_found, "every exported name is found at its own index");
  check(pe_export_name_index(image, kTable, kCount, "??") == -1,
        "a name sorting before the first export is not found");
  check(pe_export_name_index(image, kTable, kCount, "Alloca") == -1,
        "a name between two exports is not found");
  check(pe_export_name_index(image, kTable, kCount, "zzz") == -1,
        "a name sorting after the last export is not found");
  check(pe_export_name_index(image, kTable, kCount, "Fre") == -1,
        "a prefix of an export is not that export");
  check(pe_export_name_index(image, kTable, 0u, "Alloc") == -1,
        "an empty table finds nothing");
  printf("test_pe_export_search: %d failure(s)\n", failures);
  return failures ? 1 : 0;
}
