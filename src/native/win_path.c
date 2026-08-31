/* Guest Windows path resolution, replacement packs, and file-open evidence.
 * Every native file consumer goes through this owner so CRT, KERNEL32, and
 * media decode cannot disagree on drive mapping or case folding. */
#include "win_path.h"

#include "save_trace_runtime.h"
#include "shell32.h"

#include <ctype.h>
#include <dirent.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/stat.h>
#include <unistd.h>

#define ASSET_MAX 1024
#define WIN_PATH_MAX 1024

static char g_asset[ASSET_MAX][96];
static int g_nasset, g_asset_over, g_file_gate_hit;
static unsigned long g_opens_total, g_opens_failed, g_replaced;

static int files_traced(void)
{
    static int on = -1;
    if (on < 0) {
        const char *value = getenv("X2_FILES");
        on = value && *value && *value != '0';
        if (on)
            fprintf(stderr,
                "[FILE] tracing file operations (X2_FILES). What this DOES "
                "show: every CreateFile-family call with its answer, and the "
                "FIRST open of each distinct name from any path including the "
                "CRT's fopen. What it does NOT show: repeat opens of a name "
                "already listed. Totals are in the shutdown report.\n");
    }
    return on;
}

void k32_file_trace(const char *operation, const char *guest_path,
                    const char *host_path, const char *outcome)
{
    if (!files_traced()) return;
    fprintf(stderr, "[FILE] %-18s \"%s\"\n         -> \"%s\"  %s\n",
            operation, guest_path ? guest_path : "(null)",
            host_path ? host_path : "(null)", outcome);
}

/* `known_root` is an already validated host directory, not guest input.  It
 * keeps Android's resolver inside the app sandbox: an app may access its own
 * /data/user/... tree but cannot enumerate /data itself. */
static int resolve_case_insensitive(char *path, const char *known_root)
{
    char resolved[WIN_PATH_MAX];
    char *component, *separator;
    DIR *directory;
    struct dirent *entry;
    size_t used;
    size_t root_size = 0;

    if (!path || !*path) return 0;
    if (known_root && *known_root) {
        root_size = strlen(known_root);
        while (root_size > 1 && known_root[root_size - 1] == '/') root_size--;
        if (strncmp(path, known_root, root_size) == 0
            && (path[root_size] == '\0' || path[root_size] == '/')) {
            if (root_size >= sizeof resolved) return 0;
            memcpy(resolved, known_root, root_size);
            resolved[root_size] = '\0';
            component = path + root_size;
            while (*component == '/') component++;
        } else {
            root_size = 0;
            component = path;
        }
    } else {
        component = path;
    }
    if (!root_size && path[0] == '/') {
        resolved[0] = '/';
        resolved[1] = '\0';
        component = path + 1;
    } else if (!root_size) {
        resolved[0] = '\0';
    }
    used = strlen(resolved);
    while (*component) {
        char matched[NAME_MAX + 1];
        size_t component_size;
        int found = 0;

        separator = strchr(component, '/');
        component_size = separator ? (size_t)(separator - component)
                                   : strlen(component);
        if (!component_size) {
            if (files_traced())
                fprintf(stderr, "[FILE] path resolver refused an empty component in \"%s\"\n",
                        path);
            return 0;
        }
        directory = opendir(resolved[0] ? resolved : ".");
        if (!directory) {
            if (files_traced())
                fprintf(stderr, "[FILE] path resolver cannot open \"%s\" while resolving \"%.*s\": %s\n",
                        resolved[0] ? resolved : ".", (int)component_size,
                        component, strerror(errno));
            return 0;
        }
        for (entry = readdir(directory); entry; entry = readdir(directory))
            if (strlen(entry->d_name) == component_size
                && strncasecmp(entry->d_name, component, component_size) == 0)
                break;
        if (entry) {
            snprintf(matched, sizeof matched, "%s", entry->d_name);
            found = 1;
        }
        closedir(directory);
        if (!found) {
            if (files_traced())
                fprintf(stderr, "[FILE] path resolver found no \"%.*s\" under \"%s\"\n",
                        (int)component_size, component,
                        resolved[0] ? resolved : ".");
            return 0;
        }
        if (used && resolved[used - 1] != '/') {
            if (used + 1 >= sizeof resolved) {
                if (files_traced())
                    fprintf(stderr, "[FILE] path resolver exceeded %u bytes resolving \"%s\"\n",
                            (unsigned)sizeof resolved, path);
                return 0;
            }
            resolved[used++] = '/';
            resolved[used] = '\0';
        }
        if (used + component_size >= sizeof resolved) {
            if (files_traced())
                fprintf(stderr, "[FILE] path resolver exceeded %u bytes resolving \"%s\"\n",
                        (unsigned)sizeof resolved, path);
            return 0;
        }
        memcpy(resolved + used, matched, component_size);
        used += component_size;
        resolved[used] = '\0';
        if (!separator) break;
        component = separator + 1;
    }
    snprintf(path, WIN_PATH_MAX, "%s", resolved);
    if (access(path, F_OK) == 0) return 1;
    if (files_traced())
        fprintf(stderr, "[FILE] path resolver reached \"%s\" but access failed: %s\n",
                path, strerror(errno));
    return 0;
}

