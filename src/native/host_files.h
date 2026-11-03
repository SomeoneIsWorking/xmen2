/* Host file access for resolved paths: the one layer between a host path and
 * the storage that holds it. A path under a mounted tree goes to that tree's
 * backend (an Android document tree); every other path is the host's own
 * filesystem. Calls follow POSIX: -1 with errno set on failure. */
#pragma once

#include <cstdio>
#include <memory>
#include <string>
#include <sys/stat.h>
#include <vector>

namespace x2::native {

/* A tree that is not reachable through POSIX paths. Paths are relative to the
 * mount root without a leading slash; "" is the root. Results are 0 (or a
 * descriptor the caller owns) on success and -errno on failure. */
class HostFileTree {
public:
  virtual ~HostFileTree() = default;
  virtual int open(const std::string &relative, int flags) = 0;
  virtual int stat(const std::string &relative, struct stat *out) = 0;
  virtual int list(const std::string &relative,
                   std::vector<std::string> &names) = 0;
  virtual int make_directory(const std::string &relative) = 0;
  virtual int remove(const std::string &relative, bool directory) = 0;
  virtual int replace(const std::string &from, const std::string &to) = 0;
};

/* Mounts `tree` at the absolute path `root`, replacing an earlier mount there,
 * and drops every cached directory listing. */
void host_files_mount(const char *root, std::unique_ptr<HostFileTree> tree);

int host_open(const char *path, int flags, int mode);
int host_stat(const char *path, struct stat *out);
#if !defined(_WIN32)
/* As host_stat, but a host symlink is described, not followed; trees have none.
 */
int host_lstat(const char *path, struct stat *out);
#endif
FILE *host_fopen(const char *path, const char *mode);
int host_mkdir(const char *path, int mode);
int host_unlink(const char *path);
int host_rmdir(const char *path);
/* Moves the complete file `from` over `to`. A crash leaves the old or the new
 * `to`, never neither: a tree that cannot rename over a file goes through
 * `<to>.new`, which it finishes on the next access. */
int host_replace(const char *from, const char *to);
/* Makes a completed rename in `path` durable where the host can. */
int host_sync_directory(const char *path);

/* A directory's names, read once when it is opened. */
class HostDirectory;
HostDirectory *host_opendir(const char *path);
const char *host_readdir(HostDirectory *directory);
void host_closedir(HostDirectory *directory);

} // namespace x2::native
