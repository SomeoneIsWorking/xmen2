#include "xbox_defaults.h"

#include <assert.h>
#include <stdio.h>

static int checks;
#define CHECK(c)                                                               \
  do {                                                                         \
    assert(c);                                                                 \
    checks++;                                                                  \
  } while (0)

int main(void) {
  static const XboxDefaultBinding expected[] = {
      {0, 0x04},  {1, 0x03},  {2, 0x02},  {3, 0x01},  {4, 0x15},  {5, 0x16},
      {6, 0x18},  {7, 0x17},  {8, 0x06},  {9, 0x05},  {10, 0x1a}, {12, 0x14},
      {13, 0x13}, {14, 0x12}, {15, 0x11}, {16, 0x1e}, {17, 0x1c}, {18, 0x1b},
      {19, 0x0a}, {20, 0x09}, {21, 0x08}, {22, 0x07},
  };
  const XboxDefaultBinding *actual;
  size_t count, i, j;

  actual = xbox_default_bindings(&count);
  CHECK(count == sizeof expected / sizeof expected[0]);
  for (i = 0; i < count; i++) {
    CHECK(actual[i].binding == expected[i].binding);
    CHECK(actual[i].code == expected[i].code);
    for (j = i + 1; j < count; j++)
      CHECK(actual[i].binding != actual[j].binding);
  }
  printf("test_xbox_defaults: %d checks over %zu canonical bindings\n", checks,
         count);
  return 0;
}