const char *win_path(const char *input)
{
    static char path[WIN_PATH_MAX];
    const char *game = getenv("GAME_PC_DIR");
    const char *tail = input;
    const char *root = game;
    size_t index;
    if (!input) return NULL;
    /* S: is the writable save drive. Every other guest drive, a leading
       slash, and every relative name resolve against the read-only install. */
    if ((input[0] == X2_SAVE_DRIVE || input[0] == X2_SAVE_DRIVE + 32)
            && input[1] == ':') {
        root = x2_save_dir();
        tail = input + 2;
        while (*tail == '\\' || *tail == '/') tail++;
        snprintf(path, sizeof(path), "%s/%s", root, tail);
    } else if (input[0] && input[1] == ':') {
        tail = input + 2;
        while (*tail == '\\' || *tail == '/') tail++;
        snprintf(path, sizeof(path), "%s/%s", game ? game : ".", tail);
    } else if (input[0] == '\\' || input[0] == '/') {
        while (*tail == '\\' || *tail == '/') tail++;
        snprintf(path, sizeof(path), "%s/%s", game ? game : ".", tail);
    }
    else
        snprintf(path, sizeof(path), "%s/%s", game ? game : ".", input);
    for (index = 0; path[index]; ++index)
        if (path[index] == '\\') path[index] = '/';
    resolve_case_insensitive(path, root);
    return path;
}

static int contains_case_insensitive(const char *haystack, const char *needle)
{
    size_t needle_size = strlen(needle), index;
    if (!needle_size) return 1;
    for (index = 0; haystack[index]; ++index) {
        size_t matched = 0;
        while (haystack[index + matched] && matched < needle_size
                && tolower((unsigned char)haystack[index + matched])
                   == tolower((unsigned char)needle[matched])) matched++;
        if (matched == needle_size) return 1;
    }
    return 0;
}

int k32_file_gate_open(void)
{
    const char *wanted = getenv("X2_SHOT_AFTER_FILE");
    return !wanted || !*wanted || g_file_gate_hit;
}

