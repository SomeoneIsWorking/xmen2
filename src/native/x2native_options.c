#include <lucent/log_c.h>
/* Command-line policy for the native executable. */
#include "x2native_options.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

int x2native_options_parse(int argc, char **argv, X2NativeOptions *o) {
  int i;

  memset(o, 0, sizeof *o);
  o->window = 1;
  for (i = 1; i < argc; i++) {
    if (strcmp(argv[i], "--no-window") == 0)
      o->window = 0;
    else if (strcmp(argv[i], "--appimage") == 0)
      o->appimage = 1;
    else if (strcmp(argv[i], "--unbounded") == 0)
      o->unbounded = 1;
    else if (strncmp(argv[i], "--control", 9) == 0)
      o->control = argv[i][9] == '=' ? atoi(argv[i] + 10) : 8420;
    else if (strncmp(argv[i], "--record-input", 14) == 0)
      o->input_record = argv[i][14] == '=' ? argv[i] + 15 : "";
    else if (strcmp(argv[i], "--selftest") == 0)
      o->selftest = 1;
    else if (strcmp(argv[i], "--run") == 0)
      o->run = 1;
    else if (strcmp(argv[i], "--ark-probe") == 0)
      o->ark_probe = 1;
    else if (strcmp(argv[i], "--vk") == 0)
      o->vk = 1;
    else if (strcmp(argv[i], "--vk-selftest") == 0)
      o->vk_selftest = 1;
    else if (strcmp(argv[i], "--vk-permissive") == 0)
      o->vk_permissive = 1;
    else if (strcmp(argv[i], "--d3d8") == 0)
      o->d3d8 = 1;
    else if (strcmp(argv[i], "--d3d8-selftest") == 0)
      o->d3d8_selftest = 1;
    else if (strcmp(argv[i], "--d3d8-permissive") == 0)
      o->d3d8_permissive = 1;
    else if (strcmp(argv[i], "--dialog-selftest") == 0)
      o->dialog_selftest = 1;
    else if (strcmp(argv[i], "--override-selftest") == 0)
      o->override_selftest = 1;
    else if (strcmp(argv[i], "--fault-selftest") == 0)
      o->fault_selftest = 1;
    /* Runtime CVar overrides are consumed by x2_runtime_config_init, which
       re-scans argv. Recognise the token (and its value form) here so the
       unknown-option guard below does not reject it. */
    else if (strcmp(argv[i], "--set") == 0)
      i++;
    else if (strncmp(argv[i], "--set=", 6) == 0)
      ;
    else if (argv[i][0] == '-') {
      lucent_log_error(
          "x2",
          "x2native: unknown option '%s'. Refusing rather "
          "than treating it as the install directory.\n"
          "  Known: --no-window --appimage --unbounded --control[=port] "
          "--record-input[=path] --run "
          "--selftest "
          "--ark-probe "
          "--vk --vk-selftest --vk-permissive\n"
          "         --d3d8 --d3d8-selftest "
          "--d3d8-permissive --dialog-selftest\n"
          "         --fault-selftest "
          "--override-selftest\n"
          "         --set NAME=VALUE (repeatable runtime CVar override)\n",
          argv[i]);
      return 2;
    } else {
      o->install_dir = argv[i];
    }
  }

  /* No action mode means the product: the game through D3D8 on SDL3 GPU.
     --no-window and an install directory modify that product, so neither
     suppresses it. Historical --run remains module bring-up without a
     renderer; diagnostics and alternate renderers stay explicit. */
  if (!o->run && !o->selftest && !o->ark_probe && !o->vk && !o->vk_selftest &&
      !o->d3d8 && !o->d3d8_selftest && !o->dialog_selftest &&
      !o->fault_selftest && !o->override_selftest) {
    o->d3d8 = 1;
    o->run = 1;
    o->product = 1;
    if (!o->input_record)
      o->input_record = "";
  }
  return 0;
}

int x2native_options_uses_project_env(const X2NativeOptions *options) {
  return options && !options->appimage;
}
