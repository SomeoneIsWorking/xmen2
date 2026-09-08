#include "platform_file_map.h"

#include <stdint.h>

#if defined(_WIN32)

#include <windows.h>

int x2_file_map_readonly(const char *path, X2FileMap *out) {
  HANDLE file, section;
  LARGE_INTEGER size;
  void *address;

  out->address = NULL;
  out->size = 0;
  file = CreateFileA(path, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING,
                     FILE_ATTRIBUTE_NORMAL, NULL);
  if (file == INVALID_HANDLE_VALUE)
    return -1;
  if (!GetFileSizeEx(file, &size) || size.QuadPart < 0 ||
      (unsigned long long)size.QuadPart > (unsigned long long)SIZE_MAX) {
    CloseHandle(file);
    return -1;
  }
  section = CreateFileMappingA(file, NULL, PAGE_READONLY, 0, 0, NULL);
  CloseHandle(file);
  if (section == NULL)
    return -1;
  address = MapViewOfFile(section, FILE_MAP_READ, 0, 0, 0);
  CloseHandle(section);
  if (address == NULL)
    return -1;
  out->address = address;
  out->size = (size_t)size.QuadPart;
  return 0;
}

void x2_file_unmap(X2FileMap *mapping) {
  if (mapping->address != NULL)
    (void)UnmapViewOfFile(mapping->address);
  mapping->address = NULL;
  mapping->size = 0;
}

#else

#include <errno.h>
#include <fcntl.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <unistd.h>

int x2_file_map_readonly(const char *path, X2FileMap *out) {
  struct stat stat_info;
  int descriptor;
  void *address;

  out->address = NULL;
  out->size = 0;
  descriptor = open(path, O_RDONLY);
  if (descriptor < 0)
    return -1;
  if (fstat(descriptor, &stat_info) != 0 || stat_info.st_size < 0) {
    close(descriptor);
    return -1;
  }
  address = mmap(NULL, (size_t)stat_info.st_size, PROT_READ, MAP_PRIVATE,
                 descriptor, 0);
  close(descriptor);
  if (address == MAP_FAILED)
    return -1;
  out->address = address;
  out->size = (size_t)stat_info.st_size;
  return 0;
}

void x2_file_unmap(X2FileMap *mapping) {
  if (mapping->address != NULL)
    (void)munmap(mapping->address, mapping->size);
  mapping->address = NULL;
  mapping->size = 0;
}

#endif
