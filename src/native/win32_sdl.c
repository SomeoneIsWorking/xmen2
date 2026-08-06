/*
 * The Win32 surface libIGDisplay actually calls, implemented on SDL3 and libc.
 *
 * This is the part that replaces Wine. It is deliberately NOT a Win32
 * emulation layer: it implements the 43 functions the recompiled bodies really
 * reach (measured with `nm -u`, not guessed from the import table -- the table
 * lists more), and every function outside that set stays an aborting stub that
 * names itself.
 *
 * The rule for everything here: implement it, or abort. A stub that returns 0
 * or a plausible handle is worse than one that stops, because the failure then
 * surfaces somewhere with no connection to the missing function. There is one
 * category of exception, marked NO-OP below, where doing nothing IS the correct
 * native behaviour rather than a placeholder -- and each one says why.
 *
 * ---- the calling convention ----
 *
 * A recompiled body calls an import like this:
 *
 *     C->esp -= 4; WR32(C->esp, <fake return address>);
 *     imp_USER32_ShowWindow(C);
 *
 * so on entry C->esp points at that fake return address and argument i is at
 * C->esp + 4 + 4i. On exit the runtime has to leave C->esp where the real
 * callee would have: Win32 is __stdcall, so the callee pops its arguments;
 * the CRT is __cdecl, so it does not. Getting that wrong shifts the guest
 * stack by a word and the damage appears much later, so the two cases are
 * separate functions with the count spelled out at every call site.
 */
#include "x86rt.h"
#include "x86rt_native.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <SDL3/SDL.h>

#include "guest_heap.h"

/* ---- guest ABI helpers ------------------------------------------------- */

#define A(i) RD32(C->esp + 4u + (uint32_t)(i) * 4u)

/* __stdcall: the callee pops `nargs` dwords as well as the return address. */
static void ret_std(CPU *C, uint32_t eax, int nargs)
{
    C->eax = eax;
    C->esp += 4u + (uint32_t)nargs * 4u;
}

/* __cdecl: the caller cleans up, so only the return address goes. */
static void ret_cdecl(CPU *C, uint32_t eax)
{
    C->eax = eax;
    C->esp += 4u;
}

static void unimplemented(const char *what)
{
    fprintf(stderr, "win32_sdl: %s is reached but not implemented.\n"
                    "  Not returning a plausible value: that would move the "
                    "failure somewhere unrelated.\n", what);
    abort();
}

/* ---- the window --------------------------------------------------------
 *
 * The guest thinks in HWNDs. One SDL window backs all of them, and handles are
 * small tokens rather than pointers so that a handle the guest invented, or
 * kept past DestroyWindow, is caught here instead of being dereferenced.
 */
#define HWND_DESKTOP_TOK 0x00010000u
#define HWND_MAIN_TOK    0x00010004u

static SDL_Window *g_win;
static int         g_win_live;
static int         g_cursor_shown = 1;   /* Win32 starts the count at 0 */
static int         g_cursor_count;

/* The renderer needs the window to put a swapchain on; see win32_sdl.h. It is
   reported only while live, so a destroyed window cannot be presented to. */
SDL_Window *win32_sdl_window(void) { return g_win_live ? g_win : NULL; }

/* The desktop is a valid target for size queries and nothing else. */
static int hwnd_is_main(uint32_t h)    { return h == HWND_MAIN_TOK && g_win_live; }
static int hwnd_is_desktop(uint32_t h) { return h == HWND_DESKTOP_TOK; }

static void guest_rect(uint32_t p, int32_t l, int32_t t, int32_t r, int32_t b)
{
    WR32(p +  0u, (uint32_t)l);
    WR32(p +  4u, (uint32_t)t);
    WR32(p +  8u, (uint32_t)r);
    WR32(p + 12u, (uint32_t)b);
}

