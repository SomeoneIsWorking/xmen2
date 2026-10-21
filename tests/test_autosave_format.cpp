#include "autosave_format.h"

#include <stdio.h>
#include <string.h>

static int checks;
static int failures;
#define CHECK(x)                                                               \
  do {                                                                         \
    checks++;                                                                  \
    if (!(x)) {                                                                \
      failures++;                                                              \
      fprintf(stderr, "CHECK failed at %s:%d: %s\n", __FILE__, __LINE__, #x);  \
    }                                                                          \
  } while (0)

/* A payload's head as 0x00468990 writes it: newline, then the tag field. */
static void payload(unsigned char out[64], const char *tag) {
  memset(out, 0, 64);
  out[0] = '\n';
  memcpy(out + 1, tag, strlen(tag) < 0x32u ? strlen(tag) : 0x32u);
}

int main(void) {
  static const char EXPECTED[] = "00:13 - Sanctuary (Normal)";
  unsigned char head[64];
  unsigned char header[x2::save::kSaveHeaderBytes];
  unsigned i;

  payload(head, "[SAVEGAMEBEGIN: 00:13 - Sanctuary (Normal)]");
  memset(header, 0xa5, sizeof header);
  CHECK(x2::save::autosave_header_from_payload(head, sizeof head, header));
  CHECK(!strcmp((const char *)header, EXPECTED));
  for (i = (unsigned)strlen(EXPECTED); i < sizeof header; i++)
    CHECK(header[i] == 0u);
  /* Southern Habitats does not fit; the field ends in a NUL, not "]". */
  payload(head, "[SAVEGAMEBEGIN: 00:00 - Southern Habitats (Normal");
  CHECK(x2::save::autosave_header_from_payload(head, sizeof head, header));
  CHECK(!strcmp((const char *)header, "00:00 - Southern Habitats (Normal"));
  payload(head, "not a retail save");
  CHECK(!x2::save::autosave_header_from_payload(head, sizeof head, header));
  payload(head, "[SAVEGAMEBEGIN: ]");
  CHECK(!x2::save::autosave_header_from_payload(head, sizeof head, header));
  payload(head, "[SAVEGAMEBEGIN: bad\tname]");
  CHECK(!x2::save::autosave_header_from_payload(head, sizeof head, header));
  memset(head + 1, 'x', 63);
  memcpy(head + 1, "[SAVEGAMEBEGIN: ", 16);
  CHECK(!x2::save::autosave_header_from_payload(head, sizeof head, header));
  payload(head, "[SAVEGAMEBEGIN: 00:13 - Sanctuary (Normal)]");
  CHECK(!x2::save::autosave_header_from_payload(head, 0x32u, header));
  CHECK(!x2::save::autosave_header_from_payload(NULL, sizeof head, header));
  CHECK(!x2::save::autosave_header_from_payload(head, sizeof head, NULL));

  printf("autosave_format: %d checks, %d failures\n", checks, failures);
  return failures != 0;
}
