#pragma once

#include <cstdint>

namespace x2::native {

/* Extract a selected ZIP into a prepared user-data tree, validate the unique
 * XMen2.exe, and atomically replace the earlier accepted ZIP extraction. The
 * previous valid tree survives every preparation failure. */
int install_archive_prepare(const char *archive, char *executable,
                            unsigned executable_capacity, char *reason,
                            unsigned reason_capacity);

/* As above, but lets a platform-owned transaction choose an adjacent private
 * destination. The caller supplies a fresh leaf and only publishes it after
 * the complete title install has validated. */
int install_archive_prepare_to(const char *archive, const char *destination,
                               char *executable, unsigned executable_capacity,
                               char *reason, unsigned reason_capacity);

/* Browser OPFS cannot rename directories. Extract into a fresh, unpublished
 * directory; the web owner marks it ready only after title validation passes.
 */
using InstallArchiveProgress = void (*)(uint64_t expanded_bytes,
                                        uint64_t total_expanded_bytes,
                                        void *context);
int install_archive_extract_unpublished(
    const char *archive, const char *destination, char *executable,
    unsigned executable_capacity, char *reason, unsigned reason_capacity,
    InstallArchiveProgress progress, void *progress_context);

} // namespace x2::native