static void desktop_size(int *w, int *h)
{
    const SDL_DisplayMode *m = NULL;
    SDL_DisplayID d = SDL_GetPrimaryDisplay();
    if (d) m = SDL_GetDesktopDisplayMode(d);
    /* No display (headless CI, no compositor) is a real answer, not a guess:
       say so rather than inventing 1920x1080 and letting the game lay out
       against a resolution that does not exist. */
    if (!m) {
        fprintf(stderr, "win32_sdl: no display -- SDL cannot report a desktop "
                        "size, and this layer will not invent one\n");
        abort();
    }
    *w = m->w;
    *h = m->h;
}

/* The C runtime lives in src/native/crt.c, under both its MSVCRT and MSVCR71
   spellings. It was here first, which put CRT functions in the file named for
   the Win32/SDL surface; moving them removed a duplicate definition and the
   confusion that came with it. */

/* ---- KERNEL32 ---------------------------------------------------------- */

void imp_KERNEL32_DisableThreadLibraryCalls(CPU *C)
{
    /* NO-OP, and correct: it suppresses DLL_THREAD_ATTACH notifications, and
       this build has no Windows loader to deliver them in the first place. */
    ret_std(C, 1, 1);
}

void imp_KERNEL32_GetModuleHandleA(CPU *C)
{
    /* Only the module's own handle is meaningful here, and in a PE that handle
       IS the image base -- which is what the guest uses it as. A request for
       any other module is a real question this layer cannot answer. */
    uint32_t name = A(0);
    if (name == 0) { ret_std(C, G_IMGBASE, 1); return; }
    fprintf(stderr, "win32_sdl: GetModuleHandleA(\"%s\") -- only the calling "
                    "module's own handle exists natively\n",
            (const char *)(uintptr_t)name);
    abort();
}

void imp_KERNEL32_MultiByteToWideChar(CPU *C)
{
    /* The game uses it for ASCII only. Anything else would need a real
       codepage conversion, so it stops rather than mangling text. */
    uint32_t cp = A(0), src = A(2); int32_t srclen = (int32_t)A(3);
    uint32_t dst = A(4); int32_t dstlen = (int32_t)A(5);
    const unsigned char *s = (const unsigned char *)(uintptr_t)src;
    int n, i;
    if (cp != 0u && cp != 1252u && cp != 65001u) {
        fprintf(stderr, "win32_sdl: MultiByteToWideChar codepage %u is not "
                        "ASCII-compatible and is not implemented\n", cp);
        abort();
    }
    n = srclen < 0 ? (int)strlen((const char *)s) + 1 : srclen;
    for (i = 0; i < n; i++)
        if (s[i] > 0x7F) {
            fprintf(stderr, "win32_sdl: MultiByteToWideChar got a non-ASCII "
                            "byte 0x%02x; this layer only widens ASCII\n", s[i]);
            abort();
        }
    if (dstlen == 0) { ret_std(C, (uint32_t)n, 6); return; }
    if (n > dstlen) { ret_std(C, 0, 6); return; }
    for (i = 0; i < n; i++) WR16(dst + (uint32_t)i * 2u, s[i]);
    ret_std(C, (uint32_t)n, 6);
}

/* ---- USER32: window lifecycle ------------------------------------------ */

void imp_USER32_GetDesktopWindow(CPU *C) { ret_std(C, HWND_DESKTOP_TOK, 0); }

/*
 * MessageBoxA -- printed, never swallowed.
 *
 * There is no dialog to put up here, but the TEXT is the point: the game uses
 * this to report a condition it thinks the player must see, and a port that
 * silently returns IDOK turns "your video card is unsupported" into a feature
 * that mysteriously does not work. So it goes to stderr, loudly, with its
 * caption.
 *
 * The RETURN is the part that cannot be honest, and it is worth being clear
 * about which way it is dishonest. A message box asking a question gets IDOK
 * here -- the default, and what a player clicking through would most often
 * pick -- but nothing has established that OK is the right answer for any
 * particular prompt, and a Yes/No that means "delete your save?" would be
 * answered without being asked. The button style is printed so that case is
 * visible rather than silent.
 */
