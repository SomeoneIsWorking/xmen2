/*
 * GDI32 -- the little of it this game touches.
 *
 * Eight imports across the whole set, and they are two unrelated jobs:
 *
 *   GetDeviceCaps                 libIGDisplay asking about the screen
 *   CreateCompatibleDC, CreateDIBSection, ExtTextOutA, SetTextColor,
 *   SetBkMode, DeleteObject, DeleteDC
 *                                 libIGGfx rasterising text into a bitmap,
 *                                 i.e. building a font texture with Windows'
 *                                 own glyph rendering
 *
 * The DC and DIB half is implemented now that the engine has reached it: a
 * memory DC is a token with some text state on it, and a DIB section is
 * GUEST-ADDRESSABLE PIXELS -- the caller writes through the pointer it is
 * handed, so the memory has to be somewhere the guest can name.
 *
 * ExtTextOutA is the one thing here that is IGNORED rather than implemented,
 * and it is ignored loudly and counted: rasterising Windows' own glyphs needs a
 * font engine this port does not have. The consequence is stated at the point
 * of the ignore and again in the shutdown report -- whatever this was drawing
 * comes out blank, and that is a knowable fact rather than a mystery about
 * missing text.
 */
#include "guest_heap.h"
#include "guest_memory.h"
#include "x86rt.h"
#include "x86rt_native.h"

#include <stdio.h>
#include <string.h>

#ifdef X2_WITH_SDL
#include <SDL3/SDL.h>
#endif

#define A(i) RD32(C->esp + 4u + (uint32_t)(i) * 4u)

static void ret_std(CPU *C, uint32_t eax, int nargs) {
  C->eax = eax;
  C->esp += 4u + (uint32_t)nargs * 4u;
}

/* wingdi.h indices, only the ones a display query plausibly asks for. */
#define DRIVERVERSION 0
#define TECHNOLOGY 2
#define HORZSIZE 4
#define VERTSIZE 6
#define HORZRES 8
#define VERTRES 10
#define BITSPIXEL 12
#define PLANES 14
#define NUMCOLORS 24
#define RASTERCAPS 38
#define LOGPIXELSX 88
#define LOGPIXELSY 90
#define SIZEPALETTE 104
#define COLORRES 108
#define VREFRESH 116
#define DESKTOPVERTRES 117
#define DESKTOPHORZRES 118

#define DT_RASDISPLAY 1

/*
 * The real desktop, when SDL can say. Falling back to a made-up 1920x1080 would
 * be a lie of exactly the kind that matters here: libIGDisplay uses this to
 * decide which video modes to offer, so a wrong answer produces a wrong mode
 * list and the failure lands later, in mode selection, looking unrelated.
 * Whether the value is measured or assumed is therefore reported.
 */
static int desktop(int *w, int *h, int *bpp, int *hz) {
  *w = 1024;
  *h = 768;
  *bpp = 32;
  *hz = 60;
#ifdef X2_WITH_SDL
  {
    SDL_DisplayID d = SDL_GetPrimaryDisplay();
    const SDL_DisplayMode *m = d ? SDL_GetDesktopDisplayMode(d) : NULL;
    if (m) {
      const SDL_PixelFormatDetails *pf = SDL_GetPixelFormatDetails(m->format);
      /* PIXELS: SDL reports a HiDPI desktop in scaled points (this 4K
         screen says 1536x864 at pixel_density 2.5), and a 2005 title
         asking GetDeviceCaps for HORZRES means pixels. See the same
         conversion, and what a logical answer cost, in
         src/d3d8/d3d8_d3d8.c current_desktop(). */
      float density = m->pixel_density > 0.0f ? m->pixel_density : 1.0f;
      *w = (int)((float)m->w * density + 0.5f);
      *h = (int)((float)m->h * density + 0.5f);
      if (pf && pf->bits_per_pixel)
        *bpp = pf->bits_per_pixel;
      if (m->refresh_rate > 0.0f)
        *hz = (int)(m->refresh_rate + 0.5f);
      return 1;
    }
  }
#endif
  return 0;
}

