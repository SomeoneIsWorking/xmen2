#define _GNU_SOURCE
#include "x86watch_stack.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int failures;

static char *report(uint32_t guest_esp, uint32_t cpu, unsigned long size) {
  char *text = NULL;
  size_t length = 0;
  FILE *out = open_memstream(&text, &length);
  x86_watch_stack_report(out, 0x401000, guest_esp, cpu, size);
  fclose(out);
  return text;
}

static void expect(uint32_t guest_esp, uint32_t cpu, const char *needle) {
  char *text = report(guest_esp, cpu, 0x100);
  if (!strstr(text, needle)) {
    fprintf(stderr, "FAIL: expected %s in %s", needle, text);
    failures++;
  }
  free(text);
}

int main(void) {
  expect(0x70001000, 0x70000e00, "SHARED STACK");
  expect(0x70001000, 0x71000000, "ABOVE guest_esp");
  expect(0x70001000, 0x60000000, "SEPARATE STACKS");
  if (failures)
    return 1;
  puts("x86watch stack: shared, above, and separate classes passed");
  return 0;
}
