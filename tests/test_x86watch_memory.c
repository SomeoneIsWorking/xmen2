#include "x86watch_memory.h"

#include <stdint.h>
#include <stdio.h>
#include <string.h>

static int failures;
static unsigned reads;

static void check(int condition, const char *message) {
  if (!condition) {
    fprintf(stderr, "FAIL: %s\n", message);
    failures++;
  }
}

static uint32_t read_fixture(void *context, uint32_t address) {
  const uint32_t *words = context;
  reads++;
  switch (address) {
  case 0x1000:
    return words[0];
  case 0x2000:
    return words[1];
  case 0x3060:
    return words[2];
  default:
    failures++;
    return 0;
  }
}

int main(void) {
  X86MemWatch watch;
  uint32_t fixture[] = {0x2000, 0x3000, 0x446890};
  uint32_t address = 0, value = 0;
  char error[96];

  check(x86_memwatch_parse(&watch, "0x7ac24c,0,0x60", error, sizeof error),
        "valid pointer chain parses");
  check(watch.root == 0x7ac24c && watch.offset_count == 2,
        "parse preserves root and two offsets");
  reads = 0;
  check(x86_memwatch_read(&watch, 0x1000, read_fixture, fixture, &address,
                          &value),
        "resolved chain reports a value");
  check(address == 0x3060 && value == 0x446890 && reads == 3,
        "root -> object -> vtable+slot resolves exactly");

  fixture[0] = 0;
  reads = 0;
  check(!x86_memwatch_read(&watch, 0x1000, read_fixture, fixture, &address,
                           &value),
        "null intermediate pointer is reported unresolved");
  check(address == 0 && value == 0 && reads == 1,
        "unresolved chain never dereferences NULL");

  memset(error, 0, sizeof error);
  check(!x86_memwatch_parse(&watch, "0x7ac24c,nope", error, sizeof error),
        "malformed offset is rejected");
  check(strstr(error, "offset") != NULL,
        "parse failure names the malformed offset");

  if (failures)
    return 1;
  puts("x86watch memory: 3 classes passed");
  return 0;
}
