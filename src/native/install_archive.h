#ifndef X2_INSTALL_ARCHIVE_H
#define X2_INSTALL_ARCHIVE_H

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
                                  char *executable, unsigned executable_capacity,
                                  char *reason, unsigned reason_capacity);

#ifdef __cplusplus
}
#endif

#endif /* X2_INSTALL_ARCHIVE_H */
