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

void x2_settings_defaults(X2Settings *settings)
{
    unsigned i;
    memset(settings, 0, sizeof *settings);
    settings->width = 1280;
    settings->height = 720;
    settings->window_mode = X2_WINDOW_WINDOWED;
    settings->boot_mode = X2_BOOT_NORMAL;
    for (i = 0; i < X2_SETTINGS_KEYBOARD_PROFILES; i++)
        settings->keyboard_player[i] = X2_SETTINGS_UNASSIGNED;
    for (i = 0; i < X2_SETTINGS_CONTROLLER_ASSIGNMENTS; i++)
        settings->controller[i].player = X2_SETTINGS_UNASSIGNED;
    settings->keyboard_player[0] = 0;
}

static int valid_player(int player)
{
    return player == X2_SETTINGS_UNASSIGNED ||
           (player >= 0 && player < (int)X2_SETTINGS_PLAYERS);
}

int x2_settings_assign_keyboard(X2Settings *settings, unsigned profile,
                                int player)
{
    unsigned i;
    if (!settings || profile >= X2_SETTINGS_KEYBOARD_PROFILES ||
        !valid_player(player)) return 0;
    if (player >= 0)
        for (i = 0; i < X2_SETTINGS_KEYBOARD_PROFILES; i++)
            if (settings->keyboard_player[i] == player)
                settings->keyboard_player[i] = X2_SETTINGS_UNASSIGNED;
    settings->keyboard_player[profile] = (int8_t)player;
    return 1;
}

static int controller_slot(const X2Settings *settings, const char *id)
{
    unsigned i;
    if (!id || !id[0]) return -1;
    for (i = 0; i < X2_SETTINGS_CONTROLLER_ASSIGNMENTS; i++)
        if (strcmp(settings->controller[i].id, id) == 0) return (int)i;
    return -1;
}

int x2_settings_assign_controller(X2Settings *settings, const char *id,
                                  int player)
{
    int slot;
    unsigned i;
    if (!settings || !id || !id[0] || strlen(id) >= X2_SETTINGS_DEVICE_ID ||
        !valid_player(player)) return 0;
    slot = controller_slot(settings, id);
    if (player == X2_SETTINGS_UNASSIGNED) {
        if (slot >= 0) {
            memset(&settings->controller[slot], 0,
                   sizeof settings->controller[slot]);
            settings->controller[slot].player = X2_SETTINGS_UNASSIGNED;
        }
        return 1;
    }
    for (i = 0; i < X2_SETTINGS_CONTROLLER_ASSIGNMENTS; i++)
        if ((int)i != slot && settings->controller[i].player == player) {
            memset(&settings->controller[i], 0, sizeof settings->controller[i]);
            settings->controller[i].player = X2_SETTINGS_UNASSIGNED;
        }
    if (slot < 0)
        for (i = 0; i < X2_SETTINGS_CONTROLLER_ASSIGNMENTS; i++)
            if (!settings->controller[i].id[0] ||
                settings->controller[i].player == X2_SETTINGS_UNASSIGNED) {
                slot = (int)i;
                break;
            }
    if (slot < 0) return 0;
    snprintf(settings->controller[slot].id,
             sizeof settings->controller[slot].id, "%s", id);
    settings->controller[slot].player = (int8_t)player;
    return 1;
}

int x2_settings_controller_player(const X2Settings *settings, const char *id)
{
    int slot = settings ? controller_slot(settings, id) : -1;
    return slot >= 0 ? settings->controller[slot].player
                     : X2_SETTINGS_UNASSIGNED;
}

const char *x2_settings_player_controller(const X2Settings *settings,
                                          unsigned player)
{
    unsigned i;
    if (!settings || player >= X2_SETTINGS_PLAYERS) return NULL;
    for (i = 0; i < X2_SETTINGS_CONTROLLER_ASSIGNMENTS; i++)
        if (settings->controller[i].player == (int)player)
            return settings->controller[i].id;
    return NULL;
}

