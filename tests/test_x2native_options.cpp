#include "x2native_options.h"

#include "../src/config/environment.h"

#include <stdio.h>
#include <string.h>

static int check(int condition, const char *message) {
  if (condition)
    return 0;
  fprintf(stderr, "FAIL: %s\n", message);
  return 1;
}

int main(void) {
  X2NativeOptions o;
  char *plain[] = {"x2native"};
  char *headless[] = {"x2native", "--no-window"};
  char *bringup[] = {"x2native", "--run"};
  char *appimage[] = {"x2native", "--appimage"};
  char *diagnostic[] = {"x2native", "--selftest"};
  char *set_pair[] = {"x2native", "--set", "jit.cache=false", "--no-window"};
  char *set_joined[] = {"x2native", "--set=jit.profile=1024"};
  char *env_pair[] = {"x2native", "--env", "X2_FRAME_DUMP=busy:100"};
  char *env_joined[] = {"x2native", "--env=X2_HEARTBEAT=2"};
  char *env_unknown[] = {"x2native", "--env", "X2_NOT_A_KNOWN_NAME=1"};
  char *env_malformed[] = {"x2native", "--env", "X2_FRAME_DUMP"};
  char *unknown[] = {"x2native", "--nonsense"};
  const char *armed;
  int fails = 0;

  fails += check(x2native_options_parse(1, plain, &o) == 0,
                 "zero-argument parse failed");
  fails += check(o.run && o.d3d8 && o.window && o.product && o.input_record &&
                     !o.input_record[0],
                 "zero arguments did not select the inspectable, recorded "
                 "SDL3 GPU game");
  fails += check(x2native_options_uses_project_env(&o),
                 "developer launch lost project .env support");
  fails += check(x2native_options_parse(2, headless, &o) == 0,
                 "headless parse failed");
  fails +=
      check(o.run && o.d3d8 && !o.window && o.product,
            "--no-window replaced the product mode instead of extending it");
  fails += check(x2native_options_parse(2, bringup, &o) == 0,
                 "bring-up parse failed");
  fails += check(o.run && !o.d3d8,
                 "explicit --run no longer selects renderer-free bring-up");
  fails += check(x2native_options_parse(2, appimage, &o) == 0 && o.appimage &&
                     o.run && o.d3d8 && o.product,
                 "AppImage launch did not retain the product route");
  fails += check(!x2native_options_uses_project_env(&o),
                 "AppImage launch accepted the developer project .env");
  fails += check(x2native_options_parse(2, diagnostic, &o) == 0,
                 "selftest parse failed");
  fails += check(o.selftest && !o.run && !o.d3d8,
                 "a diagnostic was replaced by the default product");
  /* --set NAME=VALUE (and --set=NAME=VALUE) is consumed later by
     x2_runtime_config_init; the option parser must accept it and its value
     without mistaking either for the install directory. */
  fails += check(x2native_options_parse(4, set_pair, &o) == 0 &&
                     !o.install_dir && o.run && o.d3d8 && !o.window,
                 "--set NAME=VALUE was rejected or ate the --no-window flag");
  fails +=
      check(x2native_options_parse(2, set_joined, &o) == 0 && !o.install_dir,
            "--set=NAME=VALUE was rejected");
  fails += check(x2native_options_parse(2, unknown, &o) == 2,
                 "unknown option is no longer refused");
  /* --env NAME=VALUE reaches the configuration owner, which is the only way
     a host with no environment -- the browser -- can arm a diagnostic. An
     unknown or malformed name must be refused rather than silently dropped:
     a diagnostic that does not arm looks exactly like one that found
     nothing. */
  fails += check(x2native_options_parse(3, env_pair, &o) == 0 && !o.install_dir,
                 "--env NAME=VALUE was rejected");
  armed = x2_config_override_get(kX2ConfigFrameDump);
  fails += check(armed && !strcmp(armed, "busy:100"),
                 "--env NAME=VALUE did not reach the configuration owner");
  fails += check(x2native_options_parse(2, env_joined, &o) == 0,
                 "--env=NAME=VALUE was rejected");
  armed = x2_config_override_get(kX2ConfigHeartbeat);
  fails += check(armed && !strcmp(armed, "2"),
                 "--env=NAME=VALUE did not reach the configuration owner");
  fails += check(x2native_options_parse(3, env_unknown, &o) == 2,
                 "--env accepted a name outside the override whitelist");
  fails += check(x2native_options_parse(3, env_malformed, &o) == 2,
                 "--env accepted an assignment with no value");
  printf("x2native options: %d of 20 checks passed\n", 20 - fails);
  return fails ? 1 : 0;
}
