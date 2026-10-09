#include "advapi32_internal.h"
#include "shell32.h"
#include "x86rt_native.h"

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>

namespace {

int failures;

void expect(bool ok, const char *what) {
  std::printf("  %s  %s\n", ok ? "pass" : "FAIL", what);
  failures += ok ? 0 : 1;
}

} // namespace

namespace x2::native {
const char *save_dir(void) { return "."; }
} // namespace x2::native

void x86_native_export(const char *, const char *, void (*)(X86pCpu *)) {}

int main() {
  x2::native::RegValue *first =
      x2::native::advapi32_store_put("HKEY_CURRENT_USER\\Test", "v0");
  first->type = 4u;
  first->len = 4u;
  std::memcpy(first->data, "\x2a\0\0\0", 4u);
  // Advanced Options alone writes ~256 values; the store must hold all of them.
  for (int i = 1; i < 1000; ++i) {
    const std::string name = "v" + std::to_string(i);
    x2::native::RegValue *value =
        x2::native::advapi32_store_put("HKEY_CURRENT_USER\\Test", name.c_str());
    value->type = 4u;
    value->len = 4u;
    std::memcpy(value->data, &i, 4u);
  }
  x2::native::RegValue *found =
      x2::native::advapi32_store_find("HKEY_CURRENT_USER\\Test", "v999");
  int stored = 0;
  if (found) {
    std::memcpy(&stored, found->data, 4u);
  }
  expect(found && stored == 999, "the 1000th value is stored and found");
  expect(x2::native::advapi32_store_find("HKEY_CURRENT_USER\\Test", "v0") ==
             first,
         "a value handed out first stays at its address as the store grows");
  expect(first->data[0] == 0x2a, "and keeps its contents");
  expect(x2::native::advapi32_store_put("HKEY_CURRENT_USER\\Test", "v500") ==
             x2::native::advapi32_store_find("HKEY_CURRENT_USER\\Test", "v500"),
         "putting an existing value returns that value");
  std::printf("test_advapi32_store: %d failure(s)\n", failures);
  return failures ? 1 : 0;
}
