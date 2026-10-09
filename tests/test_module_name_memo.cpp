/*
 * module_name_memo.cpp keyed by pointer: two strings with the same text are two
 * keys, a name never put is a miss, names whose slots collide all resolve,
 * and a full memo refuses one more without losing what it holds.
 */
#include "module_name_memo.h"

#include <stdio.h>

struct X86Module {
  int id;
};

enum { NAMES = x2::native::MODULE_NAME_MEMO_SLOTS + 1 };

static x2::native::ModuleNameMemo g_memo;
static struct X86Module g_modules[NAMES];
static char g_names[NAMES][2];
static int failures;

static void expect(const char *what, const struct X86Module *got,
                   const struct X86Module *want) {
  if (got != want) {
    fprintf(stderr, "FAIL %s: got %d, want %d\n", what, got ? got->id : -1,
            want ? want->id : -1);
    failures++;
  }
}

int main(void) {
  for (int i = 0; i < NAMES; i++) {
    g_modules[i].id = i;
    g_names[i][0] = 'm';
  }
  expect("a name never put",
         x2::native::module_name_memo_get(&g_memo, g_names[0]), NULL);
  x2::native::module_name_memo_put(&g_memo, g_names[0], &g_modules[0]);
  expect("a name put", x2::native::module_name_memo_get(&g_memo, g_names[0]),
         &g_modules[0]);
  expect("the same text at another address",
         x2::native::module_name_memo_get(&g_memo, g_names[1]), NULL);
  x2::native::module_name_memo_put(&g_memo, g_names[0], &g_modules[0]);
  for (int i = 1; i < x2::native::MODULE_NAME_MEMO_SLOTS; i++)
    x2::native::module_name_memo_put(&g_memo, g_names[i], &g_modules[i]);
  for (int i = 0; i < x2::native::MODULE_NAME_MEMO_SLOTS; i++) {
    char what[48];
    snprintf(what, sizeof what, "name %d of a full memo", i);
    expect(what, x2::native::module_name_memo_get(&g_memo, g_names[i]),
           &g_modules[i]);
  }
  x2::native::module_name_memo_put(
      &g_memo, g_names[x2::native::MODULE_NAME_MEMO_SLOTS],
      &g_modules[x2::native::MODULE_NAME_MEMO_SLOTS]);
  expect("one more than a full memo holds",
         x2::native::module_name_memo_get(
             &g_memo, g_names[x2::native::MODULE_NAME_MEMO_SLOTS]),
         NULL);
  if (failures) {
    fprintf(stderr, "%d failure(s)\n", failures);
    return 1;
  }
  printf("module_name_memo: ok\n");
  return 0;
}
