#define _GNU_SOURCE
#include "env_file.h"

#include <errno.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

static char start_dir[PATH_MAX];
static char test_dir[64];

static void cleanup(void) {
  char path[PATH_MAX];
  if (!start_dir[0] || !test_dir[0])
    return;
  chdir(start_dir);
  snprintf(path, sizeof path, "%s/project/bin/x2native", test_dir);
  unlink(path);
  snprintf(path, sizeof path, "%s/project/.env", test_dir);
  unlink(path);
  snprintf(path, sizeof path, "%s/cwd/.env", test_dir);
  unlink(path);
  snprintf(path, sizeof path, "%s/.env", test_dir);
  unlink(path);
  snprintf(path, sizeof path, "%s/project/bin", test_dir);
  rmdir(path);
  snprintf(path, sizeof path, "%s/project", test_dir);
  rmdir(path);
  snprintf(path, sizeof path, "%s/cwd", test_dir);
  rmdir(path);
  rmdir(test_dir);
}

static int write_text(const char *path, const char *text) {
  FILE *f;
  f = fopen(path, "w");
  if (!f)
    return 0;
  fputs(text, f);
  return fclose(f) == 0;
}

static int setup_simple(const char *text) {
  char path[96];
  snprintf(path, sizeof path, "%s/.env", test_dir);
  return write_text(path, text) && chdir(test_dir) == 0;
}

static int setup_precedence(void) {
  char path[PATH_MAX];
  snprintf(path, sizeof path, "%s/cwd", test_dir);
  if (mkdir(path, 0700) != 0)
    return 0;
  snprintf(path, sizeof path, "%s/project", test_dir);
  if (mkdir(path, 0700) != 0)
    return 0;
  snprintf(path, sizeof path, "%s/project/bin", test_dir);
  if (mkdir(path, 0700) != 0)
    return 0;
  snprintf(path, sizeof path, "%s/cwd/.env", test_dir);
  if (!write_text(path, "X2_VERBOSE=wrong-cwd\n"))
    return 0;
  snprintf(path, sizeof path, "%s/project/.env", test_dir);
  if (!write_text(path, "X2_VERBOSE=from-executable\n"))
    return 0;
  snprintf(path, sizeof path, "%s/project/bin/x2native", test_dir);
  if (!write_text(path, ""))
    return 0;
  snprintf(path, sizeof path, "%s/cwd", test_dir);
  return chdir(path) == 0;
}

int main(int argc, char **argv) {
  const char *got;
  char executable[PATH_MAX];
  int rc;
  if (argc != 2)
    return 2;
  if (!getcwd(start_dir, sizeof start_dir))
    return 2;
  snprintf(test_dir, sizeof test_dir, "env-file-test-%ld", (long)getpid());
  if (mkdir(test_dir, 0700) != 0)
    return 2;
  atexit(cleanup);
  unsetenv("X2_VERBOSE");

  if (!strcmp(argv[1], "load")) {
    if (!setup_simple("X2_VERBOSE='path with spaces'\n"))
      return 2;
    rc = x2_load_project_env(NULL);
    got = getenv("X2_VERBOSE");
    if (rc != 1 || !got || strcmp(got, "path with spaces") != 0)
      return 1;
  } else if (!strcmp(argv[1], "preserve")) {
    if (!setup_simple("X2_VERBOSE=from-file\n"))
      return 2;
    setenv("X2_VERBOSE", "from-launcher", 1);
    rc = x2_load_project_env(NULL);
    got = getenv("X2_VERBOSE");
    if (rc != 1 || !got || strcmp(got, "from-launcher") != 0)
      return 1;
  } else if (!strcmp(argv[1], "malformed")) {
    if (!setup_simple("this is not an assignment\n"))
      return 2;
    if (x2_load_project_env(NULL) != -1)
      return 1;
  } else if (!strcmp(argv[1], "unknown-key")) {
    if (!setup_simple("X2_NOT_A_REGISTERED_SETTING=unsafe\n"))
      return 2;
    if (x2_load_project_env(NULL) != 1 ||
        getenv("X2_NOT_A_REGISTERED_SETTING") != NULL)
      return 1;
  } else if (!strcmp(argv[1], "executable-precedence")) {
    if (!setup_precedence())
      return 2;
    snprintf(executable, sizeof executable, "%s/%s/project/bin/x2native",
             start_dir, test_dir);
    rc = x2_load_project_env(executable);
    got = getenv("X2_VERBOSE");
    if (rc != 1 || !got || strcmp(got, "from-executable") != 0)
      return 1;
  } else
    return 2;
  return 0;
}
