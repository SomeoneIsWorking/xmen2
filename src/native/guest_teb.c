/* guest_teb.c -- see guest_teb.h. */
#include "guest_teb.h"

#include "guest_heap.h"
#include "guest_memory.h"
#include "pe_map.h"
#include "x2_log.h"
#include "x86rt_native.h"

#include <string.h>

enum {
  MAIN_TEB = 0x000A0000u,
  TEB_BYTES = 0x1000u,
  SEH_CHAIN_END = 0xFFFFFFFFu,
  MAX_IMAGES = 8,
  TEB_TLS_POINTER = 0x2cu
};

typedef struct TlsTemplate {
  uint32_t raw_start;
  uint32_t raw_bytes;
  uint32_t zero_fill;
} TlsTemplate;

static TlsTemplate g_templates[MAX_IMAGES];
static uint32_t g_count;

int guest_teb_register_image(uint32_t base) {
  PeTlsDirectory dir;
  if (!pe_tls_directory(base, &dir))
    return 1;
  if (dir.raw_end < dir.raw_start) {
    x2_log_error("static tls: image at 0x%08x has a TLS template that ends "
                 "(0x%08x) before it starts (0x%08x)\n",
                 base, dir.raw_end, dir.raw_start);
    return 0;
  }
  if (dir.callbacks && RD32(dir.callbacks)) {
    x2_log_error("static tls: image at 0x%08x declares TLS callback 0x%08x; "
                 "this host does not run TLS callbacks, so its threads "
                 "would start with state the image never initialised\n",
                 base, RD32(dir.callbacks));
    return 0;
  }
  if (g_count == MAX_IMAGES) {
    x2_log_error("static tls: more than %u images declare TLS\n", MAX_IMAGES);
    return 0;
  }
  WR32(dir.index_address, g_count);
  g_templates[g_count] =
      (TlsTemplate){dir.raw_start, dir.raw_end - dir.raw_start, dir.zero_fill};
  x2_log_info("static tls: image at 0x%08x is TLS index %u (%u template "
              "byte(s) + %u zero-filled)\n",
              base, g_count, dir.raw_end - dir.raw_start, dir.zero_fill);
  ++g_count;
  return 1;
}

int guest_teb_tls_attach(uint32_t tib) {
  uint32_t array;
  WR32(tib + TEB_TLS_POINTER, 0);
  if (!g_count)
    return 1;
  array = guest_malloc(g_count * 4u);
  if (!array) {
    x2_log_error("static tls: no guest memory for a TLS array\n");
    return 0;
  }
  for (uint32_t i = 0; i < g_count; ++i)
    WR32(array + i * 4u, 0);
  WR32(tib + TEB_TLS_POINTER, array);
  for (uint32_t i = 0; i < g_count; ++i) {
    const TlsTemplate *t = &g_templates[i];
    const uint32_t bytes = t->raw_bytes + t->zero_fill;
    const uint32_t block = guest_malloc(bytes ? bytes : 1u);
    WR32(array + i * 4u, block);
    if (!block) {
      x2_log_error("static tls: no guest memory for TLS index %u's %u "
                   "byte(s)\n",
                   i, bytes);
      guest_teb_tls_detach(tib);
      return 0;
    }
    guest_memory_write(block, guest_memory_const_pointer(t->raw_start),
                       t->raw_bytes);
    for (uint32_t z = 0; z < t->zero_fill; ++z)
      WR8(block + t->raw_bytes + z, 0);
  }
  return 1;
}

void guest_teb_tls_detach(uint32_t tib) {
  const uint32_t array = RD32(tib + TEB_TLS_POINTER);
  if (!array)
    return;
  for (uint32_t i = 0; i < g_count; ++i)
    if (RD32(array + i * 4u))
      guest_free(RD32(array + i * 4u));
  guest_free(array);
  WR32(tib + TEB_TLS_POINTER, 0);
}

uint32_t guest_teb_main_init(void) {
  if (pe_map_anon_low(MAIN_TEB, TEB_BYTES) != 0) {
    x2_log_error("guest teb: could not map the main thread's TEB at "
                 "0x%08x\n",
                 MAIN_TEB);
    return 0;
  }
  /* A zero here would look like a valid record at address 0 to anything
     that did walk the chain. */
  WR32(MAIN_TEB, SEH_CHAIN_END);
  return guest_teb_tls_attach(MAIN_TEB) ? MAIN_TEB : 0u;
}