/* Icons and cursors are window decoration this host does not draw. A distinct
   non-zero token per call keeps them telling apart if anything compares them,
   and nothing here pretends to load an image. */
void imp_USER32_LoadIconA(CPU *C)   { static uint32_t t = 0x00E10000u; ret_std(C, ++t, 2); }
void imp_USER32_LoadCursorA(CPU *C) { static uint32_t t = 0x00E20000u; ret_std(C, ++t, 2); }


/* ---- the message pump ---------------------------------------------------
 *
 * The guest runs a Win32 message loop and expects to drive the window through
 * it. SDL owns the real event queue here, so this pumps SDL and translates the
 * few events the game's loop actually reacts to into Win32 messages.
 *
 * PeekMessageA returning FALSE means "no messages", which is the normal state
 * of an idle frame -- so this is one of the few places where returning nothing
 * is the correct answer rather than a stub. The distinction that matters is
 * WM_QUIT: it must be delivered when SDL says the window closed, or the game
 * never exits and the port looks hung.
 */
#define WM_QUIT     0x0012u
#define WM_CLOSE    0x0010u
#define WM_ACTIVATE 0x0006u

static int g_quit_posted;

/* MSG: hwnd, message, wParam, lParam, time, pt.x, pt.y */
static void put_msg(uint32_t p, uint32_t msg, uint32_t wp, uint32_t lp)
{
    if (!p) return;
    WR32(p +  0u, HWND_MAIN_TOK);
    WR32(p +  4u, msg);
    WR32(p +  8u, wp);
    WR32(p + 12u, lp);
    WR32(p + 16u, 0);
    WR32(p + 20u, 0);
    WR32(p + 24u, 0);
}

/* Drain SDL and note anything the guest's loop needs to hear about. */
static int pump_sdl(void)
{
    SDL_Event e;
    while (SDL_PollEvent(&e)) {
        if (e.type == SDL_EVENT_QUIT ||
            e.type == SDL_EVENT_WINDOW_CLOSE_REQUESTED)
            g_quit_posted = 1;
    }
    return g_quit_posted;
}

void imp_USER32_PeekMessageA(CPU *C)
{
    /* (lpMsg, hWnd, filterMin, filterMax, wRemoveMsg) */
    if (pump_sdl()) {
        put_msg(A(0), WM_QUIT, 0, 0);
        ret_std(C, 1, 5);
        return;
    }
    ret_std(C, 0, 5);                     /* no messages -- the idle case */
}

void imp_USER32_GetMessageA(CPU *C)
{
    /* Blocking in Win32. Returns 0 on WM_QUIT, which is how the loop ends. */
    while (!pump_sdl()) SDL_Delay(1);
    put_msg(A(0), WM_QUIT, 0, 0);
    ret_std(C, 0, 4);
}

void imp_USER32_TranslateMessage(CPU *C) { ret_std(C, 0, 1); }

void imp_USER32_DispatchMessageA(CPU *C)
{
    /* The guest's own WndProc is registered but never invoked here: nothing
       synthesises the messages it would need, and calling it with an invented
       one would run game code on an event that did not happen. */
    ret_std(C, 0, 1);
}

void imp_USER32_DefWindowProcA(CPU *C) { ret_std(C, 0, 4); }

/* ---- window state ------------------------------------------------------ */

/* GWL_* are read and written by the engine to stash its own pointers; keeping
   them per-window rather than discarding them is the whole contract. */
#define GWL_SLOTS 8
static uint32_t g_gwl[GWL_SLOTS];

static int gwl_index(int32_t idx)
{
    /* GWL_USERDATA (-21), GWL_WNDPROC (-4), GWL_STYLE (-16), GWL_EXSTYLE (-20) */
    switch (idx) {
    case -21: return 0;
    case -4:  return 1;
    case -16: return 2;
    case -20: return 3;
    case -6:  return 4;                   /* GWL_HINSTANCE */
    case -8:  return 5;                   /* GWL_HWNDPARENT */
    case -12: return 6;                   /* GWL_ID */
    default:  return -1;
    }
}

