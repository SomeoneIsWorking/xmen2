#ifndef X2_WIN32_MOUSE_H
#define X2_WIN32_MOUSE_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* The subset of USER32 messages produced by the SDL host. Keep the Win32
   values here so translation and its tests share one vocabulary. */
#define X2_WM_ACTIVATE       0x0006u
#define X2_WM_QUIT           0x0012u
#define X2_WM_MOUSEMOVE      0x0200u
#define X2_WM_LBUTTONDOWN    0x0201u
#define X2_WM_LBUTTONUP      0x0202u
#define X2_WM_RBUTTONDOWN    0x0204u
#define X2_WM_RBUTTONUP      0x0205u
#define X2_WM_MBUTTONDOWN    0x0207u
#define X2_WM_MBUTTONUP      0x0208u

#define X2_WA_INACTIVE 0u
#define X2_WA_ACTIVE   1u

#define X2_MK_LBUTTON 0x0001u
#define X2_MK_RBUTTON 0x0002u
#define X2_MK_SHIFT   0x0004u
#define X2_MK_CONTROL 0x0008u
#define X2_MK_MBUTTON 0x0010u

#define X2_WIN32_MESSAGE_CAPACITY 64u

typedef enum {
    X2_WIN32_MOUSE_LEFT,
    X2_WIN32_MOUSE_RIGHT,
    X2_WIN32_MOUSE_MIDDLE
} X2Win32MouseButton;

typedef struct {
    uint32_t hwnd;
    uint32_t message;
    uint32_t wparam;
    uint32_t lparam;
    uint32_t time;
    int32_t screen_x;
    int32_t screen_y;
} X2Win32Message;

typedef struct {
    X2Win32Message messages[X2_WIN32_MESSAGE_CAPACITY];
    size_t message_count;
    uint32_t buttons;
    int quit_posted;
    int window_hidden;
    int window_focused;
    int pointer_inside;
    int overlay_visible;
    int modal_visible;
    int guest_cursor_count;
} X2Win32Mouse;

/* Queue an ordinary thread/window message. A full queue is reported to the
   caller instead of silently dropping input. */
int x2_win32_message_post(X2Win32Mouse *mouse,
                          const X2Win32Message *message);
int x2_win32_message_post_quit(X2Win32Mouse *mouse);

/* PeekMessage/GetMessage's shared selection rule. WM_QUIT bypasses filters as
   it does on Win32. `remove` chooses PM_NOREMOVE versus PM_REMOVE. */
int x2_win32_message_take(X2Win32Mouse *mouse, uint32_t hwnd,
                          uint32_t filter_min, uint32_t filter_max,
                          int remove, X2Win32Message *message);

/* Map host-window coordinates back through the same aspect-fit rectangle used
   for presentation. The result is the logical game backbuffer coordinate
   carried in a mouse message; Y stays top-down because Alchemy performs its
   own one-and-only inversion. */
int x2_win32_mouse_map_point(int32_t x, int32_t y,
                             uint32_t window_width,
                             uint32_t window_height,
                             uint32_t game_width, uint32_t game_height,
                             int32_t *game_x, int32_t *game_y);
/* The inverse used for guest-requested cursor warps. A logical coordinate is
   placed at the centre of its fitted host-coordinate bucket where
   representable, so mapping the resulting host point returns the same logical
   point. */
int x2_win32_mouse_unmap_point(int32_t game_x, int32_t game_y,
                               uint32_t window_width,
                               uint32_t window_height,
                               uint32_t game_width, uint32_t game_height,
                               int32_t *x, int32_t *y);
uint32_t x2_win32_mouse_pack_point(int32_t x, int32_t y);

int x2_win32_mouse_motion(X2Win32Mouse *mouse, uint32_t hwnd,
                          int32_t client_x, int32_t client_y,
                          int32_t screen_x, int32_t screen_y,
                          uint32_t time, uint32_t buttons,
                          uint32_t modifiers);
int x2_win32_mouse_button(X2Win32Mouse *mouse, uint32_t hwnd,
                          X2Win32MouseButton button, int down,
                          int32_t client_x, int32_t client_y,
                          int32_t screen_x, int32_t screen_y,
                          uint32_t time, uint32_t modifiers);

/* Product cursor arbitration. The game-drawn cursor owns focused retail
   content; the OS cursor owns unfocused/outside content and native UI. */
void x2_win32_mouse_window_state(X2Win32Mouse *mouse, int hidden,
                                 int focused, int pointer_inside);
void x2_win32_mouse_overlay(X2Win32Mouse *mouse, int visible);
void x2_win32_mouse_modal(X2Win32Mouse *mouse, int visible);
int x2_win32_mouse_os_cursor_visible(const X2Win32Mouse *mouse);

/* Preserve USER32's process-wide display counter and return value without
   letting it override the one-cursor product policy above. */
int x2_win32_mouse_guest_show_cursor(X2Win32Mouse *mouse, int show);

#ifdef __cplusplus
}
#endif

#endif
