#include "win32_events.h"

#include "settings_store.h"
#include "win32_mouse.h"
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

typedef struct {
    int32_t window_x;
    int32_t window_y;
    uint32_t window_width;
    uint32_t window_height;
    uint32_t game_width;
    uint32_t game_height;
} MouseGeometry;

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
        x2_win32_mouse_window_state(&g_mouse, 0, 0, 0);
        apply_cursor_policy();
        g_window = NULL;
        g_hwnd = 0;
        return;
    }

    g_window = window;
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

static void mouse_geometry(MouseGeometry *geometry)
{
    const X2Settings *settings = x2_settings_store();
    int window_x, window_y, window_width, window_height;

    if (!g_window) {
        fprintf(stderr, "win32 events: cursor coordinates require an "
                        "attached guest window\n");
        abort();
    }
    if (!SDL_GetWindowPosition(g_window, &window_x, &window_y)) {
        fprintf(stderr, "win32 events: cannot read the guest window "
                        "position: %s\n", SDL_GetError());
        abort();
    }
    if (!SDL_GetWindowSize(g_window, &window_width, &window_height)) {
        fprintf(stderr, "win32 events: cannot read the guest window size: "
                        "%s\n", SDL_GetError());
        abort();
    }
    if (window_width <= 0 || window_height <= 0) {
        fprintf(stderr, "win32 events: guest window has invalid size %dx%d\n",
                window_width, window_height);
        abort();
    }
    geometry->window_x = window_x;
    geometry->window_y = window_y;
    geometry->window_width = (uint32_t)window_width;
    geometry->window_height = (uint32_t)window_height;
    geometry->game_width = settings->width;
    geometry->game_height = settings->height;
}

static int32_t coordinate_add(int32_t coordinate, int64_t offset,
                              const char *axis)
{
    int64_t result = (int64_t)coordinate + offset;

    if (result < INT32_MIN || result > INT32_MAX) {
        fprintf(stderr, "win32 events: %s cursor coordinate is outside the "
                        "guest 32-bit range\n", axis);
        abort();
    }
    return (int32_t)result;
}

int x2_win32_events_client_to_screen(int32_t *x, int32_t *y)
{
    MouseGeometry geometry;

    if (!x || !y)
        return 0;
    mouse_geometry(&geometry);
    *x = coordinate_add(*x, geometry.window_x, "horizontal");
    *y = coordinate_add(*y, geometry.window_y, "vertical");
    return 1;
}

int x2_win32_events_screen_to_client(int32_t *x, int32_t *y)
{
    MouseGeometry geometry;

    if (!x || !y)
        return 0;
    mouse_geometry(&geometry);
    *x = coordinate_add(*x, -(int64_t)geometry.window_x, "horizontal");
    *y = coordinate_add(*y, -(int64_t)geometry.window_y, "vertical");
    return 1;
}

int x2_win32_events_get_cursor_pos(int32_t *x, int32_t *y)
{
    MouseGeometry geometry;
    float global_x, global_y;
    int32_t client_x, client_y;

    if (!x || !y)
        return 0;
    mouse_geometry(&geometry);
    SDL_GetGlobalMouseState(&global_x, &global_y);
    if (!x2_win32_mouse_map_point(
            coordinate_add((int32_t)global_x,
                           -(int64_t)geometry.window_x, "horizontal"),
            coordinate_add((int32_t)global_y,
                           -(int64_t)geometry.window_y, "vertical"),
            geometry.window_width, geometry.window_height,
            geometry.game_width, geometry.game_height,
            &client_x, &client_y)) {
        fprintf(stderr, "win32 events: cannot map the physical cursor from "
                        "window %ux%u to game %ux%u\n",
                geometry.window_width, geometry.window_height,
                geometry.game_width, geometry.game_height);
        abort();
    }
    *x = coordinate_add(client_x, geometry.window_x, "horizontal");
    *y = coordinate_add(client_y, geometry.window_y, "vertical");
    return 1;
}

