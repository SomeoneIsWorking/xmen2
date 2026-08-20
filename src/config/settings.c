#include "settings.h"

#include <ctype.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define X2_MIN_WIDTH 640u
#define X2_MAX_WIDTH 7680u
#define X2_MIN_HEIGHT 480u
#define X2_MAX_HEIGHT 4320u

static void reason(char *why, int whyn, const char *text)
{
    if (why && whyn > 0) snprintf(why, (size_t)whyn, "%s", text);
}

const char *x2_window_mode_name(X2WindowMode mode)
{
    static const char *const NAME[] = { "windowed", "borderless", "fullscreen" };
    return mode <= X2_WINDOW_FULLSCREEN ? NAME[mode] : "invalid";
}

int x2_window_mode_parse(const char *text, X2WindowMode *mode)
{
    X2WindowMode i;
    for (i = X2_WINDOW_WINDOWED; i <= X2_WINDOW_FULLSCREEN; i++)
        if (strcmp(text, x2_window_mode_name(i)) == 0) {
            if (mode) *mode = i;
            return 1;
        }
    return 0;
}

const char *x2_player_device_name(X2PlayerDevice device)
{
    static const char *const NAME[] = { "none", "auto", "keyboard", "gamepad" };
    return device <= X2_PLAYER_GAMEPAD ? NAME[device] : "invalid";
}

void x2_settings_defaults(X2Settings *settings)
{
    unsigned i;
    memset(settings, 0, sizeof *settings);
    settings->width = 1280;
    settings->height = 720;
    settings->window_mode = X2_WINDOW_WINDOWED;
    settings->player[0].type = X2_PLAYER_AUTO;
    for (i = 0; i < X2_SETTINGS_PLAYERS; i++) {
        settings->player[i].keyboard_profile = (uint8_t)i;
        if (i == 0) continue;
        settings->player[i].type = X2_PLAYER_NONE;
    }
}

static char *trim(char *s)
{
    char *end;
    while (isspace((unsigned char)*s)) s++;
    end = s + strlen(s);
    while (end > s && isspace((unsigned char)end[-1])) *--end = 0;
    return s;
}

static int number(const char *text, unsigned min, unsigned max, unsigned *out)
{
    char *end;
    unsigned long value;
    errno = 0;
    value = strtoul(text, &end, 10);
    if (errno || end == text || *end || value < min || value > max) return 0;
    *out = (unsigned)value;
    return 1;
}

static int parse_player_device(X2PlayerSettings *player, const char *value)
{
    if (strcmp(value, "none") == 0) player->type = X2_PLAYER_NONE;
    else if (strcmp(value, "auto") == 0) player->type = X2_PLAYER_AUTO;
    else if (strcmp(value, "keyboard") == 0) player->type = X2_PLAYER_KEYBOARD;
    else if (strncmp(value, "gamepad:", 8) == 0 && value[8]) {
        if (strlen(value + 8) >= sizeof player->id) return 0;
        player->type = X2_PLAYER_GAMEPAD;
        snprintf(player->id, sizeof player->id, "%s", value + 8);
        return 1;
    } else return 0;
    player->id[0] = 0;
    return 1;
}

static int parse_profile_binding(X2Settings *settings, const char *key,
                                 const char *value)
{
    unsigned profile, row, code;
    char tail;
    int n = sscanf(key, "input.profile%u.row%u%c", &profile, &row, &tail);
    if (n != 2 || profile >= X2_SETTINGS_KEYBOARD_PROFILES ||
        row >= X2_SETTINGS_ROWS)
        return 0;
    if (!number(value, 0, 65535, &code)) return 0;
    settings->keyboard_profile[profile].keyboard[row] = (uint16_t)code;
    settings->keyboard_profile[profile].keyboard_set[row] = 1;
    return 1;
}

