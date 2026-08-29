/* OS user-config directory resolution, shared by settings and save storage. */
#include "config_directory.h"

#include <errno.h>
#if defined(_WIN32)
#include <direct.h>
#define X2_MKDIR(path) _mkdir(path)
#else
#include <sys/stat.h>
#define X2_MKDIR(path) mkdir(path, 0700)
#endif
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static char g_directory[4096];
static int g_resolved;

const char *x2_config_directory(void)
{
    const char *base = NULL;
    const char *home = getenv("HOME");

    if (g_resolved) return g_directory[0] ? g_directory : NULL;
    g_resolved = 1;
#if defined(_WIN32)
    base = getenv("APPDATA");
#elif defined(__APPLE__)
    if (home && home[0])
        snprintf(g_directory, sizeof g_directory,
                 "%s/Library/Application Support/xmen2", home);
#else
    base = getenv("XDG_CONFIG_HOME");
    if (base && base[0] == '/')
        snprintf(g_directory, sizeof g_directory, "%s/xmen2", base);
    else
        base = NULL;
#endif
    if (!g_directory[0] && base && base[0])
        snprintf(g_directory, sizeof g_directory, "%s/xmen2", base);
    if (!g_directory[0] && home && home[0])
        snprintf(g_directory, sizeof g_directory, "%s/.config/xmen2", home);
    if (!g_directory[0]) return NULL;
    if (strlen(g_directory) >= sizeof g_directory - 1) {
        g_directory[0] = 0;
        return NULL;
    }
    return g_directory;
}

int x2_config_directory_ensure(void)
{
    char copy[sizeof g_directory], *slash;
    const char *path = x2_config_directory();

    if (!path || strlen(path) >= sizeof copy) return 0;
    snprintf(copy, sizeof copy, "%s", path);
    for (slash = copy + (copy[0] == '/' ? 1 : 0); *slash; slash++) {
        if (*slash != '/') continue;
        *slash = 0;
        if (X2_MKDIR(copy) != 0 && errno != EEXIST) return 0;
        *slash = '/';
    }
    return X2_MKDIR(copy) == 0 || errno == EEXIST;
}
