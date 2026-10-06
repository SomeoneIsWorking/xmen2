#define _POSIX_C_SOURCE 200809L

#include "config_directory.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

int main(void) {
#if defined(_WIN32)
  _putenv("APPDATA=C:\\x2-config-test");
#elif defined(__APPLE__)
  setenv("HOME", "/x2-home-test", 1);
#else
  setenv("XDG_CONFIG_HOME", "/x2-config-test", 1);
#endif
  const char *path = x2_config_directory();
  if (!path || !path[0]) {
    fprintf(stderr, "config directory was not resolved: %s\n",
            path ? path : "(null)");
    return 1;
  }
#if defined(_WIN32)
  if (strcmp(path, "C:\\x2-config-test/xmen2") != 0) {
#elif defined(__APPLE__)
  if (strcmp(path, "/x2-home-test/Library/Application Support/xmen2") != 0) {
#else
  if (strcmp(path, "/x2-config-test/xmen2") != 0) {
#endif
    fprintf(stderr, "unexpected config directory: %s\n", path);
    return 1;
  }
  printf("config_directory: %s\n", path);
  return 0;
}
