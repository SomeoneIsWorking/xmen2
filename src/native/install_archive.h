#ifndef X2_INSTALL_ARCHIVE_H
#define X2_INSTALL_ARCHIVE_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Extract a selected ZIP into a prepared user-data tree, validate the unique
 * XMen2.exe, and atomically replace the earlier accepted ZIP extraction. The
 * previous valid tree survives every preparation failure. */
int x2_install_archive_prepare(const char *archive, char *executable,
                               unsigned executable_capacity, char *reason,
                               unsigned reason_capacity);

/* As above, but lets a platform-owned transaction choose an adjacent private
 * destination. The caller supplies a fresh leaf and only publishes it after
 * the complete title install has validated. */
int x2_install_archive_prepare_to(const char *archive, const char *destination,
                                  char *executable,
                                  unsigned executable_capacity, char *reason,
                                  unsigned reason_capacity);

/* Browser OPFS cannot rename directories. Extract into a fresh, unpublished
 * directory; the web owner marks it ready only after title validation passes.
 */
typedef void (*x2_install_archive_progress)(uint64_t expanded_bytes,
                                            uint64_t total_expanded_bytes,
                                            void *context);
int x2_install_archive_extract_unpublished(
    const char *archive, const char *destination, char *executable,
    unsigned executable_capacity, char *reason, unsigned reason_capacity,
    x2_install_archive_progress progress, void *progress_context);

#ifdef __cplusplus
}
#endif

#endif /* X2_INSTALL_ARCHIVE_H */
