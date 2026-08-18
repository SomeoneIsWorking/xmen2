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
#include "../gpu/gpu_device.h"
#include "../d3d8/d3d8_drawcall.h"

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
/* --no-window. Read by the dialog path as well as by CreateWindowExA, which is
   why it lives up here with the window itself rather than beside its setter. */
static int         g_hide_windows;
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
        /*
         * F9 -- dump every draw of the next frame.
         *
         * Read here and NOT forwarded to the guest: F9 is not one of the
         * game's bindings, and the diagnostic must not become an input the
         * game acts on. This is the only way a person watching a live run can
         * say "this frame, the one that looks wrong" -- every other selector
         * is fixed before the run starts.
         */
        else if (e.type == SDL_EVENT_KEY_DOWN && !e.key.repeat) {
            /*
             * The first key EVER seen, named, once.
             *
             * If F9 does not arm the table, two things are indistinguishable
             * without this line: the key never reached the process (no window
             * manager, no focus, a compositor eating it) and the handler ran
             * and did nothing. One says fix the rig, the other says fix the
             * code, and a silent event loop says neither.
             */
            static int told;
            if (!told++)
                fprintf(stderr, "win32_sdl: the event loop is receiving keys "
                        "(first was key 0x%08x). F9 arms the frame table; "
                        "SIGUSR1 does the same without needing focus.\n",
                        (unsigned)e.key.key);
            if (e.key.key == SDLK_F9)
                d3d8_frame_table_arm();
        }
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

/*
 * A device context handle, in the same style as the HWNDs above: a token, not
 * a pointer, so a handle the guest invented is caught rather than dereferenced.
 */
#define HDC_MAIN_TOK 0x00010008u

void imp_USER32_GetDC(CPU *C)
{
    /*
     * This USED to return NULL, on the reasoning that a device context is a
     * GDI concept with nothing behind it and a non-NULL token would be
     * dereferenced by GDI calls that do not exist.
     *
     * The first half is right and the second was wrong, and it cost the whole
     * display. igWin32Window::open (libIGDisplay 0x10005740) calls
     * CreateWindowExA, then GetDC, and treats a NULL DC as fatal:
     *
     *     100058a9  CALL dword ptr [0x100090d8]   ; GetDC
     *     100058b1  TEST EBP,EBP
     *     100058b3  JZ  0x100059f1                ; -> return false
     *
     * That false latches the game's startup error byte and surfaces, four
     * hops later, as the "Display failed!" message box (issue #22). Returning
     * NULL was not a safe refusal -- it was the failure.
     *
     * And the GDI calls it was guarding against do not exist either. The
     * ENTIRE GDI surface this game imports is one function, GetDeviceCaps
     * (measured across the module's imports, not assumed), and that one does
     * not look at the HDC at all -- it answers from SDL's display mode. So
     * there is nothing a token can be dereferenced by.
     *
     * Only the main window and the desktop have one. Anything else still gets
     * NULL, which is what Win32 returns for an invalid HWND.
     */
    uint32_t h = A(0);
    if (h == 0u || hwnd_is_main(h) || hwnd_is_desktop(h)) {
        ret_std(C, HDC_MAIN_TOK, 1);
        return;
    }
    fprintf(stderr, "win32_sdl: GetDC for an HWND this layer does not know "
                    "(0x%08x) -- returning NULL, as Win32 does\n", h);
    ret_std(C, 0, 1);
}

