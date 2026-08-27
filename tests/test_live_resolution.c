#include "live_resolution.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

struct SDL_Window { int token; };

enum { STEP_D3D = 1, STEP_TITLE, STEP_WINDOW, STEP_SAVE };

static int checks;
static int steps[16];
static int step_count;
static int d3d_calls;
static int title_calls;
static int window_calls;
static int save_calls;
static int fail_d3d_call;
static int fail_title_call;
static int fail_window_call;
static int fail_save_call;
static uint32_t d3d_width[4], d3d_height[4];
static uint32_t title_width[4], title_height[4];
static uint32_t window_width[4], window_height[4];

static void check(int condition, const char *expression, int line)
{
    checks++;
    if (condition) return;
    fprintf(stderr, "test_live_resolution: check %d failed at line %d: %s\n",
            checks, line, expression);
    exit(1);
}

#define CHECK(c) check((c), #c, __LINE__)

int d3d8_live_resolution_apply(uint32_t width, uint32_t height,
                               char *why, int whyn)
{
    steps[step_count++] = STEP_D3D;
    d3d_width[d3d_calls] = width;
    d3d_height[d3d_calls] = height;
    d3d_calls++;
    if (d3d_calls == fail_d3d_call) {
        snprintf(why, (size_t)whyn, "D3D refusal");
        return 0;
    }
    return 1;
}

int x2_display_mode_runtime_apply(uint32_t width, uint32_t height,
                                  char *why, int whyn)
{
    steps[step_count++] = STEP_TITLE;
    title_width[title_calls] = width;
    title_height[title_calls] = height;
    title_calls++;
    if (title_calls == fail_title_call) {
        snprintf(why, (size_t)whyn, "title refusal");
        return 0;
    }
    return 1;
}

int x2_window_settings_apply(struct SDL_Window *window,
                             const X2Settings *settings,
                             char *why, int whyn)
{
    (void)window;
    steps[step_count++] = STEP_WINDOW;
    window_width[window_calls] = settings->width;
    window_height[window_calls] = settings->height;
    window_calls++;
    if (window_calls == fail_window_call) {
        snprintf(why, (size_t)whyn, "window refusal");
        return 0;
    }
    return 1;
}

int x2_settings_store_save(char *why, int whyn)
{
    steps[step_count++] = STEP_SAVE;
    save_calls++;
    if (save_calls == fail_save_call) {
        snprintf(why, (size_t)whyn, "save refusal");
        return 0;
    }
    return 1;
}

static void reset_calls(void)
{
    memset(steps, 0, sizeof steps);
    memset(d3d_width, 0, sizeof d3d_width);
    memset(d3d_height, 0, sizeof d3d_height);
    memset(title_width, 0, sizeof title_width);
    memset(title_height, 0, sizeof title_height);
    memset(window_width, 0, sizeof window_width);
    memset(window_height, 0, sizeof window_height);
    step_count = d3d_calls = title_calls = window_calls = save_calls = 0;
    fail_d3d_call = fail_title_call = fail_window_call = fail_save_call = 0;
}

static X2Settings changed(const X2Settings *before)
{
    X2Settings next = *before;
    next.width = 1920;
    next.height = 1080;
    return next;
}

int main(void)
{
    struct SDL_Window window = {1};
    X2Settings before, settings;
    char why[256];

    memset(&before, 0, sizeof before);
    before.width = 1280;
    before.height = 720;
    before.window_mode = X2_WINDOW_WINDOWED;

    settings = before;
    x2_live_resolution_select_next(&settings);
    CHECK(settings.width == 1600 && settings.height == 900);
    x2_live_resolution_select_next(&settings);
    CHECK(settings.width == 1920 && settings.height == 1080);
    settings.width = 1366;
    settings.height = 768;
    x2_live_resolution_select_next(&settings);
    CHECK(settings.width == 1280 && settings.height == 720);

    reset_calls();
    settings = changed(&before);
    CHECK(x2_live_resolution_apply(&window, &settings, &before,
                                   why, sizeof why));
    CHECK(step_count == 4 && steps[0] == STEP_D3D &&
          steps[1] == STEP_TITLE && steps[2] == STEP_WINDOW &&
          steps[3] == STEP_SAVE);
    CHECK(title_width[0] == 1920 && title_height[0] == 1080);
    CHECK(settings.width == 1920 && settings.height == 1080);

    reset_calls();
    fail_d3d_call = 1;
    settings = changed(&before);
    CHECK(!x2_live_resolution_apply(&window, &settings, &before,
                                    why, sizeof why));
    CHECK(step_count == 1 && steps[0] == STEP_D3D);
    CHECK(settings.width == 1280 && settings.height == 720);

    reset_calls();
    fail_title_call = 1;
    settings = changed(&before);
    CHECK(!x2_live_resolution_apply(&window, &settings, &before,
                                    why, sizeof why));
    CHECK(step_count == 3 && steps[0] == STEP_D3D &&
          steps[1] == STEP_TITLE && steps[2] == STEP_D3D);
    CHECK(d3d_width[1] == 1280 && d3d_height[1] == 720);
    CHECK(settings.width == 1280 && settings.height == 720);

    reset_calls();
    fail_window_call = 1;
    settings = changed(&before);
    CHECK(!x2_live_resolution_apply(&window, &settings, &before,
                                    why, sizeof why));
    CHECK(step_count == 6 && steps[0] == STEP_D3D &&
          steps[1] == STEP_TITLE && steps[2] == STEP_WINDOW &&
          steps[3] == STEP_WINDOW && steps[4] == STEP_TITLE &&
          steps[5] == STEP_D3D);
    CHECK(window_width[1] == 1280 && window_height[1] == 720);
    CHECK(title_width[1] == 1280 && title_height[1] == 720);
    CHECK(d3d_width[1] == 1280 && d3d_height[1] == 720);
    CHECK(settings.width == 1280 && settings.height == 720);

    reset_calls();
    fail_save_call = 1;
    settings = changed(&before);
    CHECK(!x2_live_resolution_apply(&window, &settings, &before,
                                    why, sizeof why));
    CHECK(step_count == 7 && steps[0] == STEP_D3D &&
          steps[1] == STEP_TITLE && steps[2] == STEP_WINDOW &&
          steps[3] == STEP_SAVE && steps[4] == STEP_WINDOW &&
          steps[5] == STEP_TITLE && steps[6] == STEP_D3D);
    CHECK(settings.width == 1280 && settings.height == 720);

    reset_calls();
    fail_window_call = 2;
    fail_save_call = 1;
    settings = changed(&before);
    CHECK(!x2_live_resolution_apply(&window, &settings, &before,
                                    why, sizeof why));
    CHECK(d3d_calls == 2);
    CHECK(strstr(why, "rollback failed") != NULL);

    printf("test_live_resolution: %d checks passed\n", checks);
    return 0;
}
