#include "x2_log.h"
/* The player's PC installation: judging a selection (executable, folder or
 * ZIP), the OS file dialog, and the remembered directory in the port's user
 * configuration. The first-run screen that uses these is install_setup. */
#include "../config/config_directory.h"
#include "install_archive.h"
#include "install_picker.h"
#include "install_validation.h"

#include <SDL3/SDL.h>

#include "platform_strings.h"
#include <cerrno>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <string>
#include <sys/stat.h>

#include <lucent/zip.h>

namespace x2::native {

#define X2_INSTALL_PATH_SIZE 4096

namespace {

char g_selected[X2_INSTALL_PATH_SIZE];

SDL_AtomicInt g_dialog_done;

int g_dialog_status;

int copy_string(char *destination, size_t capacity, const char *source) {
  size_t length;
  if (!destination || !source)
    return 0;
  length = strlen(source);
  if (!length || length >= capacity)
    return 0;
  memcpy(destination, source, length + 1);
  return 1;
}

} // namespace

int install_picker_directory_from_executable(const char *path, char *directory,
                                             unsigned capacity) {
  const char *slash;
  struct stat info;
  size_t length;

  if (!path || !directory || capacity < 2 || stat(path, &info) != 0 ||
      !S_ISREG(info.st_mode))
    return 0;
  slash = strrchr(path, '/');
#if defined(_WIN32)
  /* Windows hands back either separator, often both in one path. */
  const char *backslash = strrchr(path, '\\');
  if (backslash && (!slash || backslash > slash))
    slash = backslash;
#endif
  if (!slash || slash == path || strcasecmp(slash + 1, "XMen2.exe") != 0)
    return 0;
  if (!x2::native::install_validate_executable(path, NULL, 0))
    return 0;
  length = (size_t)(slash - path);
  if (!length || length >= capacity)
    return 0;
  memcpy(directory, path, length);
  directory[length] = 0;
  return 1;
}

namespace {

int preference_path(char *path, size_t capacity) {
  const char *base = x2::config::config_directory();
  int written;
  if (!base || !x2::config::config_directory_ensure())
    return 0;
  written = snprintf(path, capacity, "%s/install-path.txt", base);
  return written > 0 && (size_t)written < capacity;
}

void SDLCALL file_dialog_callback(void *unused, const char *const *filelist,
                                  int filter) {
  (void)unused;
  (void)filter;
  g_dialog_status = 0;
  if (filelist && filelist[0] &&
      copy_string(g_selected, sizeof g_selected, filelist[0]))
    g_dialog_status = 1;
  else if (!filelist)
    g_dialog_status = -1;
  SDL_SetAtomicInt(&g_dialog_done, 1);
}

int choose_file(void) {
  static const SDL_DialogFileFilter filters[] = {
      {"X-Men Legends II executable", "exe"},
      {"ZIP archive", "zip"},
      {"All files", "*"},
  };
  SDL_SetAtomicInt(&g_dialog_done, 0);
  g_dialog_status = 0;
  g_selected[0] = 0;
  SDL_ShowOpenFileDialog(file_dialog_callback, NULL, NULL, filters, 3, NULL,
                         false);
  while (!SDL_GetAtomicInt(&g_dialog_done)) {
    SDL_PumpEvents();
    SDL_Delay(16);
  }
  return g_dialog_status;
}

int is_zip_path(const char *path) {
  const char *extension = strrchr(path, '.');
  return extension && strcasecmp(extension, ".zip") == 0;
}

int directory_from_folder(const char *folder, char *directory,
                          unsigned capacity, char *reason,
                          size_t reason_capacity) {
  std::error_code error;
  std::filesystem::path found;
  unsigned matches = 0;
  const std::filesystem::path root(folder ? folder : "");
  if (folder == nullptr || !std::filesystem::is_directory(root, error) ||
      error) {
    snprintf(reason, reason_capacity, "Cannot inspect the selected folder.");
    return 0;
  }
  for (std::filesystem::recursive_directory_iterator
           it(root, std::filesystem::directory_options::skip_permission_denied,
              error),
       end;
       it != end && !error; it.increment(error)) {
    if (!it->is_regular_file(error) || error) {
      error.clear();
      continue;
    }
    if (strcasecmp(it->path().filename().string().c_str(), "XMen2.exe") == 0) {
      found = it->path();
      if (++matches > 1)
        return 0;
    }
  }
  if (error) {
    snprintf(reason, reason_capacity, "Cannot inspect the selected folder.");
    return 0;
  }
  if (matches != 1) {
    snprintf(reason, reason_capacity,
             "That folder must contain exactly one XMen2.exe, including in its "
             "subfolders.");
    return 0;
  }
  const std::string path = found.string();
  if (!x2::native::install_validate_executable(path.c_str(), reason,
                                               reason_capacity))
    return 0;
  return install_picker_directory_from_executable(path.c_str(), directory,
                                                  capacity);
}

int directory_from_selection(const char *selection, char *directory,
                             unsigned capacity, char *reason,
                             size_t reason_capacity,
                             const char *archive_destination = nullptr) {
  std::error_code status_error;
  if (!selection || !*selection) {
    snprintf(reason, reason_capacity, "No XMen2.exe or ZIP was selected.");
    return 0;
  }
  if (selection && std::filesystem::is_directory(selection, status_error)) {
    return directory_from_folder(selection, directory, capacity, reason,
                                 reason_capacity);
  }
  if (status_error) {
    snprintf(reason, reason_capacity, "Cannot inspect the selected path: %s",
             status_error.message().c_str());
    return 0;
  }
  if (!is_zip_path(selection)) {
    if (!x2::native::install_validate_executable(selection, reason,
                                                 reason_capacity))
      return 0;
    if (install_picker_directory_from_executable(selection, directory,
                                                 capacity))
      return 1;
    snprintf(reason, reason_capacity,
             "That file is not XMen2.exe. Choose the executable or a ZIP "
             "containing exactly one XMen2.exe.");
    return 0;
  }

  char executable[X2_INSTALL_PATH_SIZE];
  const int extracted = archive_destination
                            ? x2::native::install_archive_prepare_to(
                                  selection, archive_destination, executable,
                                  sizeof executable, reason, reason_capacity)
                            : x2::native::install_archive_prepare(
                                  selection, executable, sizeof executable,
                                  reason, reason_capacity);
  if (!extracted || !install_picker_directory_from_executable(
                        executable, directory, capacity)) {
    if (!reason[0])
      snprintf(reason, reason_capacity,
               "That ZIP did not produce a usable XMen2.exe.");
    return 0;
  }
  return 1;
}

} // namespace

int install_picker_resolve_selection(const char *selection,
                                     const char *archive_destination,
                                     char *directory, unsigned capacity,
                                     char *reason, unsigned reason_capacity) {
  if (!directory || capacity < 2 || !reason || reason_capacity < 2)
    return 0;
  directory[0] = 0;
  reason[0] = 0;
  return directory_from_selection(selection, directory, capacity, reason,
                                  reason_capacity, archive_destination);
}

int install_picker_prepare_selection(const char *selection,
                                     const char *archive_destination,
                                     char *reason, unsigned reason_capacity) {
  char directory[X2_INSTALL_PATH_SIZE];
  return install_picker_resolve_selection(selection, archive_destination,
                                          directory, sizeof directory, reason,
                                          reason_capacity);
}

int install_picker_saved_directory(char *directory, unsigned capacity) {
  char path[X2_INSTALL_PATH_SIZE], line[X2_INSTALL_PATH_SIZE];
  FILE *file;
  if (!preference_path(path, sizeof path))
    return 0;
  file = fopen(path, "r");
  if (!file)
    return 0;
  if (!fgets(line, sizeof line, file)) {
    fclose(file);
    return 0;
  }
  fclose(file);
  line[strcspn(line, "\r\n")] = 0;
  if (!line[0])
    return 0;
  snprintf(path, sizeof path, "%s/XMen2.exe", line);
  return install_picker_directory_from_executable(path, directory, capacity);
}

void install_picker_remember_directory(const char *directory) {
  char path[X2_INSTALL_PATH_SIZE];
  FILE *file;
  if (!directory || !preference_path(path, sizeof path))
    return;
  file = fopen(path, "w");
  if (!file) {
    x2_log_error("install picker: could not remember installation: %s\n",
                 strerror(errno));
    return;
  }
  fprintf(file, "%s\n", directory);
  fclose(file);
}

int install_picker_browse(char *selection, unsigned capacity) {
  const int chosen = choose_file();
  if (chosen != 1)
    return chosen;
  return copy_string(selection, capacity, g_selected) ? 1 : -1;
}

} // namespace x2::native