static void note_asset(const char *guest_path, int succeeded,
                       const char *host_path)
{
    int index;
    g_opens_total++;
    if (!succeeded) g_opens_failed++;
    if (!guest_path) return;
    if (!g_file_gate_hit) {
        const char *wanted = getenv("X2_SHOT_AFTER_FILE");
        if (wanted && *wanted
                && contains_case_insensitive(guest_path, wanted)) {
            g_file_gate_hit = 1;
            fprintf(stderr, "[FILE] X2_SHOT_AFTER_FILE=\"%s\" matched "
                            "\"%s\" -- the scene gate is now OPEN.\n",
                    wanted, guest_path);
        }
    }
    for (index = 0; index < g_nasset; ++index)
        if (strcmp(g_asset[index], guest_path) == 0) return;
    if (g_nasset == ASSET_MAX) { g_asset_over++; return; }
    snprintf(g_asset[g_nasset++], sizeof(g_asset[0]), "%s", guest_path);
    if (files_traced())
        fprintf(stderr, "[FILE] first open of \"%s\"\n"
                        "       -> \"%s\"%s\n",
                guest_path, host_path ? host_path : "(null)",
                succeeded ? "" : "  -- NOT FOUND");
}

void k32_asset_report(void)
{
    int index;
    printf("  files: %lu open call(s) over %d distinct name(s)%s; %lu failed\n",
           g_opens_total, g_nasset,
           g_asset_over ? " (the name table is FULL -- some are not listed)"
                        : "", g_opens_failed);
    if (getenv("X2_ASSETS"))
        printf("         X2_ASSETS=%s -- %lu name(s) were replaced from it%s\n",
               getenv("X2_ASSETS"), g_replaced,
               g_replaced ? "" : ". NONE: nothing the game opened had a "
                                 "counterpart there, so this run drew the "
                                 "shipped assets");
    if (!files_traced()) {
        printf("         set X2_FILES=1 to list them as they are opened.\n");
        return;
    }
    for (index = 0; index < g_nasset; ++index)
        printf("         %s\n", g_asset[index]);
}

static const char *asset_replacement(const char *guest_path)
{
    static char path[1024];
    const char *root = getenv("X2_ASSETS");
    char relative[512];
    size_t index;
    struct stat status;
    if (!root || !*root || !guest_path) return NULL;
    if (guest_path[0] && guest_path[1] == ':') guest_path += 2;
    while (*guest_path == '\\' || *guest_path == '/') guest_path++;
    for (index = 0; guest_path[index] && index + 1 < sizeof(relative); ++index)
        relative[index] = guest_path[index] == '\\' ? '/' : guest_path[index];
    relative[index] = '\0';
    snprintf(path, sizeof(path), "%s/%s", root, relative);
    if (resolve_case_insensitive(path, root) && stat(path, &status) == 0 &&
        S_ISREG(status.st_mode)) return path;
    for (index = 0; relative[index]; ++index)
        if (relative[index] >= 'A' && relative[index] <= 'Z')
            relative[index] = (char)(relative[index] + 32);
    snprintf(path, sizeof(path), "%s/%s", root, relative);
    return resolve_case_insensitive(path, root) && stat(path, &status) == 0 &&
           S_ISREG(status.st_mode) ? path : NULL;
}

const char *k32_open_path(const char *guest_path, int for_write)
{
    const char *replacement = for_write ? NULL : asset_replacement(guest_path);
    return replacement ? replacement : win_path(guest_path);
}

int k32_open_replaced(const char *guest_path, int for_write)
{
    return !for_write && asset_replacement(guest_path) != NULL;
}

void k32_open_note(const char *guest_path, int succeeded, int replaced,
                   const char *host_path)
{
    note_asset(guest_path, succeeded, host_path);
    x2_save_trace_asset_open(guest_path, succeeded);
    if (!replaced || !succeeded) return;
    {
        static char reported[64][96];
        static int reported_count;
        int index;
        for (index = 0; index < reported_count; ++index)
            if (strcmp(reported[index], guest_path) == 0) return;
        if (reported_count < 64)
            snprintf(reported[reported_count++], sizeof(reported[0]), "%s",
                     guest_path);
        g_replaced++;
        fprintf(stderr, "assets: REPLACED \"%s\" with %s (X2_ASSETS)\n",
                guest_path, host_path);
    }
}