void imp_USER32_GetWindowLongA(CPU *C)
{
    int i = gwl_index((int32_t)A(1));
    if (i < 0) {
        fprintf(stderr, "win32_sdl: GetWindowLongA index %d is not one this "
                        "host tracks -- returning 0, which may be wrong\n",
                (int)(int32_t)A(1));
        ret_std(C, 0, 2);
        return;
    }
    ret_std(C, g_gwl[i], 2);
}

void imp_USER32_SetWindowLongA(CPU *C)
{
    int i = gwl_index((int32_t)A(1));
    uint32_t old = 0;
    if (i >= 0) { old = g_gwl[i]; g_gwl[i] = A(2); }
    ret_std(C, old, 3);
}

void imp_USER32_SetClassLongA(CPU *C)
{
    /* Class-level icon and cursor changes, which this host does not draw. The
       previous value is what Win32 returns and nothing here kept one. */
    ret_std(C, 0, 3);
}

void imp_USER32_SetWindowPos(CPU *C)
{
    /* (hWnd, hWndInsertAfter, X, Y, cx, cy, uFlags) */
    const uint32_t SWP_NOMOVE = 0x0002u, SWP_NOSIZE = 0x0001u;
    uint32_t flags = A(6);
    if (g_win && hwnd_is_main(A(0))) {
        if (!(flags & SWP_NOMOVE)) SDL_SetWindowPosition(g_win, (int)A(2), (int)A(3));
        if (!(flags & SWP_NOSIZE)) SDL_SetWindowSize(g_win, (int)A(4), (int)A(5));
    }
    ret_std(C, 1, 7);
}

void imp_USER32_SetWindowTextA(CPU *C)
{
    if (g_win && hwnd_is_main(A(0)) && A(1))
        SDL_SetWindowTitle(g_win, (const char *)(uintptr_t)A(1));
    ret_std(C, 1, 2);
}

void imp_USER32_IsWindow(CPU *C)
{
    ret_std(C, (hwnd_is_main(A(0)) || hwnd_is_desktop(A(0))) ? 1u : 0u, 1);
}

void imp_USER32_GetParent(CPU *C) { ret_std(C, 0, 1); }   /* top-level */
void imp_USER32_EnableWindow(CPU *C) { ret_std(C, 0, 2); }
void imp_USER32_GetMenu(CPU *C) { ret_std(C, 0, 1); }     /* no menu bar */

void imp_USER32_GetDC(CPU *C)
{
    /* A device context is a GDI concept this host has none of. Returning NULL
       is what Win32 does on failure, and every caller must check it -- a
       non-NULL token would be dereferenced by GDI calls that do not exist. */
    ret_std(C, 0, 1);
}

void imp_USER32_GetSystemMetrics(CPU *C)
{
    /* SM_CXSCREEN 0, SM_CYSCREEN 1, SM_CXFULLSCREEN 16, SM_CYFULLSCREEN 17 */
    uint32_t idx = A(0), v = 0;
    const SDL_DisplayMode *dm = NULL;
    SDL_DisplayID d = SDL_GetPrimaryDisplay();
    if (d) dm = SDL_GetCurrentDisplayMode(d);
    switch (idx) {
    case 0: case 16: v = dm ? (uint32_t)dm->w : 800u; break;
    case 1: case 17: v = dm ? (uint32_t)dm->h : 600u; break;
    case 4:  v = 0; break;                /* SM_CYCAPTION -- borderless */
    case 32: case 33: v = 0; break;       /* SM_CXSIZEFRAME / SM_CYSIZEFRAME */
    default:
        fprintf(stderr, "win32_sdl: GetSystemMetrics(%u) is not one this host "
                        "answers -- returning 0, which may be wrong\n", idx);
        v = 0;
    }
    ret_std(C, v, 1);
}