int x2_settings_player_keyboard(const X2Settings *settings, unsigned player)
{
    unsigned i;
    if (!settings || player >= X2_SETTINGS_PLAYERS) return -1;
    for (i = 0; i < X2_SETTINGS_KEYBOARD_PROFILES; i++)
        if (settings->keyboard_player[i] == (int)player) return (int)i;
    return -1;
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

typedef enum { LEGACY_NONE, LEGACY_AUTO, LEGACY_KEYBOARD, LEGACY_GAMEPAD }
    LegacyDevice;

typedef struct {
    LegacyDevice device;
    char id[X2_SETTINGS_DEVICE_ID];
    unsigned profile;
} LegacyPlayer;

typedef struct {
    X2Settings settings;
    LegacyPlayer legacy_player[X2_SETTINGS_PLAYERS];
    int saw_legacy;
    int saw_grid;
} ParseState;

static int parse_legacy_device(LegacyPlayer *player, const char *value)
{
    if (strcmp(value, "none") == 0) player->device = LEGACY_NONE;
    else if (strcmp(value, "auto") == 0) player->device = LEGACY_AUTO;
    else if (strcmp(value, "keyboard") == 0) player->device = LEGACY_KEYBOARD;
    else if (strncmp(value, "gamepad:", 8) == 0 && value[8]) {
        if (strlen(value + 8) >= sizeof player->id) return 0;
        player->device = LEGACY_GAMEPAD;
        snprintf(player->id, sizeof player->id, "%s", value + 8);
    } else return 0;
    return 1;
}

static int parse_owner(const char *value, int8_t *owner)
{
    unsigned player;
    if (strcmp(value, "unassigned") == 0) {
        *owner = X2_SETTINGS_UNASSIGNED;
        return 1;
    }
    if (!number(value, 0, X2_SETTINGS_PLAYERS - 1, &player)) return 0;
    *owner = (int8_t)player;
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

static int parse_line(ParseState *state, char *line)
{
    X2Settings *settings = &state->settings;
    char *eq = strchr(line, '=');
    char *key, *value;
    unsigned player, n, profile, slot;
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
    if (strcmp(key, "boot.mode") == 0)
        return x2_boot_mode_parse(value, &settings->boot_mode);
    if (strcmp(key, "input.assignment_version") == 0) {
        state->saw_grid = 1;
        return strcmp(value, "2") == 0;
    }
    for (profile = 0; profile < X2_SETTINGS_KEYBOARD_PROFILES; profile++) {
        snprintf(setting_key, sizeof setting_key, "input.keyboard%u.player",
                 profile);
        if (strcmp(key, setting_key) == 0) {
            state->saw_grid = 1;
            return parse_owner(value, &settings->keyboard_player[profile]);
        }
    }
    for (slot = 0; slot < X2_SETTINGS_CONTROLLER_ASSIGNMENTS; slot++) {
        snprintf(setting_key, sizeof setting_key, "input.controller%u.id", slot);
        if (strcmp(key, setting_key) == 0) {
            if (strlen(value) >= X2_SETTINGS_DEVICE_ID) return 0;
            state->saw_grid = 1;
            snprintf(settings->controller[slot].id,
                     sizeof settings->controller[slot].id, "%s", value);
            return 1;
        }
        snprintf(setting_key, sizeof setting_key, "input.controller%u.player",
                 slot);
        if (strcmp(key, setting_key) == 0) {
            state->saw_grid = 1;
            return parse_owner(value, &settings->controller[slot].player);
        }
    }
    for (player = 0; player < X2_SETTINGS_PLAYERS; player++) {
        snprintf(setting_key, sizeof setting_key, "input.player%u.device", player);
        if (strcmp(key, setting_key) == 0) {
            state->saw_legacy = 1;
            return parse_legacy_device(&state->legacy_player[player], value);
        }
        snprintf(setting_key, sizeof setting_key, "input.player%u.profile", player);
        if (strcmp(key, setting_key) == 0) {
            if (!number(value, 0, X2_SETTINGS_KEYBOARD_PROFILES - 1, &profile))
                return 0;
            state->saw_legacy = 1;
            state->legacy_player[player].profile = profile;
            return 1;
        }
    }
    n = (unsigned)strlen("input.profile");
    if (strncmp(key, "input.profile", n) == 0)
        return parse_profile_binding(settings, key, value);
    return 0;
}

static int settings_valid(const X2Settings *settings)
{
    unsigned i, j;
    if ((unsigned)settings->boot_mode > X2_BOOT_CONTINUE) return 0;
    for (i = 0; i < X2_SETTINGS_KEYBOARD_PROFILES; i++) {
        int owner = settings->keyboard_player[i];
        if (!valid_player(owner)) return 0;
        for (j = i + 1; owner >= 0 && j < X2_SETTINGS_KEYBOARD_PROFILES; j++)
            if (settings->keyboard_player[j] == owner) return 0;
    }
    for (i = 0; i < X2_SETTINGS_CONTROLLER_ASSIGNMENTS; i++) {
        const X2ControllerAssignment *a = &settings->controller[i];
        if (!valid_player(a->player) || (a->player >= 0 && !a->id[0])) return 0;
        if (!a->id[0]) continue;
        for (j = i + 1; j < X2_SETTINGS_CONTROLLER_ASSIGNMENTS; j++)
            if (strcmp(a->id, settings->controller[j].id) == 0 ||
                (a->player >= 0 && a->player == settings->controller[j].player))
                return 0;
    }
    return 1;
}

static int migrate_legacy(ParseState *state)
{
    X2KeyboardProfile original[X2_SETTINGS_KEYBOARD_PROFILES];
    unsigned char reserved[X2_SETTINGS_KEYBOARD_PROFILES] = {0};
    unsigned i, profile;
    X2Settings *settings = &state->settings;
    if (!state->saw_legacy) return 1;
    if (state->saw_grid) return 0;
    memcpy(original, settings->keyboard_profile, sizeof original);
    for (i = 0; i < X2_SETTINGS_PLAYERS; i++) {
        LegacyPlayer *legacy = &state->legacy_player[i];
        if (legacy->device == LEGACY_AUTO || legacy->device == LEGACY_KEYBOARD)
            reserved[legacy->profile] = 1;
    }
    for (i = 0; i < X2_SETTINGS_KEYBOARD_PROFILES; i++)
        settings->keyboard_player[i] = X2_SETTINGS_UNASSIGNED;
    memset(settings->controller, 0, sizeof settings->controller);
    for (i = 0; i < X2_SETTINGS_CONTROLLER_ASSIGNMENTS; i++)
        settings->controller[i].player = X2_SETTINGS_UNASSIGNED;
    for (i = 0; i < X2_SETTINGS_PLAYERS; i++) {
        LegacyPlayer *legacy = &state->legacy_player[i];
        if (legacy->device == LEGACY_AUTO || legacy->device == LEGACY_KEYBOARD) {
            profile = legacy->profile;
            if (settings->keyboard_player[profile] != X2_SETTINGS_UNASSIGNED) {
                for (profile = 0; profile < X2_SETTINGS_KEYBOARD_PROFILES;
                     profile++)
                    if (!reserved[profile] &&
                        settings->keyboard_player[profile] ==
                            X2_SETTINGS_UNASSIGNED)
                        break;
                if (profile == X2_SETTINGS_KEYBOARD_PROFILES) return 0;
                settings->keyboard_profile[profile] =
                    original[legacy->profile];
            }
            if (!x2_settings_assign_keyboard(settings, profile, (int)i)) return 0;
        }
        if (legacy->device == LEGACY_GAMEPAD &&
            !x2_settings_assign_controller(settings, legacy->id, (int)i))
            return 0;
    }
    return 1;
}

int x2_settings_load(X2Settings *settings, const char *path,
                     char *why, int whyn)
{
    ParseState parsed;
    FILE *file;
    char line[512];
    unsigned lineno = 0;

    unsigned player;
    memset(&parsed, 0, sizeof parsed);
    x2_settings_defaults(&parsed.settings);
    parsed.legacy_player[0].device = LEGACY_AUTO;
    for (player = 0; player < X2_SETTINGS_PLAYERS; player++)
        parsed.legacy_player[player].profile = player;
    file = fopen(path, "r");
    if (!file) {
        if (errno == ENOENT) {
            *settings = parsed.settings;
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
    if (!migrate_legacy(&parsed) || !settings_valid(&parsed.settings)) {
        if (why) snprintf(why, (size_t)whyn,
                          "%s has conflicting device assignments", path);
        return 0;
    }
    *settings = parsed.settings;
    reason(why, whyn, "settings loaded");
    return 1;
}

int x2_settings_save(const X2Settings *settings, const char *path,
                     char *why, int whyn)
{
    char pending[1200];
    FILE *file;
    unsigned slot, profile, row;

    if (!settings || !settings_valid(settings)) {
        reason(why, whyn, "settings contain conflicting device assignments");
        return 0;
    }
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
    fprintf(file, "boot.mode=%s\n", x2_boot_mode_name(settings->boot_mode));
    fprintf(file, "input.assignment_version=2\n");
    for (profile = 0; profile < X2_SETTINGS_KEYBOARD_PROFILES; profile++) {
        int owner = settings->keyboard_player[profile];
        fprintf(file, "input.keyboard%u.player=", profile);
        if (owner < 0) fprintf(file, "unassigned\n");
        else fprintf(file, "%d\n", owner);
    }
    for (slot = 0; slot < X2_SETTINGS_CONTROLLER_ASSIGNMENTS; slot++) {
        const X2ControllerAssignment *assignment = &settings->controller[slot];
        fprintf(file, "input.controller%u.id=%s\n", slot, assignment->id);
        fprintf(file, "input.controller%u.player=", slot);
        if (assignment->player < 0) fprintf(file, "unassigned\n");
        else fprintf(file, "%d\n", assignment->player);
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
