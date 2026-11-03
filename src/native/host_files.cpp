#include "host_files.h"

#include "host_dir_cache.h"

#include "platform_dirent.h"
#include "platform_posix.h"
#include <algorithm>
#include <cctype>
#include <cerrno>
#include <cstring>
#include <fcntl.h>
#include <string_view>
#include <utility>

namespace x2::native {

class HostDirectory {
public:
  std::vector<std::string> names;
  size_t next = 0;
};

namespace {

/* At most one mounted tree; Android mounts the player's install folder. */
class Mounts {
public:
  void mount(const char *root, std::unique_ptr<HostFileTree> tree) {
    root_ = root ? root : "";
    while (root_.size() > 1 && root_.back() == '/') {
      root_.pop_back();
    }
    tree_ = std::move(tree);
  }

  /* The tree holding `path` and its normalized path relative to the root, or
   * nullptr. "." and ".." resolve as POSIX does; ".." at the root stays there.
   */
  HostFileTree *find(const char *path, std::string &relative) const {
    if (!tree_ || !path) {
      return nullptr;
    }
    const size_t size = root_.size();
    if (std::strncmp(path, root_.c_str(), size) != 0 ||
        (path[size] != '\0' && path[size] != '/')) {
      return nullptr;
    }
    relative.clear();
    const char *component = path + size;
    while (*component != '\0') {
      const char *end = std::strchr(component, '/');
      const size_t length =
          end ? static_cast<size_t>(end - component) : std::strlen(component);
      const std::string_view name(component, length);
      if (name == "..") {
        const size_t parent = relative.rfind('/');
        relative.erase(parent == std::string::npos ? 0 : parent);
      } else if (!name.empty() && name != ".") {
        if (!relative.empty()) {
          relative.push_back('/');
        }
        relative.append(name);
      }
      component += length;
      if (*component == '/') {
        component++;
      }
    }
    return tree_.get();
  }

private:
  std::string root_;
  std::unique_ptr<HostFileTree> tree_;
};

Mounts g_mounts;

int answer(int result) {
  if (result >= 0) {
    return result;
  }
  errno = -result;
  return -1;
}

/* fopen's mode string as open(2) flags. */
int fopen_flags(const char *mode) {
  const bool plus = std::strchr(mode, '+') != nullptr;
  switch (mode[0]) {
  case 'r':
    return plus ? O_RDWR : O_RDONLY;
  case 'w':
    return (plus ? O_RDWR : O_WRONLY) | O_CREAT | O_TRUNC;
  case 'a':
    return (plus ? O_RDWR : O_WRONLY) | O_CREAT | O_APPEND;
  default:
    return -1;
  }
}

/* NTFS lists "." and ".." first, then names by upper-cased code unit. Alchemy's
 * igPluginHelper::loadAllPlugins loads libraries in this order. */
void ntfs_order(std::vector<std::string> &names) {
  std::erase_if(names, [](const std::string &name) {
    return name == "." || name == "..";
  });
  std::sort(names.begin(), names.end(),
            [](const std::string &left, const std::string &right) {
              return std::lexicographical_compare(
                  left.begin(), left.end(), right.begin(), right.end(),
                  [](char a, char b) {
                    return std::toupper(static_cast<unsigned char>(a)) <
                           std::toupper(static_cast<unsigned char>(b));
                  });
            });
  names.insert(names.begin(), {".", ".."});
}

} // namespace

void host_files_mount(const char *root, std::unique_ptr<HostFileTree> tree) {
  g_mounts.mount(root, std::move(tree));
  host_dir_clear();
}

int host_open(const char *path, int flags, int mode) {
  std::string relative;
  if (HostFileTree *tree = g_mounts.find(path, relative)) {
    return answer(tree->open(relative, flags));
  }
  return open(path, flags, mode);
}

int host_stat(const char *path, struct stat *out) {
  std::string relative;
  if (HostFileTree *tree = g_mounts.find(path, relative)) {
    return answer(tree->stat(relative, out));
  }
  return stat(path, out);
}

#if !defined(_WIN32)
int host_lstat(const char *path, struct stat *out) {
  std::string relative;
  if (HostFileTree *tree = g_mounts.find(path, relative)) {
    return answer(tree->stat(relative, out));
  }
  return lstat(path, out);
}
#endif

FILE *host_fopen(const char *path, const char *mode) {
  std::string relative;
  HostFileTree *tree = g_mounts.find(path, relative);
  if (!tree) {
    return fopen(path, mode);
  }
  const int flags = mode ? fopen_flags(mode) : -1;
  if (flags < 0) {
    errno = EINVAL;
    return nullptr;
  }
  const int descriptor = answer(tree->open(relative, flags));
  if (descriptor < 0) {
    return nullptr;
  }
  FILE *file = fdopen(descriptor, mode);
  if (!file) {
    const int saved = errno;
    close(descriptor);
    errno = saved;
  }
  return file;
}

int host_mkdir(const char *path, int mode) {
  std::string relative;
  if (HostFileTree *tree = g_mounts.find(path, relative)) {
    return answer(tree->make_directory(relative));
  }
  return mkdir(path, mode);
}

int host_unlink(const char *path) {
  std::string relative;
  if (HostFileTree *tree = g_mounts.find(path, relative)) {
    return answer(tree->remove(relative, false));
  }
  return unlink(path);
}

int host_rmdir(const char *path) {
  std::string relative;
  if (HostFileTree *tree = g_mounts.find(path, relative)) {
    return answer(tree->remove(relative, true));
  }
  return rmdir(path);
}

int host_replace(const char *from, const char *to) {
  std::string relative_from;
  std::string relative_to;
  HostFileTree *source = g_mounts.find(from, relative_from);
  HostFileTree *target = g_mounts.find(to, relative_to);
  if (source != target) {
    errno = EXDEV;
    return -1;
  }
  if (source) {
    return answer(source->replace(relative_from, relative_to));
  }
  return x2_replace_file(from, to);
}

int host_sync_directory(const char *path) {
  std::string relative;
  if (g_mounts.find(path, relative)) {
    return 0;
  }
#if defined(_WIN32)
  return 0;
#else
  const int descriptor = open(path, O_RDONLY | O_DIRECTORY | O_CLOEXEC);
  if (descriptor < 0) {
    return -1;
  }
  const int synced = fsync(descriptor);
  const int saved = errno;
  close(descriptor);
  errno = saved;
  return synced;
#endif
}

HostDirectory *host_opendir(const char *path) {
  auto directory = std::make_unique<HostDirectory>();
  std::string relative;
  if (HostFileTree *tree = g_mounts.find(path, relative)) {
    if (answer(tree->list(relative, directory->names)) < 0) {
      return nullptr;
    }
  } else {
    DIR *posix = opendir(path);
    if (!posix) {
      return nullptr;
    }
    errno = 0;
    for (struct dirent *entry = readdir(posix); entry; entry = readdir(posix)) {
      directory->names.emplace_back(entry->d_name);
    }
    const int saved = errno;
    closedir(posix);
    if (saved != 0) {
      errno = saved;
      return nullptr;
    }
  }
  ntfs_order(directory->names);
  return directory.release();
}

const char *host_readdir(HostDirectory *directory) {
  if (!directory || directory->next >= directory->names.size()) {
    return nullptr;
  }
  return directory->names[directory->next++].c_str();
}

void host_closedir(HostDirectory *directory) { delete directory; }

} // namespace x2::native