void imp_USER32_ReleaseDC(CPU *C)
{
    /* Nothing was allocated, so nothing is freed. Reported as released so a
       caller checking the result does not treat it as a leak. */
    ret_std(C, 1, 2);
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

/* ---- modal dialogs ------------------------------------------------------
 *
 * The game has two of them and they are the same thing wearing different
 * clothes: USER32's MessageBoxA, and the Alchemy report box, which builds a
 * DLGTEMPLATE by hand and runs it through DialogBoxIndirectParamA (libIGCore
 * `igWin32ReportBox::doModal`). Both stop a run dead -- the report box is where
 * the engine says a library would not load, an asset is missing, or an
 * assertion failed, and until now it was the abort at USER32!DrawTextA.
 *
 * Implementing the USER32 dialog family (DialogBoxIndirectParamA, DrawTextA,
 * GetDlgItem, SendDlgItemMessageA, EndDialog) would mean writing a dialog
 * manager and a text renderer to draw a template this port has no other use
 * for. SDL already has a native modal with buttons, so the DIALOG is what gets
 * replaced, not the message: same title, same buttons, same return codes, and
 * the caller cannot tell the difference.
 *
 * The text ALWAYS goes to stderr as well, before the box is shown. A dialog
 * that is dismissed leaves no trace otherwise, and the message is usually the
 * most informative line in the whole run.
 *
 * HEADLESS. --no-window (and a machine with no display) cannot show a modal,
 * and blocking forever on one nobody can click is worse than any answer. The
 * caller names a fallback button; taking it is reported, once per dialog, with
 * the button's own label -- so a log never reads as though someone chose.
 */
int win32_sdl_dialog(const char *title, const char *text,
                     const char *const *labels, const int *ids, int n,
                     int fallback)
{
    SDL_MessageBoxButtonData btn[8];
    SDL_MessageBoxData box;
    int i, chosen = 0;
    const char *why = NULL;

    if (n < 1 || n > (int)(sizeof btn / sizeof btn[0])) {
        fprintf(stderr, "win32_sdl: a dialog with %d buttons is not something "
                        "this layer can show; refusing rather than dropping "
                        "some\n", n);
        abort();
    }
    fprintf(stderr, "\n*** %s\n%s\n", title ? title : "(no title)",
            text ? text : "(no text)");

    if (g_hide_windows)
        why = "this run is --no-window";
    else if (!SDL_GetCurrentVideoDriver())
        why = "SDL has no video driver, so there is no screen to show it on";
    if (why) {
        const char *lbl = "(unnamed)";
        for (i = 0; i < n; i++) if (ids[i] == fallback) lbl = labels[i];
        fprintf(stderr, "    NOT SHOWN: %s. Answering \"%s\" -- nobody chose "
                        "that, this host did.\n", why, lbl);
        return fallback;
    }

    memset(btn, 0, sizeof btn);
    for (i = 0; i < n; i++) {
        btn[i].buttonID = ids[i];
        btn[i].text = labels[i];
        if (ids[i] == fallback)
            btn[i].flags = SDL_MESSAGEBOX_BUTTON_RETURNKEY_DEFAULT
                         | SDL_MESSAGEBOX_BUTTON_ESCAPEKEY_DEFAULT;
    }
    memset(&box, 0, sizeof box);
    box.flags = SDL_MESSAGEBOX_WARNING;
    box.window = g_win_live ? g_win : NULL;
    box.title = title;
    box.message = text;
    box.numbuttons = n;
    box.buttons = btn;
    if (!SDL_ShowMessageBox(&box, &chosen)) {
        fprintf(stderr, "    SDL could not show it (%s). Answering the "
                        "fallback (%d) rather than blocking.\n",
                SDL_GetError(), fallback);
        return fallback;
    }
    fprintf(stderr, "    -> answered %d\n", chosen);
    return chosen;
}

void imp_USER32_MessageBoxA(CPU *C)
{
    /* (hWnd, lpText, lpCaption, uType) */
    const char *text = (const char *)(uintptr_t)A(1);
    const char *cap  = (const char *)(uintptr_t)A(2);
    uint32_t type = A(3);
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
    /*
     * The button set, from MB_* in uType's low nibble. Only the styles the
     * game actually uses are here; a style this layer does not know is not
     * approximated with OK, because the caller branches on WHICH button came
     * back and an invented set silently answers a question nobody asked.
     */
    {
        static const char *const ok[]      = { "OK" };
        static const int         ok_id[]   = { 1 };                /* IDOK */
        static const char *const okc[]     = { "OK", "Cancel" };
        static const int         okc_id[]  = { 1, 2 };             /* IDOK/IDCANCEL */
        static const char *const yn[]      = { "Yes", "No" };
        static const int         yn_id[]   = { 6, 7 };             /* IDYES/IDNO */
        static const char *const rc[]      = { "Retry", "Cancel" };
        static const int         rc_id[]   = { 4, 2 };             /* IDRETRY/IDCANCEL */
        const char *const *lbl = ok; const int *ids = ok_id;
        int n = 1, fallback = 1;
        switch (type & 0xFu) {
        case 0: break;                                   /* MB_OK */
        case 1: lbl = okc; ids = okc_id; n = 2; fallback = 2; break;
        case 4: lbl = yn;  ids = yn_id;  n = 2; fallback = 7; break;
        case 5: lbl = rc;  ids = rc_id;  n = 2; fallback = 2; break;
        default:
            fprintf(stderr, "win32_sdl: MessageBoxA button style 0x%x is not "
                            "implemented -- the caller branches on which "
                            "button, so answering one it did not offer would "
                            "be a decision this host invented\n", type & 0xFu);
            abort();
        }
        ret_std(C, (uint32_t)win32_sdl_dialog(cap ? cap : "X-Men Legends II",
                                              text ? text : "(no text)",
                                              lbl, ids, n, fallback), 4);
    }
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

void win32_sdl_hide_windows(int hide) { g_hide_windows = hide; }
/* Asked by anything that must behave headlessly without owning the flag --
   the audio device is the first, since a run with no window is a run nobody
   is listening to. */
int  win32_sdl_windows_hidden(void) { return g_hide_windows; }

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
                             w, h,
                             g_hide_windows ? SDL_WINDOW_HIDDEN : 0);
    if (!g_win) {
        fprintf(stderr, "win32_sdl: SDL_CreateWindow failed: %s\n",
                SDL_GetError());
        ret_std(C, 0, 12);
        return;
    }
    g_win_live = 1;
    /*
     * WHICH display the window went to, said out loud.
     *
     * SDL picks the video driver, and on a Wayland session it prefers Wayland
     * and ignores DISPLAY -- so a run launched onto an Xvfb screen renders to
     * the user's real desktop instead, and a screenshot of the Xvfb root comes
     * back black. That black image reads as "the renderer draws nothing". One
     * line here is the difference between that and the truth.
     */
    printf("win32_sdl: %swindow %dx%d on SDL video driver \"%s\"%s%s\n",
           g_hide_windows ? "HIDDEN " : "", w, h,
           SDL_GetCurrentVideoDriver() ? SDL_GetCurrentVideoDriver() : "(none)",
           getenv("DISPLAY") ? "  DISPLAY=" : "",
           getenv("DISPLAY") ? getenv("DISPLAY") : "");
    fflush(stdout);
    ret_std(C, HWND_MAIN_TOK, 12);
}

