/*
 * The guest's command line is the game's, not this process's.
 *
 * Run this twice: once with no arguments, and once with the exact tokens that
 * reached the game's line on a desktop host (`--appimage <dir> --set
 * quantum=20000`). Both must produce the same line, and it must name the game.
 *
 * Against the implementation this replaced -- GetCommandLineA returning
 * /proc/self/cmdline -- the second invocation fails, because the test process's
 * own arguments ARE its /proc/self/cmdline. That is the regression this guards.
 * (Issue #151's browser plateau was NOT this leak; see guest_command_line.c.)
 */
#include "guest_command_line.h"

#include "install_requirements.h"

#include <stdio.h>
#include <string.h>

static int failures;

#define CHECK(cond, ...)                                                       \
  do {                                                                         \
    if (!(cond)) {                                                             \
      printf("FAIL: ");                                                        \
      printf(__VA_ARGS__);                                                     \
      printf("\n");                                                            \
      failures++;                                                              \
    }                                                                          \
  } while (0)

/* Every port option that has been observed on the host line, plus the two the
   browser adds unconditionally. A guest line containing any of them means the
   host's arguments are reaching the game again. */
static const char *const kPortTokens[] = {
    "--appimage", "--set",      "--unbounded", "--no-window", "--d3d8",
    "--control",  "--selftest", "--run",       "x2native",
};

int main(int argc, char **argv) {
  const char *line = guest_command_line();
  const char *again = guest_command_line();
  char expected[128];
  unsigned i;

  printf("guest command line: %s  (this process was run with %d argument(s))\n",
         line, argc - 1);

  /* The negative first: the line must not carry a single port option. */
  for (i = 0; i < sizeof kPortTokens / sizeof kPortTokens[0]; i++) {
    CHECK(strstr(line, kPortTokens[i]) == NULL,
          "the guest's command line contains the port option \"%s\": %s",
          kPortTokens[i], line);
  }
  CHECK(strstr(line, "--") == NULL,
        "the guest's command line contains an option-looking token: %s", line);

  /* And the positive: it names the game the loader maps, from the same
     generated list the loader uses. */
  snprintf(expected, sizeof expected, "\"%s\"", X2_INSTALL_MAIN_IMAGE);
  CHECK(strcmp(line, expected) == 0,
        "the guest's command line is %s, expected %s", line, expected);

  /* Empty would be worse than wrong: a CRT deriving argv[0] from it would
     build an argument vector starting with nothing. */
  CHECK(line[0] != '\0', "the guest's command line is empty");
  CHECK(line[strlen(line) - 1] == '"',
        "the guest's command line is not a quoted program name: %s", line);

  /* Stable pointer, as the header promises. */
  CHECK(line == again, "two calls returned different pointers");

  if (failures == 0) {
    printf("guest_command_line: %d check(s) passed, and the guest line ignores "
           "this process's %d argument(s)\n",
           (int)(4 + sizeof kPortTokens / sizeof kPortTokens[0] + 1), argc - 1);
    return 0;
  }
  printf("guest_command_line: %d check(s) FAILED\n", failures);
  return 1;
}
