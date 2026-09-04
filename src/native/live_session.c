#include "x2_log.h"
/* Stable, repo-local discovery record for the current interactive run. */
#include "directory_create.h"
#include "json_string.h"
#include "live_session.h"

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

static int g_started;
static int g_port;
static char g_recording[512];
/* Developer default; a packaged run replaces it with the OS user-data path,
   because a package does not own the directory it happens to be started in. */
static char g_directory[400] = "scratch/run";

void live_session_set_directory(const char *directory) {
  if (!directory || !directory[0])
    return;
  snprintf(g_directory, sizeof g_directory, "%s", directory);
}

const char *live_session_record_path(void) {
  static char path[512];
  snprintf(path, sizeof path, "%s/live.json", g_directory);
  return path;
}

static int publish(int running) {
  char path[512];
  char next[512];
  FILE *file;
  char recording_json[4096];

  snprintf(path, sizeof path, "%s/live.json", g_directory);
  snprintf(next, sizeof next, "%s/live.json.new", g_directory);
  if (!x2_directory_create(g_directory))
    return 0;
  file = fopen(next, "w");
  if (!file)
    return 0;
  fprintf(file,
          "{\n  \"version\": 1,\n  \"running\": %s,\n"
          "  \"pid\": %ld,\n  \"control_port\": %d,\n"
          "  \"input_recording\": ",
          running ? "true" : "false", (long)getpid(), g_port);
  if (g_recording[0]) {
    if (!json_string_format(recording_json, sizeof recording_json,
                            g_recording)) {
      fclose(file);
      return 0;
    }
    fputs(recording_json, file);
  } else
    fputs("null", file);
  fputs("\n}\n", file);
  if (fclose(file) != 0 || rename(next, path) != 0)
    return 0;
  return 1;
}

void live_session_stop(void) {
  if (g_started)
    (void)publish(0);
}

int live_session_start(int control_port, const char *input_recording) {
  if (control_port <= 0) {
    x2_log_error("live session: no control port is open, so this run "
                 "cannot be published as inspectable.\n");
    return 0;
  }
  g_port = control_port;
  snprintf(g_recording, sizeof g_recording, "%s",
           input_recording ? input_recording : "");
  if (!publish(1)) {
    x2_log_error("live session: cannot publish %s: %s. The game is "
                 "running, but automatic discovery will not find it.\n",
                 live_session_record_path(), strerror(errno));
    return 0;
  }
  g_started = 1;
  atexit(live_session_stop);
  x2_log_info(
      "live session: tools/x2ctl.py probe now resolves this pid and port "
      "through %s\n",
      live_session_record_path());
  return 1;
}