static int parse_line(X2Settings *settings, char *line)
{
    char *eq = strchr(line, '=');
    char *key, *value;
    unsigned player, n, profile;
    char setting_key[48];

    if (!eq) return 0;
    *eq = 0;
    key = trim(line);
    value = trim(eq + 1);
    if (strcmp(key, "video.width") == 0)
        return number(value, X2_MIN_WIDTH, X2_MAX_WIDTH, &settings->width);
    if (strcmp(key, "video.height") == 0)
        return number(value, X2_MIN_HEIGHT, X2_MAX_HEIGHT, &settings->height);
    if (strcmp(key, "video.mode") == 0)
        return x2_window_mode_parse(value, &settings->window_mode);
    for (player = 0; player < X2_SETTINGS_PLAYERS; player++) {
        snprintf(setting_key, sizeof setting_key, "input.player%u.device", player);
        if (strcmp(key, setting_key) == 0)
            return parse_player_device(&settings->player[player], value);
        snprintf(setting_key, sizeof setting_key, "input.player%u.profile", player);
        if (strcmp(key, setting_key) == 0) {
            if (!number(value, 0, X2_SETTINGS_KEYBOARD_PROFILES - 1, &profile))
                return 0;
            settings->player[player].keyboard_profile = (uint8_t)profile;
            return 1;
        }
    }
    n = (unsigned)strlen("input.profile");
    if (strncmp(key, "input.profile", n) == 0)
        return parse_profile_binding(settings, key, value);
    return 0;
}

int x2_settings_load(X2Settings *settings, const char *path,
                     char *why, int whyn)
{
    X2Settings parsed;
    FILE *file;
    char line[512];
    unsigned lineno = 0;

    x2_settings_defaults(&parsed);
    file = fopen(path, "r");
    if (!file) {
        if (errno == ENOENT) {
            *settings = parsed;
            reason(why, whyn, "settings file does not exist; defaults loaded");
            return 1;
        }
        if (why) snprintf(why, (size_t)whyn, "cannot open %s: %s", path,
                          strerror(errno));
        return 0;
    }
    while (fgets(line, sizeof line, file)) {
        char *text;
        lineno++;
        if (!strchr(line, '\n') && !feof(file)) {
            if (why) snprintf(why, (size_t)whyn,
                              "%s:%u is longer than %zu bytes", path, lineno,
                              sizeof line - 2u);
            fclose(file);
            return 0;
        }
        text = trim(line);
        if (!*text || *text == '#') continue;
        if (!parse_line(&parsed, text)) {
            if (why) snprintf(why, (size_t)whyn,
                              "%s:%u has an unknown key or invalid value",
                              path, lineno);
            fclose(file);
            return 0;
        }
    }
    if (ferror(file)) {
        if (why) snprintf(why, (size_t)whyn, "cannot read %s: %s", path,
                          strerror(errno));
        fclose(file);
        return 0;
    }
    fclose(file);
    *settings = parsed;
    reason(why, whyn, "settings loaded");
    return 1;
}

int x2_settings_save(const X2Settings *settings, const char *path,
                     char *why, int whyn)
{
    char pending[1200];
    FILE *file;
    unsigned player, profile, row;

    if (snprintf(pending, sizeof pending, "%s.new", path) >= (int)sizeof pending) {
        reason(why, whyn, "settings path is too long");
        return 0;
    }
    file = fopen(pending, "w");
    if (!file) {
        if (why) snprintf(why, (size_t)whyn, "cannot write %s: %s", pending,
                          strerror(errno));
        return 0;
    }
    fprintf(file, "# x2native settings -- edited by the in-game RmlUi menu\n");
    fprintf(file, "video.width=%u\nvideo.height=%u\nvideo.mode=%s\n",
            settings->width, settings->height,
            x2_window_mode_name(settings->window_mode));
    for (player = 0; player < X2_SETTINGS_PLAYERS; player++) {
        const X2PlayerSettings *p = &settings->player[player];
        fprintf(file, "input.player%u.device=%s%s%s\n", player,
                x2_player_device_name(p->type),
                p->type == X2_PLAYER_GAMEPAD ? ":" : "",
                p->type == X2_PLAYER_GAMEPAD ? p->id : "");
        fprintf(file, "input.player%u.profile=%u\n", player,
                p->keyboard_profile);
    }
    for (profile = 0; profile < X2_SETTINGS_KEYBOARD_PROFILES; profile++) {
        const X2KeyboardProfile *p = &settings->keyboard_profile[profile];
        for (row = 0; row < X2_SETTINGS_ROWS; row++) {
            if (p->keyboard_set[row])
                fprintf(file, "input.profile%u.row%u=%u\n",
                        profile, row, p->keyboard[row]);
        }
    }
    if (fclose(file) != 0 || rename(pending, path) != 0) {
        if (why) snprintf(why, (size_t)whyn, "cannot publish %s: %s", path,
                          strerror(errno));
        return 0;
    }
    reason(why, whyn, "settings saved");
    return 1;
}
