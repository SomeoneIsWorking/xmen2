#include "win32_events.h"

#include "win32_mouse.h"
#include "win32_pointer.h"
#include "x86rt.h"
#include "x86rt_native.h"

#include "d3d8_drawcall.h"
#include "rmlui_ui.h"
#include "../input/touch_runtime.h"

#include <SDL3/SDL.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define A(i) RD32(C->esp + 4u + (uint32_t)(i) * 4u)

static SDL_Window *g_window;
static uint32_t g_hwnd;
static uint32_t g_registered_wndproc;
static uint32_t g_wndproc;
static int g_hidden;
/* Track only a hide performed by this bridge. A hidden/detached test window
   must not impose a cursor state, but it must undo a hide it previously
   owned before giving the desktop back. */
static int g_cursor_hidden;
static X2Win32Mouse g_mouse;

static void ret_std(CPU *C, uint32_t eax, int nargs)
{
    C->eax = eax;
    C->esp += 4u + (uint32_t)nargs * 4u;
}

static void set_cursor_visible(int visible)
{
    int applied = visible ? SDL_ShowCursor() : SDL_HideCursor();

    if (applied)
        return;
    fprintf(stderr, "win32 events: SDL could not %s the OS cursor: %s\n",
            visible ? "show" : "hide", SDL_GetError());
    abort();
}

static void apply_cursor_policy(void)
{
    int show;

    if (!g_window || g_hidden) {
        if (g_cursor_hidden) {
            set_cursor_visible(1);
            g_cursor_hidden = 0;
        }
        return;
    }
    show = x2_win32_mouse_os_cursor_visible(&g_mouse);
    if (show && g_cursor_hidden) {
        set_cursor_visible(1);
        g_cursor_hidden = 0;
    } else if (!show && !g_cursor_hidden) {
        set_cursor_visible(0);
        g_cursor_hidden = 1;
    }
}

void x2_win32_events_window(SDL_Window *window, uint32_t hwnd, int hidden)
{
    if (!window) {
        x2_touch_runtime_cancel();
        x2_win32_pointer_window(NULL);
        x2_win32_mouse_window_state(&g_mouse, 0, 0, 0);
        apply_cursor_policy();
        g_window = NULL;
        g_hwnd = 0;
        return;
    }

    g_window = window;
    x2_win32_pointer_window(window);
    x2_touch_runtime_window(window);
    g_hwnd = hwnd;
    g_hidden = hidden != 0;
    {
        SDL_WindowFlags flags = SDL_GetWindowFlags(window);
        x2_win32_mouse_window_state(
            &g_mouse, hidden,
            (flags & SDL_WINDOW_INPUT_FOCUS) != 0u,
            (flags & SDL_WINDOW_MOUSE_FOCUS) != 0u);
    }
    x2_win32_mouse_overlay(&g_mouse, x2_ui_captures_input());
    apply_cursor_policy();
}

void x2_win32_events_hide_window(int hidden)
{
    g_hidden = hidden != 0;
    x2_win32_mouse_window_state(&g_mouse, hidden,
                                g_mouse.window_focused,
                                g_mouse.pointer_inside);
    apply_cursor_policy();
}

void x2_win32_events_register_wndproc(uint32_t wndproc)
{
    g_registered_wndproc = wndproc;
}

uint32_t x2_win32_events_registered_wndproc(void)
{
    return g_registered_wndproc;
}

void x2_win32_events_set_wndproc(uint32_t wndproc)
{
    g_wndproc = wndproc;
}

void x2_win32_events_modal(int visible)
{
    x2_win32_mouse_modal(&g_mouse, visible);
    apply_cursor_policy();
}

int x2_win32_events_guest_show_cursor(int show)
{
    return x2_win32_mouse_guest_show_cursor(&g_mouse, show);
}

int x2_win32_events_client_to_screen(int32_t *x, int32_t *y)
{
    return x2_win32_pointer_client_to_screen(x, y);
}

int x2_win32_events_screen_to_client(int32_t *x, int32_t *y)
{
    return x2_win32_pointer_screen_to_client(x, y);
}

int x2_win32_events_get_cursor_pos(int32_t *x, int32_t *y)
{
    return x2_win32_pointer_get_cursor_pos(x, y);
}

int x2_win32_events_set_cursor_pos(int32_t x, int32_t y)
{
    return x2_win32_pointer_set_cursor_pos(x, y);
}

static void put_msg(uint32_t p, const X2Win32Message *message)
{
    if (!p)
        return;
    WR32(p + 0u, message->hwnd);
    WR32(p + 4u, message->message);
    WR32(p + 8u, message->wparam);
    WR32(p + 12u, message->lparam);
    WR32(p + 16u, message->time);
    WR32(p + 20u, (uint32_t)message->screen_x);
    WR32(p + 24u, (uint32_t)message->screen_y);
}

