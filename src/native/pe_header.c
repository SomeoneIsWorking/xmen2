#include "pe_header.h"

#include <stdint.h>
#include <stdio.h>
#include <string.h>

static int refuse(char *reason, unsigned capacity, const char *message) {
  if (reason && capacity)
    snprintf(reason, capacity, "%s", message);
  return 0;
}

static uint16_t little16(const unsigned char *bytes) {
  return (uint16_t)bytes[0] | (uint16_t)((uint16_t)bytes[1] << 8);
}

static uint32_t little32(const unsigned char *bytes) {
  return (uint32_t)bytes[0] | ((uint32_t)bytes[1] << 8) |
         ((uint32_t)bytes[2] << 16) | ((uint32_t)bytes[3] << 24);
}

static int read_at(FILE *file, long offset, void *destination, size_t size) {
  return fseek(file, offset, SEEK_SET) == 0 &&
         fread(destination, 1, size, file) == size;
}

int x2_pe32_validate_file(const char *path, char *reason,
                          unsigned reason_capacity) {
  FILE *file;
  unsigned char dos[64], header[24], optional_magic[2];
  long file_size;
  uint32_t pe_offset;
  uint16_t machine, optional_size;

  if (reason && reason_capacity)
    reason[0] = 0;
  if (!path || !*path)
    return refuse(reason, reason_capacity, "Executable path is empty.");
  file = fopen(path, "rb");
  if (!file)
    return refuse(reason, reason_capacity, "Executable cannot be opened.");
  if (fseek(file, 0, SEEK_END) != 0 || (file_size = ftell(file)) < 64 ||
      !read_at(file, 0, dos, sizeof dos)) {
    fclose(file);
    return refuse(reason, reason_capacity,
                  "Executable is too small for a DOS header.");
  }
  if (dos[0] != 'M' || dos[1] != 'Z') {
    fclose(file);
    return refuse(reason, reason_capacity, "Executable has no DOS signature.");
  }
  pe_offset = little32(dos + 0x3c);
  if ((uint64_t)pe_offset + sizeof header > (uint64_t)file_size ||
      !read_at(file, (long)pe_offset, header, sizeof header)) {
    fclose(file);
    return refuse(reason, reason_capacity, "Executable has no PE header.");
  }
  if (memcmp(header, "PE\0\0", 4) != 0) {
    fclose(file);
    return refuse(reason, reason_capacity, "Executable has no PE signature.");
  }
  machine = little16(header + 4);
  optional_size = little16(header + 20);
  if (machine != 0x014c) {
    fclose(file);
    return refuse(reason, reason_capacity,
                  "Executable is not an x86 PE image.");
  }
  if (optional_size < 2 ||
      (uint64_t)pe_offset + 24u + optional_size > (uint64_t)file_size ||
      !read_at(file, (long)pe_offset + 24, optional_magic,
               sizeof optional_magic)) {
    fclose(file);
    return refuse(reason, reason_capacity,
                  "Executable has an incomplete PE32 header.");
  }
  fclose(file);
  if (little16(optional_magic) != 0x010b)
    return refuse(reason, reason_capacity, "Executable is not a PE32 image.");
  return 1;
}
