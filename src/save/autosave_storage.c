#define _POSIX_C_SOURCE 200809L

#include "autosave_storage.h"

#include <errno.h>
#include <fcntl.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

static int write_complete(int fd, const void *data, size_t size)
{
    const unsigned char *bytes = data;
    while (size) {
        ssize_t written = write(fd, bytes, size);
        if (written < 0 && errno == EINTR) continue;
        if (written <= 0) return 0;
        bytes += (size_t)written;
        size -= (size_t)written;
    }
    return 1;
}

static int injected(X2AutosaveStorageFault requested,
                    X2AutosaveStorageFault point)
{
    if (requested != point) return 0;
    errno = EIO;
    return 1;
}

int x2_autosave_storage_publish(const char *directory,
                                const void *header,
                                const void *payload, size_t payload_size,
                                X2AutosaveStorageFault fault)
{
    static unsigned long sequence;
    unsigned char size_le[4];
    char temporary[96];
    uint32_t payload_u32;
    int directory_fd = -1;
    int file_fd = -1;
    int published = 0;
    int failed = 0;
    int saved_errno = 0;

    if (!directory || !directory[0] || !header
        || (!payload && payload_size) || payload_size > UINT32_MAX
        || fault < X2_AUTOSAVE_FAULT_NONE
        || fault > X2_AUTOSAVE_FAULT_BEFORE_RENAME) {
        errno = EINVAL;
        return 0;
    }
    directory_fd = open(directory, O_RDONLY | O_DIRECTORY | O_CLOEXEC);
    if (directory_fd < 0) return 0;
    snprintf(temporary, sizeof temporary, ".autosave.save.tmp.%ld.%lu",
             (long)getpid(), ++sequence);
    file_fd = openat(directory_fd, temporary,
                     O_WRONLY | O_CREAT | O_EXCL | O_CLOEXEC, 0600);
    if (file_fd < 0) {
        failed = 1;
        goto done;
    }

    payload_u32 = (uint32_t)payload_size;
    size_le[0] = (unsigned char)payload_u32;
    size_le[1] = (unsigned char)(payload_u32 >> 8);
    size_le[2] = (unsigned char)(payload_u32 >> 16);
    size_le[3] = (unsigned char)(payload_u32 >> 24);
    if (!write_complete(file_fd, header, X2_SAVE_HEADER_BYTES)
        || injected(fault, X2_AUTOSAVE_FAULT_AFTER_HEADER)
        || !write_complete(file_fd, size_le, sizeof size_le)
        || injected(fault, X2_AUTOSAVE_FAULT_AFTER_LENGTH)
        || !write_complete(file_fd, payload, payload_size)
        || injected(fault, X2_AUTOSAVE_FAULT_AFTER_PAYLOAD)
        || fsync(file_fd) != 0
        || injected(fault, X2_AUTOSAVE_FAULT_AFTER_FILE_SYNC)) {
        failed = 1;
        goto done;
    }
    if (close(file_fd) != 0) {
        file_fd = -1;
        failed = 1;
        goto done;
    }
    file_fd = -1;
    if (injected(fault, X2_AUTOSAVE_FAULT_BEFORE_RENAME)
        || renameat(directory_fd, temporary,
                    directory_fd, X2_AUTOSAVE_LEAF) != 0) {
        failed = 1;
        goto done;
    }
    published = 1;
    if (fsync(directory_fd) != 0) failed = 1;

done:
    if (failed) saved_errno = errno ? errno : EIO;
    if (file_fd >= 0 && close(file_fd) != 0 && !saved_errno)
        saved_errno = errno;
    if (!published) unlinkat(directory_fd, temporary, 0);
    if (directory_fd >= 0 && close(directory_fd) != 0 && !saved_errno)
        saved_errno = errno;
    if (!published || failed || saved_errno) {
        errno = saved_errno ? saved_errno : EIO;
        return 0;
    }
    return 1;
}
