/* The bundle root is derived from the path the executable was launched from,
   so a wrong answer here silently turns a packaged app into a developer run
   (project .env, no picker) or a developer run into a packaged one. */
#include "macos_bundle.h"

#include <stdio.h>
#include <string.h>

static int failures;

static void expect_root(const char *executable, const char *expected) {
  char root[4096] = "";
  const int ok = x2_macos_bundle_root(executable, root, (unsigned)sizeof root);

  if (expected) {
    if (!ok || strcmp(root, expected) != 0) {
      printf("FAIL: %s -> %s (expected %s)\n", executable,
             ok ? root : "not a bundle", expected);
      failures++;
    }
  } else if (ok) {
    printf("FAIL: %s was read as the bundle %s\n", executable, root);
    failures++;
  }
}

int main(void) {
  expect_root("/Applications/X-Men Legends II.app/Contents/MacOS/x2native",
              "/Applications/X-Men Legends II.app");
  /* A bundle inside a path that also contains the sequence resolves to the
     innermost one, which is the bundle this executable belongs to. */
  expect_root("/a/Outer.app/Contents/MacOS/Inner.app/Contents/MacOS/x2native",
              "/a/Outer.app/Contents/MacOS/Inner.app");
  /* Not a bundle: no .app, a deeper path below MacOS, or a plain build tree. */
  expect_root("/home/me/xmen2/build/native/x2native", NULL);
  expect_root("/a/Foo.bundle/Contents/MacOS/x2native", NULL);
  expect_root("/a/Foo.app/Contents/MacOS/helpers/x2native", NULL);
  expect_root("/a/Foo.app/Contents/Resources/x2native", NULL);
  expect_root("", NULL);

  /* A capacity that cannot hold the root is a refusal, never a truncation:
     a truncated root names a directory that does not exist. */
  {
    char small[8] = "";
    if (x2_macos_bundle_root("/Applications/Foo.app/Contents/MacOS/x2native",
                             small, (unsigned)sizeof small)) {
      printf("FAIL: a too-small buffer returned a root: %s\n", small);
      failures++;
    }
  }

  printf("test_macos_bundle: %s\n", failures ? "FAILED" : "passed");
  return failures ? 1 : 0;
}