void imp_USER32_DestroyWindow(CPU *C)
{
    if (!hwnd_is_main(A(0))) { ret_std(C, 0, 1); return; }
    /* SDL_GPU owns a swapchain surface and its synchronisation objects for a
       claimed window. Destroying the SDL_Window first leaves those children
       attached to an object that no longer exists; the later GPU teardown
       then destroys its Vulkan device with live semaphores and its instance
       with a live VkSurfaceKHR. Release the claim while both owners are still
       valid. This is also the order gpu_selftest uses. */
    gpu_device_attach_window(NULL);
    SDL_DestroyWindow(g_win);
    g_win = NULL;
    g_win_live = 0;
    ret_std(C, 1, 1);
}

void imp_USER32_ShowWindow(CPU *C)
{
    if (!hwnd_is_main(A(0))) { ret_std(C, 0, 2); return; }
    /* A headless run stays headless: the guest calls ShowWindow(SW_SHOW) at
       startup, and honouring it would undo --no-window one instruction after
       the window was created hidden. */
    if (A(1) == 0u || g_hide_windows) SDL_HideWindow(g_win);
    else SDL_ShowWindow(g_win);
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


/* ---- keyboard layout ---------------------------------------------------
 *
 * MapVirtualKeyA, which XMen2.exe uses to build a scancode -> character table
 * for all 256 scancodes at input init (FUN_00629210, the loop at 0x006292e0):
 * MapVirtualKeyA(scancode, MAPVK_VSC_TO_VK) then MapVirtualKeyA(vk,
 * MAPVK_VK_TO_CHAR), once per scancode, so it can print a key's name.
 *
 * THE LAYOUT IS US, AND THAT IS A CHOICE, not a fact about the machine. Win32
 * answers from the active keyboard layout; this host has none, and reading the
 * X11/Wayland layout through SDL would still not give the VK numbering this
 * function is defined in terms of. US is what the game's own fixups assume --
 * it special-cases the result 0xb4 (an acute accent, which is what VK_OEM_7
 * produces on several European layouts) and rewrites it to 0x27, an
 * apostrophe -- so answering as US is answering the way the caller expects.
 * A non-US user sees key NAMES from a US layout; the keys themselves come from
 * SDL scancodes and are unaffected.
 *
 * Everything outside the table answers 0, which is what Win32 returns for a
 * code with no mapping. That matters here: 256 scancodes are queried and most
 * of them do not exist.
 */
#define MAPVK_VK_TO_VSC     0
#define MAPVK_VSC_TO_VK     1
#define MAPVK_VK_TO_CHAR    2
#define MAPVK_VSC_TO_VK_EX  3

/* Set-1 scancode -> virtual-key code. Index is the scancode; 0 means none.
   This is the same numbering DirectInput indexes its keyboard block by, which
   is why the DIK_ table in dinput_device.c and this one agree by construction
   rather than by coincidence. */
static const unsigned char VK_OF_SCANCODE[128] = {
    /* 00 */ 0,    0x1B, '1',  '2',  '3',  '4',  '5',  '6',
    /* 08 */ '7',  '8',  '9',  '0',  0xBD, 0xBB, 0x08, 0x09,
    /* 10 */ 'Q',  'W',  'E',  'R',  'T',  'Y',  'U',  'I',
    /* 18 */ 'O',  'P',  0xDB, 0xDD, 0x0D, 0x11, 'A',  'S',
    /* 20 */ 'D',  'F',  'G',  'H',  'J',  'K',  'L',  0xBA,
    /* 28 */ 0xDE, 0xC0, 0x10, 0xDC, 'Z',  'X',  'C',  'V',
    /* 30 */ 'B',  'N',  'M',  0xBC, 0xBE, 0xBF, 0x10, 0x6A,
    /* 38 */ 0x12, ' ',  0x14, 0x70, 0x71, 0x72, 0x73, 0x74,
    /* 40 */ 0x75, 0x76, 0x77, 0x78, 0x79, 0x90, 0x91, 0x67,
    /* 48 */ 0x68, 0x69, 0x6D, 0x64, 0x65, 0x66, 0x6B, 0x61,
    /* 50 */ 0x62, 0x63, 0x60, 0x6E, 0,    0,    0,    0x7A,
    /* 58 */ 0x7B, 0,    0,    0,    0,    0,    0,    0,
    /* 60 */ 0,0,0,0,0,0,0,0,
    /* 68 */ 0,0,0,0,0,0,0,0,
    /* 70 */ 0,0,0,0,0,0,0,0,
    /* 78 */ 0,0,0,0,0,0,0,0
};

/* The EXTENDED half, which DirectInput encodes at scancode | 0x80 and which
   Win32 reaches through MAPVK_VSC_TO_VK_EX. Listed as pairs because it is
   sparse -- a 128-entry array of mostly zeros would hide how few there are. */
static const struct { unsigned char sc, vk; } VK_OF_SCANCODE_EXT[] = {
    { 0x9C, 0x0D },   /* numpad enter */
    { 0x9D, 0x11 },   /* right control */
    { 0xB5, 0x6F },   /* numpad divide */
    { 0xB8, 0x12 },   /* right alt */
    { 0xC7, 0x24 },   /* home */
    { 0xC8, 0x26 },   /* up */
    { 0xC9, 0x21 },   /* page up */
    { 0xCB, 0x25 },   /* left */
    { 0xCD, 0x27 },   /* right */
    { 0xCF, 0x23 },   /* end */
    { 0xD0, 0x28 },   /* down */
    { 0xD1, 0x22 },   /* page down */
    { 0xD2, 0x2D },   /* insert */
    { 0xD3, 0x2E },   /* delete */
    { 0xDB, 0x5B }, { 0xDC, 0x5C }, { 0xDD, 0x5D }
};

/* Virtual key -> the UNSHIFTED character it produces, or 0 for a key that
   produces none. Letters answer uppercase, which is what Win32 does. */
static uint32_t vk_to_char(uint32_t vk)
{
    if ((vk >= 'A' && vk <= 'Z') || (vk >= '0' && vk <= '9')) return vk;
    switch (vk) {
    case 0x20: return ' ';
    case 0x0D: return '\r';
    case 0x08: return '\b';
    case 0x09: return '\t';
    case 0x1B: return 0x1B;
    case 0xBA: return ';';   case 0xBB: return '=';
    case 0xBC: return ',';   case 0xBD: return '-';
    case 0xBE: return '.';   case 0xBF: return '/';
    case 0xC0: return '`';
    case 0xDB: return '[';   case 0xDC: return '\\';
    case 0xDD: return ']';   case 0xDE: return '\'';
    case 0x6A: return '*';   case 0x6B: return '+';
    case 0x6D: return '-';   case 0x6E: return '.';
    case 0x6F: return '/';
    default:
        if (vk >= 0x60 && vk <= 0x69) return (uint32_t)('0' + (vk - 0x60));
        return 0;            /* F-keys, modifiers, arrows: no character */
    }
}

void imp_USER32_MapVirtualKeyA(CPU *C)
{
    uint32_t code = A(0), type = A(1), i;

    switch (type) {
    case MAPVK_VSC_TO_VK:
    case MAPVK_VSC_TO_VK_EX:
        if (code < 128u) { ret_std(C, VK_OF_SCANCODE[code], 2); return; }
        for (i = 0; i < sizeof VK_OF_SCANCODE_EXT / sizeof VK_OF_SCANCODE_EXT[0]; i++)
            if (VK_OF_SCANCODE_EXT[i].sc == code) {
                ret_std(C, VK_OF_SCANCODE_EXT[i].vk, 2);
                return;
            }
        ret_std(C, 0, 2);
        return;
    case MAPVK_VK_TO_VSC:
        for (i = 1; i < 128u; i++)
            if (VK_OF_SCANCODE[i] == code) { ret_std(C, i, 2); return; }
        ret_std(C, 0, 2);
        return;
    case MAPVK_VK_TO_CHAR:
        ret_std(C, vk_to_char(code), 2);
        return;
    default:
        /* A map type this host does not know. Answering 0 would be
           indistinguishable from "no mapping" and the caller would believe
           it, so say which. */
        {
            static uint32_t said = 0xFFFFFFFFu;
            if (said != type) {
                said = type;
                fprintf(stderr, "win32_sdl: MapVirtualKeyA map type %u is not "
                                "implemented; answering 0, which the caller "
                                "will read as 'no mapping'.\n", type);
            }
        }
        ret_std(C, 0, 2);
        return;
    }
}
