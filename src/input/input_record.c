/* Exact, change-only recording of the DirectInput state delivered to XMen2. */
#include "input_record.h"
#include "directory_create.h"
#include "json_string.h"

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>

#define KEYBOARD_BYTES 256u
#define GAMEPAD_BYTES 272u
#define GAMEPADS 4u

typedef struct {
    unsigned char bytes[GAMEPAD_BYTES];
    size_t size;
    int seen;
} PreviousState;

static FILE *g_file;
static char g_path[512];
/* Developer default; a packaged run replaces it with the OS user-data path. */
static char g_directory[400] = "scratch/recordings";
static PreviousState g_keyboard;
static PreviousState g_mouse;
static PreviousState g_gamepad[GAMEPADS];
static unsigned long g_events;
static int g_reported;

static int same_state(PreviousState *previous, const void *state, size_t bytes)
{
    if (bytes > sizeof previous->bytes) bytes = sizeof previous->bytes;
    if (previous->seen && previous->size == bytes &&
        memcmp(previous->bytes, state, bytes) == 0)
        return 1;
    memcpy(previous->bytes, state, bytes);
    previous->size = bytes;
    previous->seen = 1;
    return 0;
}

static void record_prefix(const char *type, unsigned long frame,
                          double guest_time_s)
{
    fprintf(g_file, "{\"type\":\"%s\",\"frame\":%lu,"
                    "\"guest_time_s\":%.6f,",
            type, frame, guest_time_s);
}

static void record_down_bytes(const unsigned char *state, size_t start,
                              size_t count)
{
    size_t i;
    int comma = 0;
    fputc('[', g_file);
    for (i = 0; i < count; i++) {
        if (!(state[start + i] & 0x80u)) continue;
        fprintf(g_file, "%s%zu", comma ? "," : "", i);
        comma = 1;
    }
    fputc(']', g_file);
}

void input_record_set_directory(const char *directory)
{
    if (!directory || !directory[0]) return;
    snprintf(g_directory, sizeof g_directory, "%s", directory);
}

int input_record_start(const char *path)
{
    time_t now;
    struct tm utc;

    if (!path) return 0;
    if (g_file) return 1;
    if (!path[0]) {
        if (!x2_directory_create(g_directory)) {
            fprintf(stderr, "input record: cannot create %s: %s. REFUSING to "
                            "call this run recorded.\n",
                    g_directory, strerror(errno));
            return 0;
        }
        now = time(NULL);
        gmtime_r(&now, &utc);
        snprintf(g_path, sizeof g_path,
                 "%s/input-%04d%02d%02d-%02d%02d%02d-%ld.jsonl", g_directory,
                 utc.tm_year + 1900, utc.tm_mon + 1, utc.tm_mday,
                 utc.tm_hour, utc.tm_min, utc.tm_sec, (long)getpid());
    } else {
        snprintf(g_path, sizeof g_path, "%s", path);
    }
    g_file = fopen(g_path, "w");
    if (!g_file) {
        fprintf(stderr, "input record: cannot open %s: %s. REFUSING to call "
                        "this run recorded.\n", g_path, strerror(errno));
        g_path[0] = '\0';
        return 0;
    }
    fprintf(g_file,
            "{\"type\":\"session\",\"version\":1,\"pid\":%ld,"
            "\"clock\":\"guest_seconds\","
            "\"source\":\"DirectInput state returned to XMen2\"}\n",
            (long)getpid());
    fflush(g_file);
    printf("input record: exact game-facing input is being written to %s\n",
           g_path);
    atexit(input_record_report);
    return 1;
}

void input_record_keyboard(const void *state, size_t bytes,
                           unsigned long frame, double guest_time_s)
{
    const unsigned char *keys = (const unsigned char *)state;
    if (!g_file || !state || !bytes || same_state(&g_keyboard, state, bytes))
        return;
    if (bytes > KEYBOARD_BYTES) bytes = KEYBOARD_BYTES;
    record_prefix("keyboard", frame, guest_time_s);
    fputs("\"down_dik\":", g_file);
    record_down_bytes(keys, 0, bytes);
    fputs("}\n", g_file);
    g_events++;
    fflush(g_file);
}

void input_record_mouse(const void *state, size_t bytes,
                        unsigned long frame, double guest_time_s)
{
    const unsigned char *mouse = (const unsigned char *)state;
    int32_t x = 0, y = 0, wheel = 0;
    if (!g_file || !state || !bytes || same_state(&g_mouse, state, bytes))
        return;
    if (bytes >= 4u) memcpy(&x, mouse, sizeof x);
    if (bytes >= 8u) memcpy(&y, mouse + 4u, sizeof y);
    if (bytes >= 12u) memcpy(&wheel, mouse + 8u, sizeof wheel);
    record_prefix("mouse", frame, guest_time_s);
    fprintf(g_file, "\"dx\":%d,\"dy\":%d,\"wheel\":%d,\"buttons\":",
            x, y, wheel);
    record_down_bytes(mouse, bytes > 12u ? 12u : bytes,
                      bytes > 12u ? bytes - 12u : 0u);
    fputs("}\n", g_file);
    g_events++;
    fflush(g_file);
}

void input_record_gamepad(unsigned pad, const char *persistent_id,
                          const void *state, size_t bytes,
                          unsigned long frame, double guest_time_s)
{
    const unsigned char *joy = (const unsigned char *)state;
    uint32_t values[9] = {0};
    char id_json[4096];
    unsigned i;
    if (!g_file || pad >= GAMEPADS || !state || !bytes ||
        same_state(&g_gamepad[pad], state, bytes))
        return;
    for (i = 0; i < 9u && (i + 1u) * 4u <= bytes; i++)
        memcpy(&values[i], joy + i * 4u, sizeof values[i]);
    record_prefix("gamepad", frame, guest_time_s);
    if (!json_string_format(id_json, sizeof id_json, persistent_id)) return;
    fprintf(g_file, "\"device\":%u,\"persistent_id\":%s", pad, id_json);
    fputs(",\"axes\":[", g_file);
    for (i = 0; i < 8u; i++)
        fprintf(g_file, "%s%d", i ? "," : "", (int32_t)values[i]);
    fprintf(g_file, "],\"pov\":%u,\"buttons\":", values[8]);
    record_down_bytes(joy, 48u, bytes > 48u ? bytes - 48u : 0u);
    fputs("}\n", g_file);
    g_events++;
    fflush(g_file);
}

const char *input_record_path(void) { return g_path[0] ? g_path : NULL; }

unsigned long input_record_event_count(void) { return g_events; }

void input_record_report(void)
{
    if (g_reported++) return;
    if (!g_file) {
        fprintf(stderr, "  input record: disabled; this run has no replayable "
                        "input history.\n");
        return;
    }
    fprintf(g_file, "{\"type\":\"end\",\"events\":%lu}\n", g_events);
    fflush(g_file);
    fclose(g_file);
    g_file = NULL;
    fprintf(stderr, "  input record: %lu changed state(s) in %s\n",
            g_events, g_path);
}
