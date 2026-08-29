/* AppImage first-run installation chooser.
 *
 * The portable release cannot ask a player to export GAME_PC_DIR: an AppImage
 * is launched from a desktop and has no terminal. SDL owns both the small
 * setup prompt and the native file dialog; the selected directory is kept
 * with the port's user configuration, never beside the read-only game. ZIP
 * extraction is shared with other ports through Lucent. */
#include "install_picker.h"
#include "../config/config_directory.h"

#include <SDL3/SDL.h>

#include <cerrno>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <string>
#include <strings.h>
#include <sys/stat.h>

#include <lucent/zip.h>

#define X2_INSTALL_PATH_SIZE 4096
static char g_directory[X2_INSTALL_PATH_SIZE];
static char g_selected[X2_INSTALL_PATH_SIZE];
static SDL_AtomicInt g_dialog_done;
static int g_dialog_status;

static int copy_string(char *destination, size_t capacity, const char *source)
{
    size_t length;
    if (!destination || !source) return 0;
    length = strlen(source);
    if (!length || length >= capacity) return 0;
    memcpy(destination, source, length + 1);
    return 1;
}

extern "C" int x2_install_picker_directory_from_executable(const char *path,
                                                             char *directory,
                                                             unsigned capacity)
{
    const char *slash;
    struct stat info;
    size_t length;

    if (!path || !directory || capacity < 2 || stat(path, &info) != 0
            || !S_ISREG(info.st_mode)) return 0;
    slash = strrchr(path, '/');
    if (!slash || slash == path || strcasecmp(slash + 1, "XMen2.exe") != 0)
        return 0;
    length = (size_t)(slash - path);
    if (!length || length >= capacity) return 0;
    memcpy(directory, path, length);
    directory[length] = 0;
    return 1;
}

static int preference_path(char *path, size_t capacity)
{
    const char *base = x2_config_directory();
    int written;
    if (!base || !x2_config_directory_ensure()) return 0;
    written = snprintf(path, capacity, "%s/install-path.txt", base);
    return written > 0 && (size_t)written < capacity;
}

static int saved_directory(void)
{
    char path[X2_INSTALL_PATH_SIZE], line[X2_INSTALL_PATH_SIZE];
    FILE *file;
    if (!preference_path(path, sizeof path)) return 0;
    file = fopen(path, "r");
    if (!file) return 0;
    if (!fgets(line, sizeof line, file)) {
        fclose(file);
        return 0;
    }
    fclose(file);
    line[strcspn(line, "\r\n")] = 0;
    if (!line[0]) return 0;
    snprintf(path, sizeof path, "%s/XMen2.exe", line);
    if (!x2_install_picker_directory_from_executable(
            path, g_directory, sizeof g_directory)) return 0;
    return 1;
}

static void remember_directory(const char *directory)
{
    char path[X2_INSTALL_PATH_SIZE];
    FILE *file;
    if (!directory || !preference_path(path, sizeof path)) return;
    file = fopen(path, "w");
    if (!file) {
        fprintf(stderr, "install picker: could not remember installation: %s\n",
                strerror(errno));
        return;
    }
    fprintf(file, "%s\n", directory);
    fclose(file);
}

static void SDLCALL file_dialog_callback(void *unused,
                                          const char * const *filelist,
                                          int filter)
{
    (void)unused;
    (void)filter;
    g_dialog_status = 0;
    if (filelist && filelist[0]
            && copy_string(g_selected, sizeof g_selected, filelist[0]))
        g_dialog_status = 1;
    else if (!filelist)
        g_dialog_status = -1;
    SDL_SetAtomicInt(&g_dialog_done, 1);
}

static int prompt(SDL_Window *window)
{
    static const SDL_MessageBoxButtonData buttons[] = {
        { SDL_MESSAGEBOX_BUTTON_RETURNKEY_DEFAULT, 1, "Browse" },
        { SDL_MESSAGEBOX_BUTTON_ESCAPEKEY_DEFAULT, 0, "Quit" },
    };
    static const SDL_MessageBoxData message = {
        SDL_MESSAGEBOX_INFORMATION, NULL,
        "X-Men Legends II installation",
        "Choose XMen2.exe, or a ZIP containing exactly one XMen2.exe, from your legally obtained PC install.",
        2, buttons, NULL,
    };
    int button = 0;
    SDL_MessageBoxData shown = message;
    shown.window = window;
    if (!SDL_ShowMessageBox(&shown, &button)) {
        fprintf(stderr, "install picker: setup prompt failed: %s\n",
                SDL_GetError());
        return 0;
    }
    return button == 1;
}

static int error_prompt(SDL_Window *window, const char *reason)
{
    SDL_MessageBoxButtonData button = {
        SDL_MESSAGEBOX_BUTTON_RETURNKEY_DEFAULT, 1, "Back" };
    SDL_MessageBoxData message = {
        SDL_MESSAGEBOX_ERROR, window, "X-Men Legends II installation",
        reason,
        1, &button, NULL,
    };
    return SDL_ShowMessageBox(&message, NULL) != 0;
}