void imp_GDI32_GetDeviceCaps(CPU *C) {
  uint32_t index = A(1); /* (HDC hdc, int index) */
  int w, h, bpp, hz, measured;
  static int told;

  measured = desktop(&w, &h, &bpp, &hz);
  if (!told++) {
    fprintf(stderr,
            "GDI32: GetDeviceCaps answering from %s: %dx%d, %d bpp, %d Hz\n",
            measured ? "SDL's real desktop mode"
                     : "BUILT-IN DEFAULTS -- SDL could not report a display "
                       "mode, so these are assumed, not measured",
            w, h, bpp, hz);
  }

  switch (index) {
  case HORZRES:
    ret_std(C, (uint32_t)w, 2);
    return;
  case VERTRES:
    ret_std(C, (uint32_t)h, 2);
    return;
  case DESKTOPHORZRES:
    ret_std(C, (uint32_t)w, 2);
    return;
  case DESKTOPVERTRES:
    ret_std(C, (uint32_t)h, 2);
    return;
  case BITSPIXEL:
    ret_std(C, (uint32_t)bpp, 2);
    return;
  case VREFRESH:
    ret_std(C, (uint32_t)hz, 2);
    return;
  case PLANES:
    ret_std(C, 1, 2);
    return;
  case TECHNOLOGY:
    ret_std(C, DT_RASDISPLAY, 2);
    return;
  case DRIVERVERSION:
    ret_std(C, 0x4000, 2);
    return;
  case LOGPIXELSX:
  case LOGPIXELSY:
    ret_std(C, 96, 2);
    return; /* Windows' 100% DPI */
  /* Physical size in millimetres, derived from the pixel count at 96 DPI so
     it agrees with LOGPIXELS* rather than being independently invented. */
  case HORZSIZE:
    ret_std(C, (uint32_t)(w * 254 / 960), 2);
    return;
  case VERTSIZE:
    ret_std(C, (uint32_t)(h * 254 / 960), 2);
    return;
  /* Truthful for any bpp above 8: no palette, and more colours than fit in
     an int, which is what Windows itself reports for a direct-colour mode. */
  case NUMCOLORS:
    ret_std(C, (uint32_t)-1, 2);
    return;
  case SIZEPALETTE:
    ret_std(C, 0, 2);
    return;
  case COLORRES:
    ret_std(C, (uint32_t)bpp, 2);
    return;
  case RASTERCAPS:
    ret_std(C, 0, 2);
    return;
  default:
    /*
     * Not silently 0. Zero is a legitimate answer for several indices, so a
     * blanket zero is indistinguishable from a real one and the caller acts
     * on it -- which is how a wrong mode list would look like a driver
     * quirk. Naming the index is what makes the next one a two-line fix.
     */
    fprintf(stderr,
            "GDI32: GetDeviceCaps(index=%u) is not implemented. Returning 0, "
            "which for this index may be indistinguishable from a real "
            "answer -- if the display behaves oddly, start here "
            "(src/native/gdi32.c).\n",
            index);
    ret_std(C, 0, 2);
    return;
  }
}

/* ---- memory DCs and DIB sections ---------------------------------------
 *
 * Handles are tagged indices rather than pointers: the guest holds them in 32
 * bits, and a tag makes a DC passed where a bitmap belongs report itself
 * instead of indexing the wrong table.
 */
#define DC_TAG 0x0D000000u
#define BMP_TAG 0x0B000000u
#define MAX_DC 8
#define MAX_BMP 16

static struct {
  int used;
  uint32_t bitmap, textcolor, bkmode;
} g_dc[MAX_DC];
static struct {
  int used;
  uint32_t bits, bytes, w, h, bpp;
} g_bmp[MAX_BMP];
static unsigned long g_dcs, g_bmps, g_text_ignored;

static int dc_index(uint32_t h) {
  if ((h & 0xFF000000u) != DC_TAG)
    return -1;
  {
    uint32_t i = (h & 0xFFFFFFu);
    return (i && i <= MAX_DC && g_dc[i - 1].used) ? (int)i - 1 : -1;
  }
}