int x2_win32_events_set_cursor_pos(int32_t x, int32_t y)
{
    MouseGeometry geometry;
    int32_t host_x, host_y;

    mouse_geometry(&geometry);
    if (!x2_win32_mouse_unmap_point(
            coordinate_add(x, -(int64_t)geometry.window_x, "horizontal"),
            coordinate_add(y, -(int64_t)geometry.window_y, "vertical"),
            geometry.window_width, geometry.window_height,
            geometry.game_width, geometry.game_height, &host_x, &host_y)) {
        fprintf(stderr, "win32 events: cannot map the guest cursor from game "
                        "%ux%u to window %ux%u\n",
                geometry.game_width, geometry.game_height,
                geometry.window_width, geometry.window_height);
        abort();
    }
    host_x = coordinate_add(host_x, geometry.window_x, "horizontal");
    host_y = coordinate_add(host_y, geometry.window_y, "vertical");
    return SDL_WarpMouseGlobal((float)host_x, (float)host_y);
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

static uint32_t mouse_modifiers(void)
{
    SDL_Keymod modifiers = SDL_GetModState();
    uint32_t result = 0;

    if (modifiers & SDL_KMOD_SHIFT)
        result |= X2_MK_SHIFT;
    if (modifiers & SDL_KMOD_CTRL)
        result |= X2_MK_CONTROL;
    return result;
}

static uint32_t mouse_buttons(SDL_MouseButtonFlags state)
{
    uint32_t result = 0;

    if (state & SDL_BUTTON_LMASK)
        result |= X2_MK_LBUTTON;
    if (state & SDL_BUTTON_RMASK)
        result |= X2_MK_RBUTTON;
    if (state & SDL_BUTTON_MMASK)
        result |= X2_MK_MBUTTON;
    return result;
}

static void mouse_point(float host_x, float host_y,
                        int32_t *client_x, int32_t *client_y,
                        int32_t *screen_x, int32_t *screen_y)
{
    MouseGeometry geometry;
    int32_t x = (int32_t)host_x;
    int32_t y = (int32_t)host_y;

    mouse_geometry(&geometry);
    if (!x2_win32_mouse_map_point(x, y,
                                  geometry.window_width,
                                  geometry.window_height,
                                  geometry.game_width, geometry.game_height,
                                  client_x, client_y)) {
        fprintf(stderr, "win32 events: cannot map mouse coordinate (%d,%d) "
                        "from window %ux%u to game %ux%u\n",
                x, y, geometry.window_width, geometry.window_height,
                geometry.game_width, geometry.game_height);
        abort();
    }
    *screen_x = coordinate_add(*client_x, geometry.window_x, "horizontal");
    *screen_y = coordinate_add(*client_y, geometry.window_y, "vertical");
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

static void translate_mouse_event(const SDL_Event *event)
{
    int32_t client_x, client_y, screen_x, screen_y;
    uint32_t time, modifiers;
    int queued = 1;

    modifiers = mouse_modifiers();
    if (event->type == SDL_EVENT_MOUSE_MOTION) {
        mouse_point(event->motion.x, event->motion.y,
                    &client_x, &client_y, &screen_x, &screen_y);
        time = (uint32_t)(event->motion.timestamp / 1000000u);
        queued = x2_win32_mouse_motion(&g_mouse, g_hwnd,
                                       client_x, client_y,
                                       screen_x, screen_y, time,
                                       mouse_buttons(event->motion.state),
                                       modifiers);
    } else if (event->type == SDL_EVENT_MOUSE_BUTTON_DOWN ||
               event->type == SDL_EVENT_MOUSE_BUTTON_UP) {
        X2Win32MouseButton button;

        switch (event->button.button) {
        case SDL_BUTTON_LEFT: button = X2_WIN32_MOUSE_LEFT; break;
        case SDL_BUTTON_RIGHT: button = X2_WIN32_MOUSE_RIGHT; break;
        case SDL_BUTTON_MIDDLE: button = X2_WIN32_MOUSE_MIDDLE; break;
        default: return;
        }
        mouse_point(event->button.x, event->button.y,
                    &client_x, &client_y, &screen_x, &screen_y);
        time = (uint32_t)(event->button.timestamp / 1000000u);
        queued = x2_win32_mouse_button(
            &g_mouse, g_hwnd, button,
            event->type == SDL_EVENT_MOUSE_BUTTON_DOWN,
            client_x, client_y, screen_x, screen_y, time, modifiers);
    }
    if (!queued) {
        fprintf(stderr, "win32 events: ordered queue filled while posting a "
                        "mouse event; refusing to discard it\n");
        abort();
    }
}

static void pump_sdl(void)
{
    SDL_Event event;

    while (SDL_PollEvent(&event)) {
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
        if (x2_touch_runtime_event(&event)) continue;
        x2_win32_mouse_overlay(&g_mouse, x2_ui_captures_input());

        if (event.type == SDL_EVENT_MOUSE_MOTION ||
            event.type == SDL_EVENT_MOUSE_BUTTON_DOWN ||
            event.type == SDL_EVENT_MOUSE_BUTTON_UP) {
            translate_mouse_event(&event);
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
