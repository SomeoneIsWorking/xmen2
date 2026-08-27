#include "win32_mouse.h"

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int checks;

static void check(int condition, const char *expression, int line)
{
    if (!condition) {
        fprintf(stderr, "test_win32_mouse:%d: check failed: %s\n",
                line, expression);
        exit(1);
    }
    checks++;
}

#define CHECK(c) check((c), #c, __LINE__)

static X2Win32Message message(uint32_t hwnd, uint32_t id)
{
    X2Win32Message result;
    memset(&result, 0, sizeof result);
    result.hwnd = hwnd;
    result.message = id;
    return result;
}

static void queue_contract(void)
{
    X2Win32Mouse mouse = {0};
    X2Win32Message first = message(4u, 0x100u);
    X2Win32Message second = message(4u, 0x200u);
    X2Win32Message found;
    size_t i;

    CHECK(x2_win32_message_post(&mouse, &first));
    CHECK(x2_win32_message_post(&mouse, &second));
    CHECK(x2_win32_message_take(&mouse, 0u, 0u, 0u, 0, &found));
    CHECK(found.message == first.message);
    CHECK(mouse.message_count == 2u);
    CHECK(x2_win32_message_take(&mouse, 0u, 0u, 0u, 1, &found));
    CHECK(found.message == first.message);
    CHECK(x2_win32_message_take(&mouse, 0u, 0u, 0u, 1, &found));
    CHECK(found.message == second.message);
    CHECK(mouse.message_count == 0u);

    first = message(4u, 0x100u);
    second = message(8u, 0x200u);
    CHECK(x2_win32_message_post(&mouse, &first));
    CHECK(x2_win32_message_post(&mouse, &second));
    CHECK(x2_win32_message_take(&mouse, 8u, 0x200u, 0x200u, 1, &found));
    CHECK(found.message == 0x200u);
    CHECK(x2_win32_message_take(&mouse, 0u, 0u, 0u, 1, &found));
    CHECK(found.message == 0x100u);

    first = message(4u, 0x100u);
    CHECK(x2_win32_message_post(&mouse, &first));
    CHECK(x2_win32_message_post_quit(&mouse));
    CHECK(x2_win32_message_post_quit(&mouse));
    CHECK(mouse.message_count == 2u);
    CHECK(x2_win32_message_take(&mouse, 99u, 0x500u, 0x600u, 1, &found));
    CHECK(found.message == X2_WM_QUIT);
    CHECK(x2_win32_message_take(&mouse, 0u, 0u, 0u, 1, &found));
    CHECK(found.message == 0x100u);

    for (i = 0; i < X2_WIN32_MESSAGE_CAPACITY; i++)
        CHECK(x2_win32_message_post(&mouse, &first));
    CHECK(!x2_win32_message_post(&mouse, &first));
}

static void coordinate_contract(void)
{
    int32_t host_x, host_y, x, y;

    CHECK(x2_win32_mouse_map_point(400, 300, 800, 600, 800, 600, &x, &y));
    CHECK(x == 400 && y == 300);

    /* 4:3 game content pillarboxed into a 16:9 host window. */
    CHECK(x2_win32_mouse_map_point(640, 360, 1280, 720, 800, 600, &x, &y));
    CHECK(x == 400 && y == 300);
    CHECK(x2_win32_mouse_map_point(0, 0, 1280, 720, 800, 600, &x, &y));
    CHECK(x == 0 && y == 0);
    CHECK(x2_win32_mouse_map_point(1279, 719, 1280, 720, 800, 600, &x, &y));
    CHECK(x == 799 && y == 599);

    /* 4:3 content letterboxed inside a square host window. */
    CHECK(x2_win32_mouse_map_point(400, 100, 800, 800, 800, 600, &x, &y));
    CHECK(x == 400 && y == 0);
    CHECK(x2_win32_mouse_map_point(400, 699, 800, 800, 800, 600, &x, &y));
    CHECK(x == 400 && y == 599);

    /* A HiDPI window uses logical SDL coordinates on both sides of this map;
       pixel density therefore does not change the result. */
    CHECK(x2_win32_mouse_map_point(960, 540, 1920, 1080,
                                   1280, 720, &x, &y));
    CHECK(x == 640 && y == 360);
    CHECK(x2_win32_mouse_pack_point(640, 360) == 0x01680280u);

    CHECK(x2_win32_mouse_unmap_point(400, 300, 1280, 720,
                                     800, 600, &host_x, &host_y));
    CHECK(host_x == 640 && host_y == 360);
    CHECK(x2_win32_mouse_map_point(host_x, host_y, 1280, 720,
                                   800, 600, &x, &y));
    CHECK(x == 400 && y == 300);
    CHECK(x2_win32_mouse_unmap_point(0, 0, 1280, 720,
                                     800, 600, &host_x, &host_y));
    CHECK(host_x == 160 && host_y == 0);
    CHECK(x2_win32_mouse_unmap_point(799, 599, 1280, 720,
                                     800, 600, &host_x, &host_y));
    CHECK(host_x == 1119 && host_y == 719);
    CHECK(x2_win32_mouse_map_point(host_x, host_y, 1280, 720,
                                   800, 600, &x, &y));
    CHECK(x == 799 && y == 599);

    CHECK(x2_win32_mouse_unmap_point(0, 0, 800, 800,
                                     800, 600, &host_x, &host_y));
    CHECK(host_x == 0 && host_y == 100);
    CHECK(x2_win32_mouse_unmap_point(799, 599, 800, 800,
                                     800, 600, &host_x, &host_y));
    CHECK(host_x == 799 && host_y == 699);

    /* Out-of-range guest warps clamp to the presented content rather than
       placing the physical cursor in a letterbox/pillarbox bar. */
    CHECK(x2_win32_mouse_unmap_point(-20, 900, 1280, 720,
                                     800, 600, &host_x, &host_y));
    CHECK(host_x == 160 && host_y == 719);

    CHECK(!x2_win32_mouse_map_point(0, 0, 0, 600, 800, 600, &x, &y));
    CHECK(!x2_win32_mouse_map_point(0, 0, 800, 600, 32769, 600, &x, &y));
    CHECK(!x2_win32_mouse_unmap_point(0, 0, 0, 600, 800, 600,
                                      &host_x, &host_y));
    CHECK(!x2_win32_mouse_unmap_point(0, 0, 800, 600, 800, 32769,
                                      &host_x, &host_y));
}