static int bmp_index(uint32_t h) {
  if ((h & 0xFF000000u) != BMP_TAG)
    return -1;
  {
    uint32_t i = (h & 0xFFFFFFu);
    return (i && i <= MAX_BMP && g_bmp[i - 1].used) ? (int)i - 1 : -1;
  }
}

void imp_GDI32_CreateCompatibleDC(CPU *C) {
  int i;
  for (i = 0; i < MAX_DC; i++)
    if (!g_dc[i].used)
      break;
  if (i == MAX_DC) {
    fprintf(stderr,
            "GDI32: all %d memory DCs are live; CreateCompatibleDC "
            "fails, which is what Windows does when the system is "
            "out of them.\n",
            MAX_DC);
    ret_std(C, 0, 1);
    return;
  }
  memset(&g_dc[i], 0, sizeof g_dc[i]);
  g_dc[i].used = 1;
  g_dc[i].bkmode = 2u; /* OPAQUE, Win32's default */
  g_dcs++;
  ret_std(C, DC_TAG | (uint32_t)(i + 1), 1);
}

void imp_GDI32_DeleteDC(CPU *C) {
  int i = dc_index(A(0));
  if (i < 0) {
    ret_std(C, 0, 1);
    return;
  }
  g_dc[i].used = 0;
  ret_std(C, 1, 1);
}

/*
 * CreateDIBSection(hdc, pbmi, usage, ppvBits, hSection, offset)
 *
 * The whole point of a DIB section is that the CALLER writes the pixels, so
 * what matters here is the pointer handed back through ppvBits and the layout
 * agreeing with the header the caller passed: rows are padded to 4 bytes, and
 * a POSITIVE biHeight means the rows run bottom-up.
 */
void imp_GDI32_CreateDIBSection(CPU *C) {
  uint32_t pbmi = A(1), ppv = A(3), hsection = A(4);
  int32_t w, h;
  uint32_t bpp, compression, stride, bytes, i;

  if (!pbmi || !ppv) {
    ret_std(C, 0, 6);
    return;
  }
  if (hsection) {
    /* A DIB backed by a file mapping is a different object -- the pixels
       live in the section, not in memory this host allocates. Refused by
       name rather than quietly given private memory the caller's mapping
       would not see. */
    fprintf(stderr,
            "GDI32: CreateDIBSection with a SECTION handle "
            "(0x%08x), which this host does not implement -- the "
            "pixels would have to live in that mapping.\n",
            hsection);
    WR32(ppv, 0);
    ret_std(C, 0, 6);
    return;
  }
  w = (int32_t)RD32(pbmi + 4u);
  h = (int32_t)RD32(pbmi + 8u);
  bpp = RD16(pbmi + 14u);
  compression = RD32(pbmi + 16u);
  if (compression != 0u) { /* BI_RGB only */
    fprintf(stderr,
            "GDI32: CreateDIBSection with compression %u; only "
            "BI_RGB (0) is implemented, and guessing at a "
            "compressed layout would hand back pixels in the wrong "
            "format.\n",
            compression);
    WR32(ppv, 0);
    ret_std(C, 0, 6);
    return;
  }
  if (w <= 0 || h == 0 ||
      (bpp != 8u && bpp != 16u && bpp != 24u && bpp != 32u)) {
    fprintf(stderr,
            "GDI32: CreateDIBSection(%d x %d, %u bpp) -- refusing a "
            "shape this host cannot lay out.\n",
            w, h, bpp);
    WR32(ppv, 0);
    ret_std(C, 0, 6);
    return;
  }
  /* Win32's row padding, which the caller relies on when it indexes rows. */
  stride = (((uint32_t)w * bpp + 31u) / 32u) * 4u;
  bytes = stride * (uint32_t)(h < 0 ? -h : h);

  for (i = 0; i < MAX_BMP; i++)
    if (!g_bmp[i].used)
      break;
  if (i == MAX_BMP) {
    fprintf(stderr, "GDI32: all %d DIB sections are live.\n", MAX_BMP);
    WR32(ppv, 0);
    ret_std(C, 0, 6);
    return;
  }
  /* GUEST memory: the caller writes through this pointer with ordinary
     stores, so it has to be an address the guest can hold in 32 bits. */
  g_bmp[i].bits = guest_malloc(bytes);
  if (!g_bmp[i].bits) {
    fprintf(stderr, "GDI32: no guest memory for a %u-byte DIB section\n",
            bytes);
    WR32(ppv, 0);
    ret_std(C, 0, 6);
    return;
  }
  memset(guest_memory_pointer(g_bmp[i].bits), 0, bytes);
  g_bmp[i].used = 1;
  g_bmp[i].bytes = bytes;
  g_bmp[i].w = (uint32_t)w;
  g_bmp[i].h = (uint32_t)(h < 0 ? -h : h);
  g_bmp[i].bpp = bpp;
  g_bmps++;
  WR32(ppv, g_bmp[i].bits);
  ret_std(C, BMP_TAG | (uint32_t)(i + 1), 6);
}

