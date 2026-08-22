/* Stable, repo-local discovery record for the current interactive run. */
#include "live_session.h"
#include "json_string.h"

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

static int g_started;
static int g_port;
static char g_recording[512];

static int publish(int running)
{
    const char *path = "scratch/run/live.json";
    const char *next = "scratch/run/live.json.new";
    FILE *file;
    char recording_json[4096];

    if (mkdir("scratch", 0775) != 0 && errno != EEXIST) return 0;
    if (mkdir("scratch/run", 0775) != 0 && errno != EEXIST) return 0;
    file = fopen(next, "w");
    if (!file) return 0;
    fprintf(file, "{\n  \"version\": 1,\n  \"running\": %s,\n"
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
    }
    else fputs("null", file);
    fputs("\n}\n", file);
    if (fclose(file) != 0 || rename(next, path) != 0) return 0;
    return 1;
}

void live_session_stop(void)
{
    if (g_started) (void)publish(0);
}

int live_session_start(int control_port, const char *input_recording)
{
    if (control_port <= 0) {
        fprintf(stderr, "live session: no control port is open, so this run "
                        "cannot be published as inspectable.\n");
        return 0;
    }
    g_port = control_port;
    snprintf(g_recording, sizeof g_recording, "%s",
             input_recording ? input_recording : "");
    if (!publish(1)) {
        fprintf(stderr, "live session: cannot publish scratch/run/live.json: "
                        "%s. The game is running, but automatic discovery "
                        "will not find it.\n", strerror(errno));
        return 0;
    }
    g_started = 1;
    atexit(live_session_stop);
    printf("live session: tools/x2ctl.py probe now resolves this pid and port "
           "through scratch/run/live.json\n");
    return 1;
}
