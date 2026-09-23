/* d3d8_draw_range.c -- see d3d8_draw_range.h. */
#include "d3d8_draw_range.h"

#include "../native/x2_log.h"
#include "guest_memory.h"

#include <stdint.h>

/*
 * Does this draw read OUTSIDE the vertex buffer it is bound to?
 *
 * Nothing asked this question before, and a GPU page fault is what asking it
 * costs to miss: amdgpu killed a run with
 *
 *   [gfxhub] page fault ... Process x2native ... client 0x1b (UTCL2)
 *   ring gfx_0.0.0 timeout ... Ring gfx_0.0.0 reset succeeded
 *
 * A vertex fetch past the end of a buffer is the ordinary way to produce that,
 * and an index buffer whose contents outrun the stream bound beside it is the
 * ordinary way to produce THAT. D3D8 lets the guest set the two independently,
 * so the pairing is only wrong at the draw -- which is here.
 *
 * The check is exact, not a heuristic: the indices live in guest memory (they
 * are uploaded from there on Unlock), so the largest one this draw will
 * actually read is a fact available on the CPU. Reading them is O(indices),
 * and it is paid once per range of each upload: d3d8_index_max remembers the
 * answer by the index buffer's upload serial.
 *
 * Every outcome is counted, INCLUDING the one where the check could not run --
 * an indexed draw whose index buffer has no guest storage cannot be verified,
 * and "not verified" must never be filed under "fine".
 */
static unsigned long g_rng_checked, g_rng_unverifiable, g_rng_bad;
static uint32_t g_rng_worst_need, g_rng_worst_have;

/*
 * The largest index of a range, by the upload serial of the buffer holding
 * it. Direct-mapped: a colliding range replaces the entry, which costs one
 * scan and never a wrong answer, since the whole key is compared.
 */
#define D3D8_INDEX_MAX_ENTRIES 4096u

typedef struct IndexMaxEntry {
  uint64_t serial;
  uint32_t first, count, max;
} IndexMaxEntry;

static IndexMaxEntry g_index_max[D3D8_INDEX_MAX_ENTRIES];
static unsigned long g_index_max_hits, g_index_max_scans;

static uint32_t scan_max(const void *indices, int is32, uint32_t first,
                         uint32_t count) {
  uint32_t max = 0;
  if (is32) {
    const uint32_t *p = (const uint32_t *)indices + first;
    for (uint32_t i = 0; i < count; i++)
      if (p[i] > max)
        max = p[i];
  } else {
    const uint16_t *p = (const uint16_t *)indices + first;
    for (uint32_t i = 0; i < count; i++)
      if (p[i] > max)
        max = p[i];
  }
  return max;
}

uint32_t d3d8_index_max(uint64_t serial, const void *indices, int is32,
                        uint32_t first, uint32_t count) {
  if (!serial) {
    g_index_max_scans++;
    return scan_max(indices, is32, first, count);
  }
  const uint64_t key = serial * 0x9E3779B97F4A7C15ull ^
                       ((uint64_t)first << 32) ^ (uint64_t)count;
  IndexMaxEntry *e = &g_index_max[(key >> 20) % D3D8_INDEX_MAX_ENTRIES];
  if (e->serial == serial && e->first == first && e->count == count) {
    g_index_max_hits++;
    return e->max;
  }
  g_index_max_scans++;
  e->serial = serial;
  e->first = first;
  e->count = count;
  e->max = scan_max(indices, is32, first, count);
  return e->max;
}

int d3d8_draw_range_ok(const D3D8DrawRequest *req, uint32_t stride) {
  uint32_t n = d3d8_element_count(req->primitive_type, req->primitive_count);
  uint32_t have, need = 0, maxi = 0;

  if (!stride || !n)
    return 1; /* nothing this can decide */
  have = req->vertex_bytes / stride;

  if (req->index_buffer) {
    uint32_t esz = req->index_is_32bit ? 4u : 2u;
    uint64_t last = (uint64_t)(req->first_index + n) * esz;
    if (!req->index_guest_bytes || last > req->index_bytes) {
      /*
       * FAIL FAST: a draw whose index range cannot be checked is
       * REFUSED, not waved through.
       *
       * This used to return 1 -- "unverifiable, carry on" -- which is
       * how an unverified draw reaches the GPU and, if its indices do
       * run past the stream, page-faults the device and resets the card
       * for every process on it. Measured over a full gameplay run:
       * 0 of 273,289 draws land here, so refusing costs nothing today
       * and turns a future silent risk into a visible hole with a line
       * of log next to it.
       */
      g_rng_unverifiable++;
      if (g_rng_unverifiable <= 3)
        x2_log_error("d3d8: an indexed draw's range cannot be "
                     "checked (index guest 0x%08x, %u byte(s), first index "
                     "%u, %u indices needed) -- REFUSED rather than "
                     "submitted unverified.\n",
                     req->index_guest_bytes, req->index_bytes, req->first_index,
                     n);
      return 0;
    }
    maxi = d3d8_index_max(gpu_buffer_serial(req->index_buffer),
                          guest_memory_const_pointer(req->index_guest_bytes),
                          req->index_is_32bit, req->first_index, n);
    need = req->base_vertex + maxi + 1u;
  } else {
    need = req->first_vertex + n;
  }
  g_rng_checked++;
  if (need <= have)
    return 1;

  g_rng_bad++;
  if (need - have > g_rng_worst_need - g_rng_worst_have || !g_rng_worst_need) {
    g_rng_worst_need = need;
    g_rng_worst_have = have;
  }
  if (g_rng_bad <= 4)
    x2_log_error(
        "d3d8: a draw would fetch vertex %u from a stream holding %u "
        "(%u bytes at stride %u) -- REFUSED. primitive type %u, %u "
        "primitive(s), base vertex %u, first index %u. Submitting it "
        "reads outside the buffer, which is how the GPU is made to page "
        "fault.\n",
        need - 1u, have, req->vertex_bytes, stride, req->primitive_type,
        req->primitive_count, req->base_vertex, req->first_index);
  return 0;
}

void d3d8_draw_range_report(void) {
  /* ALWAYS, including the all-clear: "0 of 290002 draws read outside their
     stream" is a measurement, and a line that only appears when something is
     wrong cannot be told apart from a check that never ran. */
  x2_log_info(
      "        vertex range: %lu draw(s) checked, %lu read OUTSIDE their "
      "stream and were refused, %lu could not be checked (no host-"
      "readable indices, or indices past the end of their own buffer)\n",
      g_rng_checked, g_rng_bad, g_rng_unverifiable);
  if (g_rng_bad)
    x2_log_info("          worst: needed vertex %u from a stream of %u\n",
                g_rng_worst_need - 1u, g_rng_worst_have);
  x2_log_info("          index ranges: %lu answered from an earlier scan "
              "of the same upload, %lu scanned\n",
              g_index_max_hits, g_index_max_scans);
}