static int choose_file(SDL_Window *window)
{
    static const SDL_DialogFileFilter filters[] = {
        { "X-Men Legends II executable", "exe" },
        { "ZIP archive", "zip" },
        { "All files", "*" },
    };
    SDL_SetAtomicInt(&g_dialog_done, 0);
    g_dialog_status = 0;
    g_selected[0] = 0;
    SDL_ShowOpenFileDialog(file_dialog_callback, NULL, window, filters, 3,
                           NULL, false);
    while (!SDL_GetAtomicInt(&g_dialog_done)) {
        SDL_PumpEvents();
        SDL_Delay(16);
    }
    return g_dialog_status;
}

static int is_zip_path(const char *path)
{
    const char *extension = strrchr(path, '.');
    return extension && strcasecmp(extension, ".zip") == 0;
}

static unsigned archive_id(const char *path)
{
    unsigned hash = 2166136261u;
    for (const unsigned char *cursor = (const unsigned char *)path; *cursor; ++cursor) {
        hash ^= *cursor;
        hash *= 16777619u;
    }
    return hash;
}

static int directory_from_selection(const char *selection, char *directory,
                                    unsigned capacity, char *reason,
                                    size_t reason_capacity)
{
    if (!is_zip_path(selection)) {
        if (x2_install_picker_directory_from_executable(
                selection, directory, capacity)) return 1;
        snprintf(reason, reason_capacity,
                 "That file is not XMen2.exe. Choose the executable or a ZIP containing exactly one XMen2.exe.");
        return 0;
    }

    const char *base = x2_config_directory();
    if (!base || !x2_config_directory_ensure()) {
        snprintf(reason, reason_capacity,
                 "The user configuration directory could not be created.");
        return 0;
    }
    char destination[X2_INSTALL_PATH_SIZE];
    const int written = snprintf(destination, sizeof destination,
                                  "%s/xmen2-install-%08x", base,
                                  archive_id(selection));
    if (written <= 0 || (size_t)written >= sizeof destination) {
        snprintf(reason, reason_capacity, "The selected archive path is too long.");
        return 0;
    }

    std::filesystem::path executable;
    std::string extraction_error;
    if (!lucent::zip::extract_install(selection, destination, "XMen2.exe",
                                      executable, extraction_error) ||
        !x2_install_picker_directory_from_executable(
            executable.string().c_str(), directory, capacity)) {
        snprintf(reason, reason_capacity,
                 "That ZIP could not be used: %s", extraction_error.c_str());
        return 0;
    }
    return 1;
}

extern "C" int x2_install_picker_choose(const char **directory)
{
    SDL_Window *window;
    char candidate[X2_INSTALL_PATH_SIZE];
    char reason[512];

    if (!directory) return -1;
    *directory = NULL;
    if (!SDL_Init(SDL_INIT_VIDEO)) {
        fprintf(stderr, "install picker: SDL video initialization failed: %s\n",
                SDL_GetError());
        return -1;
    }
    window = SDL_CreateWindow("X-Men Legends II", 640, 240,
                              SDL_WINDOW_HIDDEN);
    if (!window) {
        fprintf(stderr, "install picker: setup window failed: %s\n",
                SDL_GetError());
        SDL_QuitSubSystem(SDL_INIT_VIDEO);
        return -1;
    }
    if (saved_directory()) {
        *directory = g_directory;
        SDL_DestroyWindow(window);
        SDL_QuitSubSystem(SDL_INIT_VIDEO);
        return 0;
    }
    for (;;) {
        if (!prompt(window)) {
            SDL_DestroyWindow(window);
            SDL_QuitSubSystem(SDL_INIT_VIDEO);
            return -1;
        }
        {
            int result = choose_file(window);
            if (result < 0) {
                fprintf(stderr, "install picker: file dialog failed: %s\n",
                        SDL_GetError());
                SDL_DestroyWindow(window);
                SDL_QuitSubSystem(SDL_INIT_VIDEO);
                return -1;
            }
            if (result == 0) {
                SDL_DestroyWindow(window);
                SDL_QuitSubSystem(SDL_INIT_VIDEO);
                return -1;
            }
        }
        if (!directory_from_selection(g_selected, candidate, sizeof candidate,
                                      reason, sizeof reason)) {
            if (!error_prompt(window, reason)) {
                SDL_DestroyWindow(window);
                SDL_QuitSubSystem(SDL_INIT_VIDEO);
                return -1;
            }
            continue;
        }
        copy_string(g_directory, sizeof g_directory, candidate);
        remember_directory(g_directory);
        *directory = g_directory;
        SDL_DestroyWindow(window);
        SDL_QuitSubSystem(SDL_INIT_VIDEO);
        return 0;
    }
}