void imp_USER32_ScreenToClient(CPU *C)
{
    /* One fullscreen window at the origin, so screen and client coincide. If
       the window is ever moved this becomes wrong, and SetWindowPos above is
       where that would start. */
    int x = 0, y = 0;
    if (g_win && hwnd_is_main(A(0))) SDL_GetWindowPosition(g_win, &x, &y);
    if (A(1)) {
        WR32(A(1),      (uint32_t)((int32_t)RD32(A(1))      - x));
        WR32(A(1) + 4u, (uint32_t)((int32_t)RD32(A(1) + 4u) - y));
    }
    ret_std(C, 1, 2);
}

void imp_USER32_PtInRect(CPU *C)
{
    /* (lprc, pt) -- POINT is passed BY VALUE, so it is two stack dwords. */
    uint32_t r = A(0);
    int32_t x = (int32_t)A(1), y = (int32_t)A(2);
    int32_t l, t, ri, b;
    if (!r) { ret_std(C, 0, 3); return; }
    l = (int32_t)RD32(r); t = (int32_t)RD32(r + 4u);
    ri = (int32_t)RD32(r + 8u); b = (int32_t)RD32(r + 12u);
    ret_std(C, (x >= l && x < ri && y >= t && y < b) ? 1u : 0u, 3);
}

void imp_USER32_MessageBoxA(CPU *C)
{
    /* (hWnd, lpText, lpCaption, uType) */
    const char *text = (const char *)(uintptr_t)A(1);
    const char *cap  = (const char *)(uintptr_t)A(2);
    uint32_t type = A(3);
    fprintf(stderr, "\n*** MessageBox [%s]\n    %s\n",
            cap ? cap : "(no caption)", text ? text : "(no text)");
    /*
     * WHO decided this. The text says what the game concluded; without the
     * caller it does not say which check concluded it, and that is the whole
     * question -- "Display failed!" (issue #22) named a symptom nobody could
     * attribute to a function. The return address is on the guest stack
     * because every emitted call site pushes one, so it costs nothing.
     */
    {
        uint32_t ra = RD32(C->esp);
        const char *nm = x86_native_name_at(ra);
        fprintf(stderr, "    raised from 0x%08x%s%s -- that is the function "
                        "that decided it, and\n"
                        "    what it tested is the thing to look at, not the "
                        "message.\n",
                ra, nm ? " " : "", nm ? nm : "");
    }
    if ((type & 0xFu) != 0u)
        fprintf(stderr, "    (button style 0x%x -- this host answers IDOK "
                        "without asking anyone)\n", type & 0xFu);
    ret_std(C, 1, 4);                     /* IDOK */
}

void imp_USER32_RegisterClassA(CPU *C)
{
    /* NO-OP with a real return: there is no class registry natively, and the
       only thing the guest does with the atom is pass it to CreateWindowExA,
       which ignores it here. The WndProc in the struct is NOT dropped silently
       -- it is remembered, because the message path will need it. */
    extern uint32_t g_wndproc;
    uint32_t wc = A(0);
    g_wndproc = RD32(wc + 4u);           /* WNDCLASSA.lpfnWndProc */
    ret_std(C, 1, 1);
}

uint32_t g_wndproc;

void imp_USER32_UnregisterClassA(CPU *C) { g_wndproc = 0; ret_std(C, 1, 2); }

void imp_USER32_CreateWindowExA(CPU *C)
{
    uint32_t name = A(2);
    int32_t w = (int32_t)A(6), h = (int32_t)A(7);
    if (g_win_live) {
        fprintf(stderr, "win32_sdl: a second window was requested; this layer "
                        "backs the guest's HWNDs with exactly one\n");
        abort();
    }
    if (w <= 0) w = 800;
    if (h <= 0) h = 600;
    g_win = SDL_CreateWindow(name ? (const char *)(uintptr_t)name : "x2native",
                             w, h, 0);
    if (!g_win) {
        fprintf(stderr, "win32_sdl: SDL_CreateWindow failed: %s\n",
                SDL_GetError());
        ret_std(C, 0, 12);
        return;
    }
    g_win_live = 1;
    ret_std(C, HWND_MAIN_TOK, 12);
}

