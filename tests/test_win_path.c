#include "win_path.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#ifndef X2_TEST_WIN_PATH_ROOT
#define X2_TEST_WIN_PATH_ROOT "scratch/test-win-path"
#endif

static int g_save_notes;

const char *x2_save_dir(void) { return X2_TEST_WIN_PATH_ROOT "/saves"; }

void x2_save_trace_asset_open(const char *guest_path, int succeeded) {
  if (guest_path && succeeded)
    g_save_notes++;
}

static void make_file(const char *path) {
  FILE *file = fopen(path, "wb");
  if (file) {
    fputc('x', file);
    fclose(file);
  }
}

int main(void) {
  const char *root = X2_TEST_WIN_PATH_ROOT;
  char expected[1024];
  char actual[1024];
  int failures = 0;
  unlink(X2_TEST_WIN_PATH_ROOT "/Data/Foo.SFD");
  unlink(X2_TEST_WIN_PATH_ROOT "/pack/movies/cine01.sfd");
  rmdir(X2_TEST_WIN_PATH_ROOT "/Data");
  rmdir(X2_TEST_WIN_PATH_ROOT "/pack/movies");
  rmdir(X2_TEST_WIN_PATH_ROOT "/pack");
  rmdir(X2_TEST_WIN_PATH_ROOT "/saves");
  rmdir(X2_TEST_WIN_PATH_ROOT);
  failures += mkdir(root, 0700) != 0;
  failures += mkdir(X2_TEST_WIN_PATH_ROOT "/Data", 0700) != 0;
  failures += mkdir(X2_TEST_WIN_PATH_ROOT "/pack", 0700) != 0;
  failures += mkdir(X2_TEST_WIN_PATH_ROOT "/pack/movies", 0700) != 0;
  failures += mkdir(X2_TEST_WIN_PATH_ROOT "/saves", 0700) != 0;
  make_file(X2_TEST_WIN_PATH_ROOT "/Data/Foo.SFD");
  make_file(X2_TEST_WIN_PATH_ROOT "/pack/movies/cine01.sfd");
  setenv("GAME_PC_DIR", root, 1);
  setenv("X2_ASSETS", X2_TEST_WIN_PATH_ROOT "/pack", 1);
  setenv("X2_SHOT_AFTER_FILE", "cine01", 1);
  snprintf(actual, sizeof(actual), "%s", win_path("C:\\data\\foo.sfd"));
  failures += access(actual, F_OK) != 0;
  failures += strstr(actual, "/Data/Foo.SFD") == NULL;
  failures += !k32_open_replaced("Movies\\CINE01.SFD", 0);
  snprintf(expected, sizeof(expected), "%s/pack/movies/cine01.sfd", root);
  snprintf(actual, sizeof(actual), "%s",
           k32_open_path("Movies\\CINE01.SFD", 0));
  failures += strcmp(actual, expected) != 0;
  failures += k32_file_gate_open();
  k32_open_note("Movies\\CINE01.SFD", 1, 1, actual);
  failures += !k32_file_gate_open() || g_save_notes != 1;
  snprintf(expected, sizeof(expected), "%s/saves/slot.dat", root);
  snprintf(actual, sizeof(actual), "%s", win_path("S:\\slot.dat"));
  failures += strcmp(actual, expected) != 0;
  unlink(X2_TEST_WIN_PATH_ROOT "/Data/Foo.SFD");
  unlink(X2_TEST_WIN_PATH_ROOT "/pack/movies/cine01.sfd");
  rmdir(X2_TEST_WIN_PATH_ROOT "/Data");
  rmdir(X2_TEST_WIN_PATH_ROOT "/pack/movies");
  rmdir(X2_TEST_WIN_PATH_ROOT "/pack");
  rmdir(X2_TEST_WIN_PATH_ROOT "/saves");
  rmdir(X2_TEST_WIN_PATH_ROOT);
  printf("Win32 path owner: %s -- case folding, replacement, save drive, "
         "open evidence, and scene gate share one production module\n",
         failures ? "FAILED" : "PASSED");
  return failures != 0;
}
