#include "install_archive.h"
#include "install_validation.h"
#include "../config/config_directory.h"

#include <cstdio>
#include <cstring>
#include <filesystem>
#include <string>

#include <lucent/zip.h>

namespace {

bool copy_path(const std::filesystem::path &source, char *destination,
               unsigned capacity)
{
    const std::string text = source.string();
    if (!destination || text.empty() || text.size() >= capacity) return false;
    std::memcpy(destination, text.c_str(), text.size() + 1);
    return true;
}

bool clean_tree(const std::filesystem::path &path, std::string &error)
{
    std::error_code status;
    std::filesystem::remove_all(path, status);
    if (!status) return true;
    error = "cannot clean " + path.filename().string() + ": " +
            status.message();
    return false;
}

bool restore_interrupted_swap(const std::filesystem::path &destination,
                              const std::filesystem::path &previous,
                              std::string &error)
{
    std::error_code status;
    const bool destination_exists = std::filesystem::exists(destination, status);
    if (status) {
        error = "cannot inspect the previous ZIP extraction: " +
                status.message();
        return false;
    }
    if (destination_exists) return true;
    const bool previous_exists = std::filesystem::exists(previous, status);
    if (status) {
        error = "cannot inspect the ZIP extraction backup: " + status.message();
        return false;
    }
    if (!previous_exists) return true;
    std::filesystem::rename(previous, destination, status);
    if (!status) return true;
    error = "cannot restore the previous ZIP extraction: " + status.message();
    return false;
}

bool accept_preparation(const std::filesystem::path &preparing,
                        const std::filesystem::path &destination,
                        const std::filesystem::path &previous,
                        std::string &error)
{
    if (!clean_tree(previous, error)) return false;

    std::error_code status;
    bool moved_previous = false;
    if (std::filesystem::exists(destination, status)) {
        std::filesystem::rename(destination, previous, status);
        moved_previous = !status;
    }
    if (status) {
        error = "cannot preserve the previous ZIP extraction: " +
                status.message();
        return false;
    }

    std::filesystem::rename(preparing, destination, status);
    if (status) {
        std::error_code restore_error;
        if (moved_previous)
            std::filesystem::rename(previous, destination, restore_error);
        error = "cannot accept the prepared ZIP extraction: " +
                status.message();
        if (restore_error)
            error += "; the previous extraction could not be restored: " +
                     restore_error.message();
        return false;
    }

    if (moved_previous) {
        std::error_code cleanup_error;
        std::filesystem::remove_all(previous, cleanup_error);
        if (cleanup_error)
            std::fprintf(stderr,
                         "install picker: accepted ZIP but could not remove "
                         "its previous extraction: %s\n",
                         cleanup_error.message().c_str());
    }
    return true;
}

} // namespace

extern "C" int x2_install_archive_prepare_to(const char *archive,
                                              const char *destination_text,
                                              char *executable,
                                              unsigned executable_capacity,
                                              char *reason,
                                              unsigned reason_capacity)
{
    if (!archive || !*archive || !destination_text || !*destination_text ||
        !executable || executable_capacity < 2 ||
        !reason || reason_capacity < 2) return 0;
    executable[0] = 0;
    reason[0] = 0;

    const std::filesystem::path destination(destination_text);
    const std::filesystem::path preparing = destination.string() + ".preparing";
    const std::filesystem::path previous = destination.string() + ".previous";

    std::string error;
    if (!clean_tree(preparing, error) ||
        !restore_interrupted_swap(destination, previous, error)) {
        std::snprintf(reason, reason_capacity, "%s", error.c_str());
        return 0;
    }

    std::filesystem::path prepared_executable;
    if (!lucent::zip::extract_install(archive, preparing, "XMen2.exe",
                                      prepared_executable, error)) {
        std::string cleanup_error;
        clean_tree(preparing, cleanup_error);
        if (!cleanup_error.empty()) error += "; " + cleanup_error;
        std::snprintf(reason, reason_capacity,
                      "That ZIP could not be used: %s", error.c_str());
        return 0;
    }
    if (!x2_install_validate_executable(prepared_executable.string().c_str(),
                                        reason, reason_capacity)) {
        std::string cleanup_error;
        clean_tree(preparing, cleanup_error);
        if (!cleanup_error.empty()) {
            const size_t used = std::strlen(reason);
            std::snprintf(reason + used, reason_capacity > used ? reason_capacity - used : 0,
                          "; %s", cleanup_error.c_str());
        }
        return 0;
    }

    const std::filesystem::path relative =
        prepared_executable.lexically_relative(preparing);
    if (relative.empty() || relative.is_absolute() ||
        relative.begin() == relative.end() || *relative.begin() == "..") {
        clean_tree(preparing, error);
        std::snprintf(reason, reason_capacity,
                      "That ZIP produced an invalid executable path.");
        return 0;
    }
    if (!copy_path(destination / relative, executable, executable_capacity)) {
        clean_tree(preparing, error);
        std::snprintf(reason, reason_capacity,
                      "The extracted executable path is too long.");
        return 0;
    }
    if (!accept_preparation(preparing, destination, previous, error)) {
        executable[0] = 0;
        std::snprintf(reason, reason_capacity, "%s", error.c_str());
        return 0;
    }
    return 1;
}

extern "C" int x2_install_archive_prepare(const char *archive,
                                           char *executable,
                                           unsigned executable_capacity,
                                           char *reason,
                                           unsigned reason_capacity)
{
    const char *base = x2_config_directory();
    if (!base || !x2_config_directory_ensure()) {
        std::snprintf(reason, reason_capacity,
                      "The user configuration directory could not be created.");
        return 0;
    }
    const std::filesystem::path destination =
        std::filesystem::path(base) / "xmen2-install";
    return x2_install_archive_prepare_to(archive, destination.string().c_str(),
                                         executable, executable_capacity,
                                         reason, reason_capacity);
}
