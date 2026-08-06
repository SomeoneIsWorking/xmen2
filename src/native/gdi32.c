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
 * Only the first is implemented here, because only the first has been reached.
 * The rest keep their generated stubs, which abort by name -- so if the game
 * gets to the font path it says so, loudly, instead of drawing nothing and
 * leaving someone to wonder why the text is missing.
 */
#include "x86rt.h"
#include "x86rt_native.h"

#include <stdio.h>

#ifdef X2_WITH_SDL
#include <SDL3/SDL.h>
#endif

#define A(i) RD32(C->esp + 4u + (uint32_t)(i) * 4u)

static void ret_std(CPU *C, uint32_t eax, int nargs)
{
    C->eax = eax;
    C->esp += 4u + (uint32_t)nargs * 4u;
}

/* wingdi.h indices, only the ones a display query plausibly asks for. */
#define DRIVERVERSION 0
#define TECHNOLOGY    2
#define HORZSIZE      4
#define VERTSIZE      6
#define HORZRES       8
#define VERTRES      10
#define BITSPIXEL    12
#define PLANES       14
#define NUMCOLORS    24
#define RASTERCAPS   38
#define LOGPIXELSX   88
#define LOGPIXELSY   90
#define SIZEPALETTE 104
#define COLORRES    108
#define VREFRESH    116
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
static int desktop(int *w, int *h, int *bpp, int *hz)
{
    *w = 1024; *h = 768; *bpp = 32; *hz = 60;
#ifdef X2_WITH_SDL
    {
        SDL_DisplayID d = SDL_GetPrimaryDisplay();
        const SDL_DisplayMode *m = d ? SDL_GetDesktopDisplayMode(d) : NULL;
        if (m) {
            const SDL_PixelFormatDetails *pf =
                SDL_GetPixelFormatDetails(m->format);
            *w = m->w;
            *h = m->h;
            if (pf && pf->bits_per_pixel) *bpp = pf->bits_per_pixel;
            if (m->refresh_rate > 0.0f) *hz = (int)(m->refresh_rate + 0.5f);
            return 1;
        }
    }
#endif
    return 0;
}

void imp_GDI32_GetDeviceCaps(CPU *C)
{
    uint32_t index = A(1);           /* (HDC hdc, int index) */
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
    case HORZRES:        ret_std(C, (uint32_t)w, 2); return;
    case VERTRES:        ret_std(C, (uint32_t)h, 2); return;
    case DESKTOPHORZRES: ret_std(C, (uint32_t)w, 2); return;
    case DESKTOPVERTRES: ret_std(C, (uint32_t)h, 2); return;
    case BITSPIXEL:      ret_std(C, (uint32_t)bpp, 2); return;
    case VREFRESH:       ret_std(C, (uint32_t)hz, 2); return;
    case PLANES:         ret_std(C, 1, 2); return;
    case TECHNOLOGY:     ret_std(C, DT_RASDISPLAY, 2); return;
    case DRIVERVERSION:  ret_std(C, 0x4000, 2); return;
    case LOGPIXELSX:
    case LOGPIXELSY:     ret_std(C, 96, 2); return;   /* Windows' 100% DPI */
    /* Physical size in millimetres, derived from the pixel count at 96 DPI so
       it agrees with LOGPIXELS* rather than being independently invented. */
    case HORZSIZE:       ret_std(C, (uint32_t)(w * 254 / 960), 2); return;
    case VERTSIZE:       ret_std(C, (uint32_t)(h * 254 / 960), 2); return;
    /* Truthful for any bpp above 8: no palette, and more colours than fit in
       an int, which is what Windows itself reports for a direct-colour mode. */
    case NUMCOLORS:      ret_std(C, (uint32_t)-1, 2); return;
    case SIZEPALETTE:    ret_std(C, 0, 2); return;
    case COLORRES:       ret_std(C, (uint32_t)bpp, 2); return;
    case RASTERCAPS:     ret_std(C, 0, 2); return;
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
                "(src/native/gdi32.c).\n", index);
        ret_std(C, 0, 2);
        return;
    }
}
