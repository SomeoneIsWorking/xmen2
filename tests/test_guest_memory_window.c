/*
 * The guest memory owner on a host with no VM of its own.
 *
 * Built with X2_GUEST_ARENA_WINDOW=1 so the browser's owner runs here, where
 * it can be falsified: the browser cannot be single-stepped and a wrong page
 * table there shows up as the game reading somebody else's bytes hours later.
 *
 * The permission table is checked through guest_memory_window(), which is the
 * pointer the execution owner hands to x86port -- so what is checked is what
 * the generated code will read, not a second copy of the same rule.
 */
#include "guest_file_io.h"
#include "guest_layout.h"
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

static void remapped(uint32_t address, uint32_t size) {
  if (!size || (uint64_t)address + size > GUEST_LAYOUT_LIMIT)
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

/* The permission byte for one guest page, read exactly where x86port reads
   it. */
static unsigned page_permission(uint32_t address) {
  const GuestMemoryWindow window = guest_memory_window();
  return window.perms[address >> window.page_shift];
}

int main(void) {
  /* In the reservation window, which nothing else in the layout uses. */
  const uint32_t base = GUEST_RESERVE_LO;
  const uint32_t last_page = GUEST_LAYOUT_LIMIT - 4096u;
  uint32_t value = 0;
  uint32_t address = 0;
  unsigned before = 0;
  uint8_t *host;

  guest_memory_set_remap_observer(remapped);
  check(guest_memory_init() == 0 && guest_memory_init() == 0,
        "initialize the guest window once");
  {
    const GuestMemoryWindow window = guest_memory_window();
    check(window.host != NULL && window.size == GUEST_LAYOUT_LIMIT &&
              window.perms != NULL && window.page_shift == GUEST_PAGE_SHIFT,
          "the window spans the layout and carries a page table");
    check(page_permission(base) == 0,
          "a page nothing has mapped grants nothing");
  }

  check(guest_memory_map_fixed(base, 12288, PROT_NONE) == 0,
        "reserve exact guest range");
  check(page_permission(base) == 0,
        "a reserved page still grants nothing to generated code");
  check(!guest_memory_is_readable(base, 1) && !x86_peek32(base, &value),
        "reserved pages refuse diagnostic reads without trapping");
  check(guest_memory_protect(base, 12288, PROT_READ | PROT_WRITE) == 0,
        "commit full reserved allocation");
  check(page_permission(base) == (PROT_READ | PROT_WRITE) &&
            page_permission(base + 8192) == (PROT_READ | PROT_WRITE),
        "committing grants read and write on every page of the span");
  check(guest_memory_protect(base + 4096, 4096, PROT_READ) == 0 &&
            page_permission(base + 4096) == PROT_READ &&
            page_permission(base) == (PROT_READ | PROT_WRITE),
        "a read-only page loses write, and only that page");

  host = guest_memory_pointer(base);
  x86_store32_raw(base + 4095, 0x76543210);
  check(x86_load32(base + 4095) == 0x76543210,
        "native unaligned ABI load/store reaches the window");
  check(guest_memory_host_address(host + 17, &address) &&
            address == base + 17 && guest_memory_address(host + 17) == address,
        "native pointer round-trips through the shared mapper");
  before = invalidations;
  check(guest_memory_protect(base + 4096, 4096, PROT_NONE) == 0 &&
            page_permission(base + 4096) == 0,
        "decommit middle page");
  /* A decommit/recommit pair is how the game replaces what a span holds, so
     the execution owner has to hear about it even though generated code reads
     the permission table itself and would not need telling for the access
     check alone. Issue #157. */
  check(invalidations == before + 1, "decommit told the execution owner");
  check(!x86_peek32(base + 4095, &value) && host[4095] == 0x10,
        "checked diagnostic span refuses decommitted crossing");
  check(guest_memory_is_readable(base, 4096) &&
            guest_memory_is_readable(base + 8192, 4096),
        "neighboring pages remain readable");
  before = invalidations;
  check(guest_memory_protect(base + 4096, 4096, PROT_READ | PROT_WRITE) == 0 &&
            x86_load32(base + 4095) == 0x76543210,
        "recommit preserves existing bytes");
  check(invalidations == before + 1, "and the recommit told it too");
  check(guest_memory_map_fixed(base + 4096, 4096, PROT_READ) == -1 &&
            errno == EEXIST,
        "mapped allocation overlap refuses");

  /*
   * RELEASE AND REMAP. Nothing unmaps a window page, so without the owner
   * clearing it the guest would read the last tenant's bytes -- which Windows
   * never does, and which is the one thing a host that really unmaps gets for
   * free.
   */
  x86_store32_raw(base + 4096, 0xdeadbeefu);
  check(guest_memory_release(base + 4096, 4096) == 0,
        "release middle page while retaining its neighbours");
  check(!guest_memory_is_readable(base + 4096, 1) &&
            page_permission(base + 4096) == 0,
        "partial release leaves a hole");
  check(guest_memory_map_fixed(base + 4096, 4096, PROT_READ | PROT_WRITE) == 0,
        "a new allocation can occupy the released hole");
  check(x86_load32(base + 4096) == 0,
        "and reads zero, not what the previous mapping left there");
  check(page_permission(base + 4096) == (PROT_READ | PROT_WRITE),
        "the new mapping's permissions reach generated code");

  x86_store64_raw(base + 4092, UINT64_C(0x1122334455667788));
  check(x86_load64(base + 4092) == UINT64_C(0x1122334455667788),
        "native 64-bit ABI accesses cross a mapping boundary");
  check(guest_memory_release(base, 12288) == 0,
        "full release drops every page of the range");
  check(!x86_peek32(base, &value), "released diagnostic read refuses");

  check(guest_memory_map_fixed(base, 32768, PROT_READ | PROT_WRITE) == 0,
        "map a bulk file-sized guest buffer");
  host = guest_memory_pointer(base);
  memcpy(host + 0x6038, "FONT_TABLE", sizeof "FONT_TABLE");
  {
    char bulk_tail[sizeof "FONT_TABLE"] = {0};
    check(guest_memory_try_read(base + 0x6038, bulk_tail, sizeof bulk_tail) &&
              memcmp(bulk_tail, "FONT_TABLE", sizeof bulk_tail) == 0,
          "a bulk host write is visible near the buffer end");
  }
  {
    enum { payload_size = 28272 };
    unsigned char *payload = malloc(payload_size);
    unsigned char *restored = malloc(payload_size);
    char font_name[sizeof "FONT_TABLE"] = {0};
    FILE *input = tmpfile();
    FILE *output = tmpfile();
    check(input && output && payload && restored, "prepare the file fixtures");
    if (input && output && payload && restored) {
      for (size_t i = 0; i < payload_size; ++i) {
        payload[i] = (unsigned char)(i * 37u + 11u);
      }
      memcpy(payload + 0x6038, "FONT_TABLE", sizeof "FONT_TABLE");
      check(fwrite(payload, 1, payload_size, input) == payload_size &&
                fseek(input, 0, SEEK_SET) == 0,
            "write the synthetic font-sized source file");
      check(x2_guest_fread(base + 8, 1, payload_size, input) == payload_size &&
                guest_memory_try_read(base + 8 + 0x6038, font_name,
                                      sizeof font_name) &&
                memcmp(font_name, "FONT_TABLE", sizeof font_name) == 0,
            "stdio read reaches a later guest page");
      check(x2_guest_fwrite(base + 8, 1, payload_size, output) ==
                    payload_size &&
                fseek(output, 0, SEEK_SET) == 0 &&
                fread(restored, 1, payload_size, output) == payload_size &&
                memcmp(restored, payload, payload_size) == 0,
            "stdio write gathers a later guest page");
      check(fseek(input, 0, SEEK_SET) == 0 &&
                x2_guest_read_fd(fileno(input), base + 8, payload_size) ==
                    (ssize_t)payload_size,
            "file-descriptor read reaches a later guest page");
      check(fseek(output, 0, SEEK_END) == 0 &&
                x2_guest_write_fd(fileno(output), base + 8, payload_size) ==
                    (ssize_t)payload_size &&
                fseek(output, payload_size, SEEK_SET) == 0 &&
                fread(restored, 1, payload_size, output) == payload_size &&
                memcmp(restored, payload, payload_size) == 0,
            "file-descriptor write gathers a later guest page");
      errno = 0;
      check(x2_guest_fread(0, 1, 16, input) == 0 && errno == EFAULT,
            "a read into the null guest address is refused");
      errno = 0;
      check(x2_guest_write_fd(fileno(output), UINT32_MAX, 2) == -1 &&
                errno == EFAULT,
            "a write that runs past 4 GB is refused");
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
          "release the bulk file buffer");
  }

  /* The top of the window works, and everything above it is refused by name
     rather than reaching whatever the program has put there. */
  check(guest_memory_map_fixed(last_page, 4096, PROT_READ | PROT_WRITE) == 0,
        "the last page of the window maps");
  x86_store8_raw(GUEST_LAYOUT_LIMIT - 1u, 0x91);
  check(x86_load8(GUEST_LAYOUT_LIMIT - 1u) == 0x91,
        "and its final byte reads back");
  check(guest_memory_map_fixed(GUEST_LAYOUT_LIMIT, 4096,
                               PROT_READ | PROT_WRITE) == -1,
        "a page past the window refuses to map");
  check(!guest_memory_is_readable(GUEST_LAYOUT_LIMIT, 1) &&
            !x86_peek32(GUEST_LAYOUT_LIMIT, &value),
        "and is neither readable nor peekable");
  {
    int mapped = -1;
    check(guest_memory_run(last_page, &mapped) == 4096u && mapped == 1,
          "the last page is a mapped run of one, ending at the layout");
    check(guest_memory_run(last_page - 8192u, &mapped) == 8192u && mapped == 0,
          "a free run stops at the next mapped page, for VirtualQuery");
    check(guest_memory_run(GUEST_LAYOUT_LIMIT, &mapped) == 0u && mapped == 0,
          "nothing past the layout is ever offered as a run");
  }
  check(guest_memory_region_use(last_page, 0).top == GUEST_LAYOUT_LIMIT &&
            guest_memory_region_use(last_page, 0).pages_now == 1u,
        "the region report sees the last page mapped");
  check(guest_memory_release(last_page, 4096) == 0,
        "release the last page of the window");
  check(guest_memory_region_use(last_page, 0).pages_now == 0u &&
            guest_memory_region_use(last_page, 0).pages_ever == 1u &&
            guest_memory_region_use(last_page, 0).top == GUEST_LAYOUT_LIMIT,
        "and still reports how far the run reached after the release");

  check(guest_memory_map_any(base, base + 8192, 4096, 4096,
                             PROT_READ | PROT_WRITE, &address) == 0 &&
            address == base,
        "map-any preserves the requested guest range and alignment");
  check(guest_memory_release(address, 4096) == 0, "release map-any allocation");
  check(invalidations >= 10, "every remap told the execution owner");

  /*
   * And the notifications are attributed. The observer alone says translated
   * code was thrown away; without a per-cause count there is no way to tell a
   * guest that rewrites its own pages from this owner notifying about a
   * protection change it did not need to report. The run above did all three
   * operations, so all three must be non-zero -- a cause left at zero here
   * would mean it is never counted, not that it never happened.
   */
  {
    const GuestMemoryRemapCounts counts = guest_memory_remap_counts();
    uint64_t total = 0;
    int cause;
    for (cause = 0; cause < kGuestRemapCauseCount; cause++) {
      total += counts.calls[cause];
      check(counts.calls[cause] > 0,
            guest_memory_remap_cause_name((GuestMemoryRemapCause)cause));
      check(counts.pages[cause] >= counts.calls[cause],
            "and each of its calls covered at least a page");
    }
    check(total == invalidations,
          "the causes account for every notification, with none unattributed");
    printf("  remaps by cause: map %llu, protect %llu, release %llu\n",
           (unsigned long long)counts.calls[kGuestRemapMap],
           (unsigned long long)counts.calls[kGuestRemapProtect],
           (unsigned long long)counts.calls[kGuestRemapRelease]);
  }
  printf("guest window memory: %u checks, %u failures, %u invalidations\n",
         checks, failures, invalidations);
  return failures ? 1 : 0;
}
