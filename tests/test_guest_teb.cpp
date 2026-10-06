/*
 * guest_teb as it ships, against the real guest arena.
 *
 * The image's TLS directory and the guest heap are the injected boundaries:
 * pe_tls_directory answers from a table this test fills, and guest_malloc is
 * a bump allocator over a mapped guest region. Everything guest_teb writes it
 * writes through the product's guest-memory accessors.
 *
 * The point of static TLS is that each thread gets its OWN copy, so the
 * central check writes through one thread's block and requires the other's
 * to be unchanged -- a TEB pointed at the shared template would pass every
 * "the bytes are there" check and fail that one.
 */
#include "guest_memory.h"
#include "guest_teb.h"
#include "pe_map.h"
#include "x86rt.h"
#include "x86rt_native.h"

#include <stdarg.h>
#include <stdio.h>
#include <string.h>
#include <sys/mman.h>

enum {
  MAIN_TEB = 0x000A0000u,
  REGION = 0x00200000u,
  REGION_BYTES = 0x00010000u,
  IMAGE = REGION,
  TEMPLATE = IMAGE + 0x100u,
  TEMPLATE_BYTES = 12u,
  ZERO_FILL = 4u,
  INDEX_SLOT = IMAGE + 0x200u,
  CALLBACKS = IMAGE + 0x210u,
  OTHER_TEB = IMAGE + 0x1000u,
  HEAP = IMAGE + 0x2000u,
  TEB_TLS_POINTER = 0x2cu
};

static unsigned checks, failures;
static uint32_t heap_next = HEAP;
static unsigned heap_frees;
static int image_has_tls;
volatile uint32_t x2_write_watch_addr;

void x2_write_watch_fire(uint32_t address, uint32_t value) {
  (void)address;
  (void)value;
}

void x2_log_error(const char *format, ...) {
  va_list args;
  va_start(args, format);
  vfprintf(stderr, format, args);
  va_end(args);
}

void x2_log_info(const char *format, ...) {
  va_list args;
  va_start(args, format);
  vfprintf(stdout, format, args);
  va_end(args);
}

uint32_t guest_malloc(uint32_t n) {
  const uint32_t at = heap_next;
  heap_next += (n + 15u) & ~15u;
  return at;
}

void guest_free(uint32_t p) {
  (void)p;
  heap_frees++;
}

int pe_map_anon_low(uint32_t want, uint32_t size) {
  return guest_memory_map_fixed(want, size, PROT_READ | PROT_WRITE);
}

int pe_tls_directory(uint32_t base, PeTlsDirectory *out) {
  if (base != IMAGE || !image_has_tls)
    return 0;
  *out = (PeTlsDirectory){TEMPLATE, TEMPLATE + TEMPLATE_BYTES, INDEX_SLOT,
                          CALLBACKS, ZERO_FILL};
  return 1;
}

static void check(int result, const char *what) {
  checks++;
  if (!result) {
    failures++;
    fprintf(stderr, "FAIL: %s\n", what);
  }
}

static uint32_t block_of(uint32_t teb) {
  return RD32(RD32(teb + TEB_TLS_POINTER));
}

int main(void) {
  const char template_bytes[TEMPLATE_BYTES] = "tls-template";
  char read_back[TEMPLATE_BYTES];

  check(guest_memory_init() == 0, "the guest arena is reserved");
  check(pe_map_anon_low(REGION, REGION_BYTES) == 0, "the test region maps");

  /* An image with no TLS directory costs nothing and asks nothing. */
  check(guest_teb_register_image(IMAGE) == 1, "an image without TLS is fine");
  WR32(OTHER_TEB + TEB_TLS_POINTER, 0xdeadbeefu);
  check(guest_teb_tls_attach(OTHER_TEB) &&
            RD32(OTHER_TEB + TEB_TLS_POINTER) == 0u,
        "with nothing registered a TEB's TLS pointer is cleared, not left "
        "holding whatever the heap had there");

  /* A TLS callback would have to run at every thread start; refuse it. */
  image_has_tls = 1;
  guest_memory_write(TEMPLATE, template_bytes, TEMPLATE_BYTES);
  WR32(CALLBACKS, 0x00401000u);
  check(guest_teb_register_image(IMAGE) == 0,
        "an image with a TLS callback is refused");
  WR32(CALLBACKS, 0u);

  WR32(INDEX_SLOT, 0xffffffffu);
  check(guest_teb_register_image(IMAGE) == 1, "the image's TLS registers");
  check(RD32(INDEX_SLOT) == 0u, "its TLS index is written where it reads it");

  check(guest_teb_main_init() == MAIN_TEB, "the main TEB is placed");
  check(RD32(MAIN_TEB) == 0xffffffffu, "it holds the SEH end-of-chain marker");
  guest_memory_read(block_of(MAIN_TEB), read_back, TEMPLATE_BYTES);
  check(memcmp(read_back, template_bytes, TEMPLATE_BYTES) == 0,
        "the main thread's block starts as the template");
  check(RD32(block_of(MAIN_TEB) + TEMPLATE_BYTES) == 0u,
        "and its zero-fill tail is zero");

  check(guest_teb_tls_attach(OTHER_TEB), "a second thread attaches");
  check(block_of(OTHER_TEB) != block_of(MAIN_TEB),
        "the second thread's block is its own");
  WR8(block_of(OTHER_TEB), 'X');
  check(RD8(block_of(MAIN_TEB)) == 't' && RD8(TEMPLATE) == 't',
        "a write through one thread's TLS reaches neither the other thread "
        "nor the template");

  const unsigned frees_before = heap_frees;
  guest_teb_tls_detach(OTHER_TEB);
  check(heap_frees - frees_before == 2u &&
            RD32(OTHER_TEB + TEB_TLS_POINTER) == 0u,
        "detach returns the block and the array and clears the pointer");
  guest_teb_tls_detach(OTHER_TEB);
  check(heap_frees - frees_before == 2u, "a second detach frees nothing");

  printf("guest teb: %u check(s), %u failure(s)\n", checks, failures);
  return failures ? 1 : 0;
}
