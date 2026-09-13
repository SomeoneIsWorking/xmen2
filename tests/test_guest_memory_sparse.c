#include "guest_file_io.h"
#include "guest_memory.h"
#include "x86rt.h"
#include "x86rt_native.h"

#include <errno.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>

static unsigned checks, failures, invalidations;
volatile uint32_t x2_write_watch_addr;

void x2_write_watch_fire(uint32_t address, uint32_t value) {
  (void)address;
  (void)value;
}

void x2_engine_invalidate_memory(uint32_t address, uint32_t size) {
  if (!size || (uint64_t)address + size > (UINT64_C(1) << 32))
    abort();
  invalidations++;
}

void x2_log_error(const char *format, ...) {
  va_list args;
  va_start(args, format);
  vfprintf(stderr, format, args);
  va_end(args);
}

static void check(int result, const char *what) {
  checks++;
  if (!result) {
    failures++;
    fprintf(stderr, "FAIL: %s\n", what);
  }
}

int main(void) {
  const uint32_t base = 0xa0000000u;
  uint32_t value = 0;
  uint32_t address = 0;
  uint8_t *host;
  check(guest_memory_init() == 0 && guest_memory_init() == 0,
        "initialize sparse memory once");
  check(guest_memory_map_fixed(base, 12288, PROT_NONE) == 0,
        "reserve exact high guest range");
  check(!guest_memory_is_readable(base, 1) && !x86_peek32(base, &value),
        "reserved pages refuse diagnostic reads without trapping");
  check(guest_memory_protect(base, 12288, PROT_READ | PROT_WRITE) == 0,
        "commit full reserved allocation");
  host = guest_memory_pointer(base);
  x86_store32_raw(base + 4095, 0x76543210);
  check(x86_load32(base + 4095) == 0x76543210,
        "native unaligned ABI load/store uses sparse backing");
  check(guest_memory_host_address(host + 17, &address) &&
            address == base + 17 && guest_memory_address(host + 17) == address,
        "native pointer round-trips through shared mapper");
  check(guest_memory_protect(base + 4096, 4096, PROT_NONE) == 0,
        "decommit middle page");
  check(!x86_peek32(base + 4095, &value) && host[4095] == 0x10,
        "checked diagnostic span refuses decommitted crossing");
  check(guest_memory_is_readable(base, 4096) &&
            guest_memory_is_readable(base + 8192, 4096),
        "neighboring pages remain readable");
  check(guest_memory_protect(base + 4096, 4096, PROT_READ | PROT_WRITE) == 0 &&
            x86_load32(base + 4095) == 0x76543210,
        "recommit preserves existing bytes");
  check(guest_memory_map_fixed(base + 4096, 4096, PROT_READ) == -1 &&
            errno == EEXIST,
        "mapped allocation overlap refuses");
  check(guest_memory_release(base + 4096, 4096) == 0,
        "release middle page while retaining allocation neighbors");
  check(!guest_memory_is_readable(base + 4096, 1),
        "partial release leaves hole");
  check(guest_memory_map_fixed(base + 4096, 4096, PROT_READ | PROT_WRITE) == 0,
        "new allocation can occupy released guest hole");
  x86_store64_raw(base + 4092, UINT64_C(0x1122334455667788));
  check(x86_load64(base + 4092) == UINT64_C(0x1122334455667788),
        "native 64-bit ABI accesses cross independently allocated spans");
  check(guest_memory_release(base, 12288) == 0 &&
            !guest_memory_host_address(host, &address),
        "full release drops all borrowed host allocation references");
  check(!x86_peek32(base, &value), "released diagnostic read refuses");
  check(guest_memory_map_fixed(base, 32768, PROT_READ | PROT_WRITE) == 0,
        "map bulk file-sized guest buffer");
  host = guest_memory_pointer(base);
  memcpy(host + 0x6038, "FONT_TABLE", sizeof "FONT_TABLE");
  char bulk_tail[sizeof "FONT_TABLE"] = {0};
  check(guest_memory_try_read(base + 0x6038, bulk_tail, sizeof bulk_tail) &&
            memcmp(bulk_tail, "FONT_TABLE", sizeof bulk_tail) == 0,
        "bulk host write remains visible to sparse guest near buffer end");
  check(guest_memory_release(base, 32768) == 0,
        "release bulk file-sized guest buffer");
  {
    enum { payload_size = 28272 };
    unsigned char *payload = malloc(payload_size);
    unsigned char *restored = malloc(payload_size);
    char font_name[sizeof "FONT_TABLE"] = {0};
    FILE *input = tmpfile();
    FILE *output = tmpfile();
    int mapped = 1;
    for (unsigned i = 0; i < 8; ++i) {
      if (guest_memory_map_fixed(base + i * 4096u, 4096,
                                 PROT_READ | PROT_WRITE) != 0) {
        mapped = 0;
      }
    }
    check(mapped && input && output && payload && restored,
          "prepare adjacent guest pages backed by separate allocations");
    if (mapped && input && output && payload && restored) {
      for (size_t i = 0; i < payload_size; ++i) {
        payload[i] = (unsigned char)(i * 37u + 11u);
      }
      memcpy(payload + 0x6038, "FONT_TABLE", sizeof "FONT_TABLE");
      check(fwrite(payload, 1, payload_size, input) == payload_size &&
                fseek(input, 0, SEEK_SET) == 0,
            "write synthetic font-sized source file");
      check(x2_guest_fread(base + 8, 1, payload_size, input) == payload_size &&
                guest_memory_try_read(base + 8 + 0x6038, font_name,
                                      sizeof font_name) &&
                memcmp(font_name, "FONT_TABLE", sizeof font_name) == 0,
            "stdio read reaches later disjoint guest page");
      check(x2_guest_fwrite(base + 8, 1, payload_size, output) ==
                    payload_size &&
                fseek(output, 0, SEEK_SET) == 0 &&
                fread(restored, 1, payload_size, output) == payload_size &&
                memcmp(restored, payload, payload_size) == 0,
            "stdio write gathers later disjoint guest page");
      check(fseek(input, 0, SEEK_SET) == 0 &&
                x2_guest_read_fd(fileno(input), base + 8, payload_size) ==
                    (ssize_t)payload_size,
            "file-descriptor read reaches later disjoint guest page");
      check(fseek(output, 0, SEEK_END) == 0 &&
                x2_guest_write_fd(fileno(output), base + 8, payload_size) ==
                    (ssize_t)payload_size &&
                fseek(output, payload_size, SEEK_SET) == 0 &&
                fread(restored, 1, payload_size, output) == payload_size &&
                memcmp(restored, payload, payload_size) == 0,
            "file-descriptor write gathers later disjoint guest page");
    }
    if (input) {
      fclose(input);
    }
    if (output) {
      fclose(output);
    }
    free(payload);
    free(restored);
    check(guest_memory_release(base, 32768) == 0,
          "release disjoint file buffer pages");
  }
  check(guest_memory_map_fixed(0xfffff000u, 4096, PROT_READ | PROT_WRITE) == 0,
        "final guest page maps without a 4GiB host arena");
  x86_store8_raw(UINT32_MAX, 0x91);
  check(x86_load8(UINT32_MAX) == 0x91 && !x86_peek32(UINT32_MAX, &value),
        "final byte works and wrapped diagnostic read refuses");
  check(guest_memory_release(0xfffff000u, 4096) == 0,
        "release final guest page");
  check(guest_memory_map_any(base, base + 8192, 4096, 4096,
                             PROT_READ | PROT_WRITE, &address) == 0 &&
            address == base,
        "map-any preserves requested guest range and alignment");
  check(guest_memory_release(address, 4096) == 0, "release map-any allocation");
  check(invalidations >= 10, "mapping changes notify execution invalidation");
  printf("guest sparse memory: %u checks, %u failures, %u invalidations\n",
         checks, failures, invalidations);
  return failures ? 1 : 0;
}