static void mouse_message_contract(void)
{
    X2Win32Mouse mouse = {0};
    X2Win32Message found;

    CHECK(x2_win32_mouse_motion(&mouse, 4u, 10, 20, 110, 220, 30u,
                                X2_MK_LBUTTON, X2_MK_SHIFT));
    CHECK(x2_win32_mouse_button(&mouse, 4u, X2_WIN32_MOUSE_RIGHT,
                                1, 11, 21, 111, 221, 31u,
                                X2_MK_CONTROL));
    CHECK(x2_win32_mouse_button(&mouse, 4u, X2_WIN32_MOUSE_RIGHT,
                                0, 12, 22, 112, 222, 32u, 0u));

    CHECK(x2_win32_message_take(&mouse, 0u, 0u, 0u, 1, &found));
    CHECK(found.message == X2_WM_MOUSEMOVE);
    CHECK(found.wparam == (X2_MK_LBUTTON | X2_MK_SHIFT));
    CHECK(found.lparam == 0x0014000au);
    CHECK(found.screen_x == 110 && found.screen_y == 220);

    CHECK(x2_win32_message_take(&mouse, 0u, 0u, 0u, 1, &found));
    CHECK(found.message == X2_WM_RBUTTONDOWN);
    CHECK(found.wparam == (X2_MK_LBUTTON | X2_MK_RBUTTON |
                            X2_MK_CONTROL));

    CHECK(x2_win32_message_take(&mouse, 0u, 0u, 0u, 1, &found));
    CHECK(found.message == X2_WM_RBUTTONUP);
    CHECK(found.wparam == X2_MK_LBUTTON);

    CHECK(x2_win32_mouse_motion(&mouse, 4u, 20, 30, 120, 230, 40u,
                                0u, 0u));
    CHECK(x2_win32_mouse_motion(&mouse, 4u, 21, 31, 121, 231, 41u,
                                0u, 0u));
    CHECK(mouse.message_count == 1u);
    CHECK(x2_win32_message_take(&mouse, 0u, 0u, 0u, 1, &found));
    CHECK(found.message == X2_WM_MOUSEMOVE);
    CHECK(found.lparam == 0x001f0015u);

    CHECK(x2_win32_mouse_button(&mouse, 4u, X2_WIN32_MOUSE_LEFT,
                                1, 0, 0, 0, 0, 0u, 0u));
    CHECK(x2_win32_message_take(&mouse, 0u, 0u, 0u, 1, &found));
    CHECK(found.message == X2_WM_LBUTTONDOWN);
}

static void cursor_contract(void)
{
    X2Win32Mouse mouse = {0};

    x2_win32_mouse_window_state(&mouse, 0, 1, 1);
    CHECK(!x2_win32_mouse_os_cursor_visible(&mouse));
    x2_win32_mouse_overlay(&mouse, 1);
    CHECK(x2_win32_mouse_os_cursor_visible(&mouse));
    x2_win32_mouse_overlay(&mouse, 0);
    x2_win32_mouse_window_state(&mouse, 0, 0, 1);
    CHECK(x2_win32_mouse_os_cursor_visible(&mouse));
    x2_win32_mouse_window_state(&mouse, 0, 1, 0);
    CHECK(x2_win32_mouse_os_cursor_visible(&mouse));
    x2_win32_mouse_window_state(&mouse, 1, 1, 1);
    CHECK(!x2_win32_mouse_os_cursor_visible(&mouse));
    x2_win32_mouse_window_state(&mouse, 0, 1, 1);
    x2_win32_mouse_modal(&mouse, 1);
    CHECK(x2_win32_mouse_os_cursor_visible(&mouse));
    x2_win32_mouse_modal(&mouse, 0);
    CHECK(!x2_win32_mouse_os_cursor_visible(&mouse));

    CHECK(x2_win32_mouse_guest_show_cursor(&mouse, 0) == -1);
    CHECK(x2_win32_mouse_guest_show_cursor(&mouse, 1) == 0);
    CHECK(!x2_win32_mouse_os_cursor_visible(&mouse));
}

int main(void)
{
    queue_contract();
    coordinate_contract();
    mouse_message_contract();
    cursor_contract();
    printf("test_win32_mouse: %d checks passed\n", checks);
    return 0;
}
