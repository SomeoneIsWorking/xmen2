#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif
#include "env_file.h"

#include <ctype.h>
#include <errno.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

static char *trim(char *s)
{
    char *end;
    while (isspace((unsigned char)*s)) s++;
    end = s + strlen(s);
    while (end != s && isspace((unsigned char)end[-1])) *--end = 0;
    return s;
}

static int load_file(const char *path)
{
    FILE *f = fopen(path, "r");
    char line[8192];
    unsigned long lineno = 0, loaded = 0, preserved = 0;

    if (!f) {
        if (errno == ENOENT) return 0;
        fprintf(stderr, "x2native: cannot read %s (%s); refusing to run with "
                "an unknown environment\n", path, strerror(errno));
        return -1;
    }
    while (fgets(line, sizeof line, f)) {
        char *key, *eq, *value, *end;
        int quote = 0;
        lineno++;
        if (!strchr(line, '\n') && !feof(f)) {
            fprintf(stderr, "x2native: %s:%lu exceeds %zu bytes; refusing a "
                    "partially parsed .env\n", path, lineno, sizeof line - 1);
            fclose(f);
            return -1;
        }
        key = trim(line);
        if (!*key || *key == '#') continue;
        if (!strncmp(key, "export", 6) && isspace((unsigned char)key[6]))
            key = trim(key + 6);
        eq = strchr(key, '=');
        if (!eq) {
            fprintf(stderr, "x2native: %s:%lu has no '='; refusing malformed "
                    ".env input\n", path, lineno);
            fclose(f);
            return -1;
        }
        *eq = 0;
        key = trim(key);
        if (!*key || !(isalpha((unsigned char)*key) || *key == '_')) goto bad_key;
        for (end = key + 1; *end; end++)
            if (!(isalnum((unsigned char)*end) || *end == '_')) goto bad_key;

        value = trim(eq + 1);
        if (*value == '\'' || *value == '"') {
            quote = *value++;
            end = strrchr(value, quote);
            if (!end || *trim(end + 1)) {
                fprintf(stderr, "x2native: %s:%lu has an unterminated or "
                        "trailed quoted value\n", path, lineno);
                fclose(f);
                return -1;
            }
            *end = 0;
        } else {
            for (end = value; *end; end++) {
                if (*end == '#' && (end == value || isspace((unsigned char)end[-1]))) {
                    *end = 0;
                    break;
                }
            }
            value = trim(value);
        }
        if (getenv(key)) preserved++;
        else if (setenv(key, value, 0) != 0) {
            fprintf(stderr, "x2native: cannot set %s from %s (%s)\n",
                    key, path, strerror(errno));
            fclose(f);
            return -1;
        } else loaded++;
        continue;

bad_key:
        fprintf(stderr, "x2native: %s:%lu has an invalid variable name; "
                "refusing malformed .env input\n", path, lineno);
        fclose(f);
        return -1;
    }
    if (ferror(f)) {
        fprintf(stderr, "x2native: failed while reading %s (%s)\n",
                path, strerror(errno));
        fclose(f);
        return -1;
    }
    fclose(f);
    fprintf(stderr, "x2native: loaded %lu variable(s) from %s; preserved %lu "
            "already supplied by the launcher.\n", loaded, path, preserved);
    return 1;
}

static int try_ancestors(char *dir)
{
    for (;;) {
        char path[PATH_MAX], *slash;
        int rc;
        if (snprintf(path, sizeof path, "%s/.env", dir) >= (int)sizeof path) {
            fprintf(stderr, "x2native: cannot inspect an overlong .env path "
                    "below %s; refusing an ambiguous project environment\n",
                    dir);
            return -1;
        }
        rc = load_file(path);
        if (rc) return rc;
        if (!strcmp(dir, "/")) return 0;
        slash = strrchr(dir, '/');
        if (!slash) return 0;
        if (slash == dir) dir[1] = 0;
        else *slash = 0;
    }
}

int x2_load_project_env(const char *argv0)
{
    char cwd[PATH_MAX], exe[PATH_MAX], *slash;
    int rc;

    /* Prefer the executable's project. A caller launched from some unrelated
       directory may have a different .env above its cwd; loading that first
       would silently configure this game from another project's settings. */
    if (argv0 && realpath(argv0, exe)) {
        slash = strrchr(exe, '/');
        if (slash) {
            *slash = 0;
            rc = try_ancestors(exe);
            if (rc) return rc;
        }
    }
    if (!getcwd(cwd, sizeof cwd)) {
        fprintf(stderr, "x2native: cannot resolve the working directory (%s); "
                "no project .env was found from the executable\n",
                strerror(errno));
        return -1;
    }
    return try_ancestors(cwd);
}
