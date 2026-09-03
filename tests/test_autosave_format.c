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

int main(void) {
  static const unsigned char PAYLOAD[] =
      "\n[SAVEGAMEBEGIN: 00:13 - Sanctuary (Normal)]\0******\n";
  static const unsigned char NO_TAG[] = "\nnot a retail save";
  static const unsigned char NO_CLOSE[] = "\n[SAVEGAMEBEGIN: incomplete";
  static const unsigned char CONTROL[] = "\n[SAVEGAMEBEGIN: bad\tname]";
  static const char EXPECTED[] = "00:13 - Sanctuary (Normal)";
  unsigned char header[X2_SAVE_HEADER_BYTES];
  unsigned i;

  memset(header, 0xa5, sizeof header);
  CHECK(x2_autosave_header_from_payload(PAYLOAD, sizeof PAYLOAD, header));
  CHECK(!strcmp((const char *)header, EXPECTED));
  for (i = (unsigned)strlen(EXPECTED); i < sizeof header; i++)
    CHECK(header[i] == 0u);
  CHECK(!x2_autosave_header_from_payload(NO_TAG, sizeof NO_TAG, header));
  CHECK(!x2_autosave_header_from_payload(NO_CLOSE, sizeof NO_CLOSE, header));
  CHECK(!x2_autosave_header_from_payload(CONTROL, sizeof CONTROL, header));
  CHECK(!x2_autosave_header_from_payload(NULL, sizeof PAYLOAD, header));
  CHECK(!x2_autosave_header_from_payload(PAYLOAD, sizeof PAYLOAD, NULL));

  printf("autosave_format: %d checks, %d failures\n", checks, failures);
  return failures != 0;
}
