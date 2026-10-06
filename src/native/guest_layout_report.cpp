#include "guest_layout_report.h"

#include "guest_layout.h"
#include "guest_memory.h"
#include "x2_log.h"

#include <stdint.h>

/*
 * The browser backs the whole layout with one window and commits all of it
 * (#159), so each region's size there is a cost paid whether or not the run
 * uses it. The ceilings were chosen so regions could not collide; this is the
 * measurement of what a run actually reaches, region by region.
 *
 * Boundaries, not regions: each span runs from one entry to the next, and
 * the last runs to 4 GB, so a mapping above the layout reads as a named
 * surprise.
 */
typedef struct LayoutBoundary {
  const char *name; /* the span from here to the next boundary */
  uint32_t address;
} LayoutBoundary;

static const LayoutBoundary kBoundaries[] = {
    {"image", 0u},
    {"modules", GUEST_MODULE_LO},
    {"reserve", GUEST_RESERVE_LO},
    {"runtime", GUEST_RUNTIME_BASE},
    {"heap", GUEST_HEAP_BASE},
    {"views", GUEST_VIEW_ARENA_BASE},
    {"above", GUEST_LAYOUT_LIMIT},
};

enum { kBoundaryCount = sizeof kBoundaries / sizeof kBoundaries[0] };

static uint64_t boundary_end(int index) {
  return index + 1 < kBoundaryCount ? kBoundaries[index + 1].address
                                    : UINT64_C(1) << 32;
}

void guest_layout_report(void) {
  const double mb = 1024.0 * 1024.0;
  int i;
  for (i = 0; i < kBoundaryCount; i++) {
    const uint32_t lo = kBoundaries[i].address;
    const uint64_t hi = boundary_end(i);
    const GuestMemoryRegionUse use = guest_memory_region_use(
        lo, hi == UINT64_C(1) << 32 ? 0u : (uint32_t)hi);
    if (!use.pages_ever) {
      x2_log_info("  guest layout %-7s 0x%08x-0x%09llx: never mapped\n",
                  kBoundaries[i].name, lo, (unsigned long long)hi);
      continue;
    }
    x2_log_info("  guest layout %-7s 0x%08x-0x%09llx: reached 0x%08llx "
                "(%.1f of %.1f MB), %.1f MB ever mapped, %.1f MB now\n",
                kBoundaries[i].name, lo, (unsigned long long)hi,
                (unsigned long long)use.top, (double)(use.top - lo) / mb,
                (double)(hi - lo) / mb,
                (double)use.pages_ever * GUEST_PAGE_SIZE / mb,
                (double)use.pages_now * GUEST_PAGE_SIZE / mb);
  }
}
