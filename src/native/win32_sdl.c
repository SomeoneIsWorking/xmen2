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

/* ---- MSVCRT ------------------------------------------------------------ */

void imp_MSVCRT_malloc(CPU *C)
{
    /* The guest heap, not the host's: a host malloc on x86-64 returns an
       address above 4 GB and a guest pointer is 32 bits. */
    uint32_t p = guest_malloc(A(0));
    if (!p)
        fprintf(stderr, "win32_sdl: guest heap exhausted on malloc(%u)\n", A(0));
    ret_cdecl(C, p);
}

void imp_MSVCRT_free(CPU *C)
{
    guest_free(A(0));
    ret_cdecl(C, 0);
}

void imp_MSVCRT__ftol(CPU *C)
{
    /* __ftol takes its argument on the x87 stack and returns the truncated
       64-bit result in EDX:EAX. It pops one register, which the lazy x87 model
       tracks in `depth`. */
    long double v;
    int64_t r;
    if (C->depth < 1) {
        fprintf(stderr, "win32_sdl: _ftol with an empty x87 stack\n");
        abort();
    }
    v = X87_ST(C, 0);
    C->top = (C->top + 1) & 7;
    C->depth--;
    r = (int64_t)v;
    C->eax = (uint32_t)(uint64_t)r;
    C->edx = (uint32_t)((uint64_t)r >> 32);
    C->esp += 4u;                        /* __cdecl, no stack arguments */
}

const char *x86_native_name_at(uint32_t addr);

void imp_MSVCRT__initterm(CPU *C)
{
    /* Walk the function-pointer table and call each non-NULL entry through the
       recompiled dispatcher -- these are the module's static constructors, and
       skipping them would leave every global unconstructed.
     *
     * The pre-pass is the point. These tables are the ONLY reference to most
     * of their targets: they are data pointers in .rdata, so static analysis
     * never marked them as code and they have no recompiled body. Running the
     * table straight through would stop at the first one, and each rebuild
     * would reveal exactly one more. Listing every missing target first turns
     * that into one seed list.
     */
    uint32_t p = A(0), end = A(1), n = 0, missing = 0;
    for (p = A(0); p < end; p += 4u) {
        uint32_t fn = RD32(p);
        if (!fn) continue;
        n++;
        if (!x86_native_name_at(fn)) {
            if (!missing)
                fprintf(stderr, "\n*** _initterm: constructor targets with no "
                                "recompiled body.\n    These are reachable only "
                                "through this table, so static analysis never "
                                "saw them as code.\n    Seed them and re-lift; "
                                "the full list follows so this costs one pass, "
                                "not one per rebuild.\n");
            fprintf(stderr, "    0x%08x\n", fn);
            missing++;
        }
    }
    if (missing) {
        fprintf(stderr, "*** %u of %u constructor targets are missing bodies\n",
                missing, n);
        abort();
    }
    for (p = A(0); p < end; p += 4u) {
        uint32_t fn = RD32(p);
        if (fn) x86_guest_call(C, fn);
    }
    ret_cdecl(C, 0);
}

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
    if (strcasecmp(mod, "MSVCRT.dll") == 0) {
        /* int __adjust_fdiv: non-zero only on a Pentium with the FDIV erratum.
           The CRT branches on it to pick a software divide. Zero is the true
           answer here, not a placeholder. */
        if (strcmp(sym, "_adjust_fdiv") == 0) return data_alloc(0);
    }
    return 0;
}

/*
 * MSVCRT!__dllonexit -- register a static destructor.
 *
 *   _PVFV __dllonexit(_PVFV func, _PVFV **pbegin, _PVFV **pend)
 *
 * Implemented rather than stubbed, even though nothing here runs the
 * destructors yet. A stub returning `func` would look identical from the
 * caller's side while quietly dropping every registration, and the table it
 * maintains is guest-visible: the CRT reads it. Growing it by one entry per
 * call is not how MSVCRT does it (it doubles), but the OBSERVABLE state --
 * *pbegin, *pend and the entries between them -- is the same.
 */
void imp_MSVCRT___dllonexit(CPU *C)
{
    uint32_t func = A(0), pbegin = A(1), pend = A(2);
    uint32_t b = RD32(pbegin), e = RD32(pend);
    uint32_t count = b ? (e - b) / 4u : 0u;
    void *nt;

    if (!func) { ret_cdecl(C, 0); return; }
    nt = (void *)(uintptr_t)guest_realloc(b, (count + 1u) * 4u);
    if (!nt) { ret_cdecl(C, 0); return; }
    b = (uint32_t)(uintptr_t)nt;
    WR32(b + count * 4u, func);
    WR32(pbegin, b);
    WR32(pend, b + (count + 1u) * 4u);
    ret_cdecl(C, func);
}
