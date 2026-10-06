#include "../native/x2_log.h"
#include "d3d8_drawcall.h"

#include <stdio.h>
#include <string.h>

#define D3DFVF_POSITION_MASK 0x000e
#define D3DFVF_XYZ 0x0002
#define D3DFVF_XYZRHW 0x0004
#define D3DFVF_XYZB1 0x0006
#define D3DFVF_XYZB2 0x0008
#define D3DFVF_XYZB3 0x000a
#define D3DFVF_XYZB4 0x000c
#define D3DFVF_XYZB5 0x000e
#define D3DFVF_NORMAL 0x0010
#define D3DFVF_PSIZE 0x0020
#define D3DFVF_DIFFUSE 0x0040
#define D3DFVF_SPECULAR 0x0080
#define D3DFVF_TEXCOUNT_MASK 0x0f00
#define D3DFVF_TEXCOUNT_SHIFT 8

/* Byte offsets follow D3D8's fixed order: position, normal, point size,
   diffuse, specular, then texture coordinates. */
int d3d8_fvf_layout(uint32_t fvf, D3D8VertexLayout *out) {
  uint32_t off = 0;
  uint32_t ntex = (fvf & D3DFVF_TEXCOUNT_MASK) >> D3DFVF_TEXCOUNT_SHIFT;

  memset(out, 0, sizeof *out);
  out->color_offset = -1;
  out->specular_offset = -1;
  out->uv_offset = -1;
  out->normal_offset = -1;
  out->pos_offset = 0;
  switch (fvf & D3DFVF_POSITION_MASK) {
  case D3DFVF_XYZ:
    off += 12;
    break;
  case D3DFVF_XYZRHW:
    out->pretransformed = 1;
    off += 16;
    break;
  /* Blend weights sit between position and normal. D3DRS_VERTEXBLEND is
     disabled on the measured title route, but their bytes still determine
     every following attribute offset. */
  case D3DFVF_XYZB1:
    off += 12 + 4;
    break;
  case D3DFVF_XYZB2:
    off += 12 + 8;
    break;
  case D3DFVF_XYZB3:
    off += 12 + 12;
    break;
  case D3DFVF_XYZB4:
    off += 12 + 16;
    break;
  case D3DFVF_XYZB5:
    off += 12 + 20;
    break;
  default:
    return 0;
  }
  if (fvf & D3DFVF_NORMAL) {
    out->normal_offset = (int)off;
    off += 12;
  }
  if (fvf & D3DFVF_PSIZE)
    off += 4;
  if (fvf & D3DFVF_DIFFUSE) {
    out->color_offset = (int)off;
    off += 4;
  }
  if (fvf & D3DFVF_SPECULAR) {
    out->specular_offset = (int)off;
    off += 4;
  }
  if (ntex) {
    out->uv_offset = (int)off;
    off += 8u * ntex;
  }
  out->stride = off;
  return 1;
}

#define FVF_SEEN_MAX 16
static struct {
  uint32_t fvf;
  unsigned long n;
} g_seen[FVF_SEEN_MAX];
static int g_seen_n;
static unsigned long g_dropped;

void d3d8_fvf_note(uint32_t fvf) {
  int i;
  for (i = 0; i < g_seen_n; i++)
    if (g_seen[i].fvf == fvf) {
      g_seen[i].n++;
      return;
    }
  if (g_seen_n == FVF_SEEN_MAX) {
    g_dropped++;
    return;
  }
  g_seen[g_seen_n].fvf = fvf;
  g_seen[g_seen_n].n = 1;
  g_seen_n++;
}

static const char *position_name(uint32_t fvf) {
  switch (fvf & D3DFVF_POSITION_MASK) {
  case D3DFVF_XYZ:
    return "XYZ";
  case D3DFVF_XYZRHW:
    return "XYZRHW (pre-transformed)";
  case D3DFVF_XYZB1:
    return "XYZB1 (1 blend weight)";
  case D3DFVF_XYZB2:
    return "XYZB2 (2 blend weights)";
  case D3DFVF_XYZB3:
    return "XYZB3 (3 blend weights)";
  case D3DFVF_XYZB4:
    return "XYZB4 (4 blend weights)";
  case D3DFVF_XYZB5:
    return "XYZB5 (5 blend weights)";
  default:
    return "NO POSITION";
  }
}

void d3d8_fvf_report(void) {
  int i;
  unsigned long total = 0, blended = 0;
  for (i = 0; i < g_seen_n; i++) {
    total += g_seen[i].n;
    if ((g_seen[i].fvf & D3DFVF_POSITION_MASK) > D3DFVF_XYZRHW)
      blended += g_seen[i].n;
  }
  x2_log_info("        vertex formats: %lu fixed-function draw(s), %d distinct "
              "FVF(s)%s; %lu of them carry BLEND WEIGHTS\n",
              total, g_seen_n, g_dropped ? " (TABLE FULL -- more exist)" : "",
              blended);
  for (i = 0; i < g_seen_n; i++)
    x2_log_info("          0x%08x  %-26s x%lu\n", g_seen[i].fvf,
                position_name(g_seen[i].fvf), g_seen[i].n);
  if (!g_seen_n)
    x2_log_info("          none -- no fixed-function draw reached this "
                "backend, so this says NOTHING about the vertex formats.\n");
}