static void post_message_or_abort(const X2Win32Message *message,
                                  const char *kind)
{
    if (x2_win32_message_post(&g_mouse, message))
        return;
    fprintf(stderr, "win32 events: ordered queue filled while posting %s; "
                    "refusing to discard input\n", kind);
    abort();
}

static void post_activation(int active, uint64_t timestamp)
{
    X2Win32Message message;

    memset(&message, 0, sizeof message);
    message.hwnd = g_hwnd;
    message.message = X2_WM_ACTIVATE;
    message.wparam = active ? X2_WA_ACTIVE : X2_WA_INACTIVE;
    message.time = (uint32_t)(timestamp / 1000000u);
    post_message_or_abort(&message, "WM_ACTIVATE");
}

static void pump_sdl(void)
{
    SDL_Event event;

    while (SDL_PollEvent(&event)) {
        X2TouchPointer touch_pointer;
        if (event.type == SDL_EVENT_QUIT ||
            event.type == SDL_EVENT_WINDOW_CLOSE_REQUESTED) {
            if (!x2_win32_message_post_quit(&g_mouse)) {
                fprintf(stderr, "win32 events: ordered queue filled while "
                                "posting WM_QUIT\n");
                abort();
            }
            continue;
        }

        if (event.type == SDL_EVENT_WINDOW_FOCUS_GAINED) {
            x2_win32_mouse_window_state(&g_mouse, g_hidden, 1,
                                        g_mouse.pointer_inside);
            post_activation(1, event.window.timestamp);
        } else if (event.type == SDL_EVENT_WINDOW_FOCUS_LOST) {
            x2_win32_mouse_window_state(&g_mouse, g_hidden, 0,
                                        g_mouse.pointer_inside);
            post_activation(0, event.window.timestamp);
        } else if (event.type == SDL_EVENT_WINDOW_MOUSE_ENTER) {
            x2_win32_mouse_window_state(&g_mouse, g_hidden,
                                        g_mouse.window_focused, 1);
        } else if (event.type == SDL_EVENT_WINDOW_MOUSE_LEAVE) {
            x2_win32_mouse_window_state(&g_mouse, g_hidden,
                                        g_mouse.window_focused, 0);
        }
        x2_touch_runtime_lifecycle_event(&event);

        if (x2_ui_handle_event(&event)) {
            x2_win32_mouse_overlay(&g_mouse, x2_ui_captures_input());
            apply_cursor_policy();
            continue;
        }
        if (x2_touch_runtime_event(&event, &touch_pointer)) {
            x2_win32_pointer_translate_touch(&touch_pointer, &g_mouse, g_hwnd);
            continue;
        }
        x2_win32_mouse_overlay(&g_mouse, x2_ui_captures_input());

        if (event.type == SDL_EVENT_MOUSE_MOTION ||
            event.type == SDL_EVENT_MOUSE_BUTTON_DOWN ||
            event.type == SDL_EVENT_MOUSE_BUTTON_UP) {
            x2_win32_pointer_translate_mouse(&event, &g_mouse, g_hwnd);
        } else if (event.type == SDL_EVENT_KEY_DOWN && !event.key.repeat) {
            static int told;
            if (!told++)
                fprintf(stderr, "win32 events: event loop receives keys "
                        "(first 0x%08x); F9 arms the frame table\n",
                        (unsigned)event.key.key);
            if (event.key.key == SDLK_F9)
                d3d8_frame_table_arm();
        }
        apply_cursor_policy();
    }

    x2_win32_mouse_overlay(&g_mouse, x2_ui_captures_input());
    apply_cursor_policy();
}

void imp_USER32_PeekMessageA(CPU *C)
{
    X2Win32Message message;

    pump_sdl();
    if (x2_win32_message_take(&g_mouse, A(1), A(2), A(3),
                              (A(4) & 1u) != 0u, &message)) {
        put_msg(A(0), &message);
        ret_std(C, 1, 5);
        return;
    }
    ret_std(C, 0, 5);
}

void imp_USER32_GetMessageA(CPU *C)
{
    X2Win32Message message;

    for (;;) {
        pump_sdl();
        if (x2_win32_message_take(&g_mouse, A(1), A(2), A(3), 1, &message))
            break;
        SDL_Delay(1);
    }
    put_msg(A(0), &message);
    ret_std(C, message.message == X2_WM_QUIT ? 0u : 1u, 4);
}

void imp_USER32_TranslateMessage(CPU *C)
{
    ret_std(C, 0, 1);
}

void imp_USER32_DispatchMessageA(CPU *C)
{
    uint32_t p = A(0);
    CPU call;

    if (!p || !g_wndproc) {
        ret_std(C, 0, 1);
        return;
    }
    call = *C;
    call.esp -= 16u;
    WR32(call.esp + 0u, RD32(p + 0u));
    WR32(call.esp + 4u, RD32(p + 4u));
    WR32(call.esp + 8u, RD32(p + 8u));
    WR32(call.esp + 12u, RD32(p + 12u));
    x86_guest_call_args(&call, g_wndproc, 16u);
    ret_std(C, call.eax, 1);
}