void imp_GDI32_DeleteObject(CPU *C) {
  int i = bmp_index(A(0));
  if (i < 0) {
    /* Not one of ours. Win32 returns FALSE for a handle it does not own,
       and saying so is better than returning TRUE for a delete that did
       not happen. */
    ret_std(C, 0, 1);
    return;
  }
  guest_free(g_bmp[i].bits);
  g_bmp[i].used = 0;
  ret_std(C, 1, 1);
}

void imp_GDI32_SetBkMode(CPU *C) {
  int i = dc_index(A(0));
  uint32_t prev;
  if (i < 0) {
    ret_std(C, 0, 2);
    return;
  }
  prev = g_dc[i].bkmode;
  g_dc[i].bkmode = A(1);
  ret_std(C, prev, 2); /* Win32 returns the previous */
}

void imp_GDI32_SetTextColor(CPU *C) {
  int i = dc_index(A(0));
  uint32_t prev;
  if (i < 0) {
    ret_std(C, 0xFFFFFFFFu, 2);
    return;
  } /* CLR_INVALID */
  prev = g_dc[i].textcolor;
  g_dc[i].textcolor = A(1);
  ret_std(C, prev, 2);
}

/*
 * ExtTextOutA -- IGNORED, loudly and counted.
 *
 * Drawing it means rasterising Windows' own glyphs, which needs a font engine
 * (and the font the caller selected, through a SelectObject this game never
 * calls). This port does not have one.
 *
 * Ignored rather than aborted because it is what the engine uses to BUILD a
 * texture, not to draw the frame: stopping the run over it would trade a blank
 * texture for no run at all. The consequence is stated here and counted in the
 * report, so "the text this was making is blank" is a fact on the record
 * rather than something to be rediscovered from a screenshot.
 */
void imp_GDI32_ExtTextOutA(CPU *C) {
  if (!g_text_ignored++)
    fprintf(stderr, "GDI32: ExtTextOutA is IGNORED -- this host has no font "
                    "rasteriser, so whatever bitmap the engine is drawing "
                    "glyphs into stays blank.\n"
                    "  The call succeeds so the engine carries on; the "
                    "total is in the shutdown report.\n");
  ret_std(C, 1, 8);
}

void gdi32_report(void) {
  int i, dcs = 0, bmps = 0;
  for (i = 0; i < MAX_DC; i++)
    if (g_dc[i].used)
      dcs++;
  for (i = 0; i < MAX_BMP; i++)
    if (g_bmp[i].used)
      bmps++;
  if (!g_dcs && !g_bmps && !g_text_ignored) {
    printf("  gdi32: no memory DC or DIB section was ever created.\n");
    return;
  }
  printf("  gdi32: %lu memory DC(s) and %lu DIB section(s) created; %d and %d "
         "still live\n",
         g_dcs, g_bmps, dcs, bmps);
  if (g_text_ignored)
    printf("         %lu ExtTextOutA call(s) IGNORED -- no font rasteriser, "
           "so whatever they were drawing into is blank\n",
           g_text_ignored);
}
