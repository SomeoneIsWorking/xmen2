#include "x86watch_memory.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int fail(char *error, size_t size, const char *message) {
  if (error && size)
    snprintf(error, size, "%s", message);
  return 0;
}

int x86_memwatch_parse(X86MemWatch *watch, const char *spec, char *error,
                       size_t error_size) {
  const char *cursor = spec;
  char *end;
  unsigned long value;

  memset(watch, 0, sizeof *watch);
  if (!cursor || !*cursor)
    return fail(error, error_size, "the memory watch is empty");
  value = strtoul(cursor, &end, 0);
  if (end == cursor || value > UINT32_MAX)
    return fail(error, error_size, "the root is not a 32-bit address");
  watch->root = (uint32_t)value;
  cursor = end;
  while (*cursor) {
    if (*cursor != ',')
      return fail(error, error_size, "expected a comma before an offset");
    cursor++;
    if (watch->offset_count == X86_MEMWATCH_MAX_OFFSETS)
      return fail(error, error_size, "too many pointer-chain offsets");
    value = strtoul(cursor, &end, 0);
    if (end == cursor || value > UINT32_MAX)
      return fail(error, error_size, "an offset is not a 32-bit number");
    watch->offsets[watch->offset_count++] = (uint32_t)value;
    cursor = end;
  }
  return 1;
}

int x86_memwatch_read(const X86MemWatch *watch, uint32_t mapped_root,
                      x86_memwatch_read32_fn read32, void *context,
                      uint32_t *resolved_address, uint32_t *value) {
  uint32_t address = mapped_root;
  unsigned i;

  for (i = 0; i < watch->offset_count; i++) {
    uint32_t pointer = read32(context, address);
    if (!pointer) {
      *resolved_address = 0;
      *value = 0;
      return 0;
    }
    address = pointer + watch->offsets[i];
  }
  *resolved_address = address;
  *value = read32(context, address);
  return 1;
}