void imp_USER32_DestroyWindow(CPU *C)
{
    if (!hwnd_is_main(A(0))) { ret_std(C, 0, 1); return; }
    SDL_DestroyWindow(g_win);
    g_win = NULL;
    g_win_live = 0;
    ret_std(C, 1, 1);
}

void imp_USER32_ShowWindow(CPU *C)
{
    if (!hwnd_is_main(A(0))) { ret_std(C, 0, 2); return; }
    if (A(1) == 0u) SDL_HideWindow(g_win); else SDL_ShowWindow(g_win);
    ret_std(C, 1, 2);
}

void imp_USER32_UpdateWindow(CPU *C)
{
    /* NO-OP, and correct: it forces a WM_PAINT for a GDI window, and nothing
       here paints through GDI -- the renderer will own the surface. */
    ret_std(C, 1, 1);
}

/* ---- USER32: geometry -------------------------------------------------- */

void imp_USER32_GetClientRect(CPU *C)
{
    uint32_t h = A(0), r = A(1);
    int w = 0, ht = 0;
    if (hwnd_is_desktop(h)) desktop_size(&w, &ht);
    else if (hwnd_is_main(h)) SDL_GetWindowSize(g_win, &w, &ht);
    else { ret_std(C, 0, 2); return; }
    guest_rect(r, 0, 0, w, ht);
    ret_std(C, 1, 2);
}

void imp_USER32_GetWindowRect(CPU *C)
{
    uint32_t h = A(0), r = A(1);
    int x = 0, y = 0, w = 0, ht = 0;
    if (hwnd_is_desktop(h)) { desktop_size(&w, &ht); guest_rect(r, 0, 0, w, ht); }
    else if (hwnd_is_main(h)) {
        SDL_GetWindowPosition(g_win, &x, &y);
        SDL_GetWindowSize(g_win, &w, &ht);
        guest_rect(r, x, y, x + w, y + ht);
    } else { ret_std(C, 0, 2); return; }
    ret_std(C, 1, 2);
}

void imp_USER32_MoveWindow(CPU *C)
{
    if (!hwnd_is_main(A(0))) { ret_std(C, 0, 6); return; }
    SDL_SetWindowPosition(g_win, (int)(int32_t)A(1), (int)(int32_t)A(2));
    SDL_SetWindowSize(g_win, (int)(int32_t)A(3), (int)(int32_t)A(4));
    ret_std(C, 1, 6);
}

void imp_USER32_AdjustWindowRect(CPU *C)
{
    /* NO-OP on the rectangle, and correct: it converts a client rect to the
       window rect a Win32 frame would need, and SDL sizes by client area. The
       rect is therefore already what the caller wants. */
    ret_std(C, 1, 3);
}

void imp_USER32_ClientToScreen(CPU *C)
{
    uint32_t h = A(0), p = A(1);
    int x = 0, y = 0;
    if (!hwnd_is_main(h)) { ret_std(C, 0, 2); return; }
    SDL_GetWindowPosition(g_win, &x, &y);
    WR32(p + 0u, RD32(p + 0u) + (uint32_t)x);
    WR32(p + 4u, RD32(p + 4u) + (uint32_t)y);
    ret_std(C, 1, 2);
}

/* ---- USER32: cursor ---------------------------------------------------- */

void imp_USER32_ShowCursor(CPU *C)
{
    /* Win32 keeps a counter, not a flag, and returns it. Callers balance
       show/hide against that value, so the counter is modelled rather than
       collapsed to SDL's boolean. */
    g_cursor_count += A(0) ? 1 : -1;
    if (g_cursor_count >= 0 && !g_cursor_shown) { SDL_ShowCursor(); g_cursor_shown = 1; }
    if (g_cursor_count < 0 && g_cursor_shown)   { SDL_HideCursor(); g_cursor_shown = 0; }
    ret_std(C, (uint32_t)g_cursor_count, 1);
}

void imp_USER32_GetCursorPos(CPU *C)
{
    float x = 0.0f, y = 0.0f;
    uint32_t p = A(0);
    SDL_GetGlobalMouseState(&x, &y);
    WR32(p + 0u, (uint32_t)(int32_t)x);
    WR32(p + 4u, (uint32_t)(int32_t)y);
    ret_std(C, 1, 1);
}

void imp_USER32_SetCursorPos(CPU *C)
{
    SDL_WarpMouseGlobal((float)(int32_t)A(0), (float)(int32_t)A(1));
    ret_std(C, 1, 2);
}

void imp_USER32_ClipCursor(CPU *C)
{
    /* SDL expresses confinement per window, and a NULL rect means release. */
    if (g_win_live) SDL_SetWindowMouseGrab(g_win, A(0) != 0u);
    ret_std(C, 1, 1);
}

void imp_USER32_SetCapture(CPU *C)
{
    SDL_CaptureMouse(true);
    ret_std(C, 0, 1);                    /* previous capture window: none */
}

void imp_USER32_ReleaseCapture(CPU *C)
{
    SDL_CaptureMouse(false);
    ret_std(C, 1, 0);
}

/* ---- native DATA exports ----------------------------------------------
 *
 * Not every import is a function. The CRT exports variables, and the guest
 * reads them straight through its IAT slot -- so a slot bound to a function,
 * or left poisoned, is wrong in different ways. These need a real, guest-
 * addressable word holding a real value.
 *
 * Each one is listed with what it means, because "return 0" is only correct
 * when 0 is the answer, and here it happens to be: _adjust_fdiv is the
 * Pentium FDIV-bug fixup flag, and no CPU this will ever run on has that bug.
 */
static uint32_t g_data_arena, g_data_used, g_data_size;

void x86_native_data_arena(uint32_t base, uint32_t size)
{
    g_data_arena = base;
    g_data_used = 0;
    g_data_size = size;
}

static uint32_t data_alloc(uint32_t v)
{
    uint32_t a;
    if (!g_data_arena || g_data_used + 4u > g_data_size) return 0;
    a = g_data_arena + g_data_used;
    g_data_used += 4u;
    *(volatile uint32_t *)(uintptr_t)a = v;
    return a;
}

uint32_t x86_native_data_export(const char *mod, const char *sym)
{
    /* The DLLs import MSVCRT and the exe imports MSVCR71: the same runtime
       under two names, so both spellings map to the same data. Checking only
       one is how _adjust_fdiv resolved for the DLLs and fell through to a
       function thunk for the exe, which then faulted when the guest read
       through it. */
    if (strcasecmp(mod, "MSVCRT.dll") == 0
        || strcasecmp(mod, "MSVCR71.dll") == 0) {
        /* int __adjust_fdiv: non-zero only on a Pentium with the FDIV erratum.
           The CRT branches on it to pick a software divide. Zero is the true
           answer here, not a placeholder. */
        if (strcmp(sym, "_adjust_fdiv") == 0) return data_alloc(0);
        /* _iob is an ARRAY, not a word: the guest reaches stderr as &_iob[2],
           so the slot must hold the array's base and the array must be real
           guest memory. crt.c owns it, because it is the code that has to
           recognise a pointer into it as a stream. */
        if (strcmp(sym, "_iob") == 0) { extern uint32_t crt_iob_base(void);
                                        return crt_iob_base(); }
        /* char *_acmdln: the raw command line the CRT parses. Empty rather
           than invented -- the game parses its own arguments, and a fabricated
           one is something it could branch on. */
        if (strcmp(sym, "_acmdln") == 0) {
            uint32_t s2 = guest_malloc(1);
            if (s2) *(volatile uint8_t *)(uintptr_t)s2 = 0;
            return data_alloc(s2);
        }
    }
    return 0;
}

