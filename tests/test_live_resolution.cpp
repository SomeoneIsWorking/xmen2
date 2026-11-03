#include "live_resolution.h"

#include "display_geometry.h"
#include "render_size.h"
#include "resolution_ladder.h"
#include "settings_store.h"
#include "x2_log.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

struct SDL_Window {
  int token;
};

enum { STEP_D3D = 1, STEP_TITLE, STEP_WINDOW, STEP_SAVE, STEP_TEXT };

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
static unsigned display_w, display_h;
static uint32_t d3d_width[4], d3d_height[4];
static uint32_t title_width[4], title_height[4];
static uint32_t window_width[4], window_height[4];

static void check(int condition, const char *expression, int line) {
  checks++;
  if (condition)
    return;
  fprintf(stderr, "test_live_resolution: check %d failed at line %d: %s\n",
          checks, line, expression);
  exit(1);
}

#define CHECK(c) check((c), #c, __LINE__)

/* The display query is the seam the ladder derives width from. Stubbing it
   here is what lets one test state every aspect ratio a player can have;
   display_w == 0 stands for "SDL cannot say". */
int x2::presentation::display_pixel_size(unsigned *width, unsigned *height) {
  if (!display_w || !display_h)
    return 0;
  *width = display_w;
  *height = display_h;
  return 1;
}

static bool host_fills_display;
static unsigned surface_w, surface_h;

bool x2::presentation::host_window_fills_display() {
  return host_fills_display;
}

int x2::presentation::window_pixel_size(SDL_Window *window, unsigned *width,
                                        unsigned *height) {
  if (!window || !surface_w || !surface_h)
    return 0;
  *width = surface_w;
  *height = surface_h;
  return 1;
}

void x2_log_error(const char *format, ...) { (void)format; }
void x2_log_info(const char *format, ...) { (void)format; }

namespace x2::d3d8 {

bool d3d8_live_resolution_active(void) { return true; }

bool d3d8_live_resolution_size(uint32_t *width, uint32_t *height) {
  (void)width;
  (void)height;
  return false;
}

int d3d8_live_resolution_apply(uint32_t width, uint32_t height, char *why,
                               int whyn) {
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

} // namespace x2::d3d8

namespace x2::native {
int display_mode_runtime_apply(uint32_t width, uint32_t height, char *why,
                               int whyn) {
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
} // namespace x2::native

namespace x2::presentation {

int window_settings_apply(SDL_Window *window,
                          const x2::config::Settings *settings, char *why,
                          int whyn) {
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

} // namespace x2::presentation

/* The text scale is derived from the output height, so a resolution the
   player accepted has to re-derive it: fonts already in memory are not
   reloaded, and without this call they keep the size the BOOT resolution
   asked for. It belongs after the save, on the success path only -- a rolled
   back change must leave the text where it was. */
namespace x2::native {
int ui_text_scale_reapply(void) {
  steps[step_count++] = STEP_TEXT;
  return 1;
}
} // namespace x2::native

static x2::config::Settings g_store;

namespace x2::config {
Settings *settings_store(void) { return &g_store; }
int settings_store_save(char *why, int whyn) {
  steps[step_count++] = STEP_SAVE;
  save_calls++;
  if (save_calls == fail_save_call) {
    snprintf(why, (size_t)whyn, "save refusal");
    return 0;
  }
  return 1;
}
} // namespace x2::config

static void reset_calls(void) {
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

static x2::config::Settings changed(const x2::config::Settings *before) {
  x2::config::Settings next = *before;
  next.width = 1920;
  next.height = 1080;
  return next;
}

int main(void) {
  struct SDL_Window window = {1};
  x2::config::Settings before, settings;
  char why[256];

  memset(&before, 0, sizeof before);
  before.width = 1280;
  before.height = 720;
  before.window_mode = x2::config::WindowMode::Windowed;

  /* Windowed, 1080p display: Native keeps the window's size, and the ladder
     offers only what the panel can show. */
  display_w = 1920;
  display_h = 1080;
  settings = before;
  settings.render_lines = x2::config::kRenderNative;
  x2::presentation::live_resolution_select_next(&settings, &window);
  CHECK(settings.render_lines == 720 && settings.width == 1280 &&
        settings.height == 720);
  x2::presentation::live_resolution_select_next(&settings, &window);
  CHECK(settings.render_lines == 1080 && settings.width == 1920 &&
        settings.height == 1080);
  x2::presentation::live_resolution_select_next(&settings, &window);
  CHECK(settings.render_lines == x2::config::kRenderNative &&
        settings.width == 1920 && settings.height == 1080);

  /* 4K: the full ladder, each width derived from the same 16:9 ratio. */
  display_w = 3840;
  display_h = 2160;
  settings.render_lines = 720;
  x2::presentation::live_resolution_select_next(&settings, &window);
  CHECK(settings.width == 1920 && settings.height == 1080);
  x2::presentation::live_resolution_select_next(&settings, &window);
  CHECK(settings.width == 2560 && settings.height == 1440);
  x2::presentation::live_resolution_select_next(&settings, &window);
  CHECK(settings.width == 3840 && settings.height == 2160);
  x2::presentation::live_resolution_select_next(&settings, &window);
  CHECK(settings.render_lines == x2::config::kRenderNative);

  /* The point of the presets: width follows the PANEL, not a 16:9 table. */
  display_w = 2560; /* 16:10 */
  display_h = 1600;
  settings.render_lines = 720;
  x2::presentation::live_resolution_select_next(&settings, &window);
  CHECK(settings.width == 1728 && settings.height == 1080);

  display_w = 3440; /* 21:9 */
  display_h = 1440;
  settings.render_lines = 720;
  x2::presentation::live_resolution_select_next(&settings, &window);
  CHECK(settings.width == 2580 && settings.height == 1080);

  /* A width that cannot be even is rounded down rather than accepted. */
  display_w = 1366;
  display_h = 768;
  settings.render_lines = x2::config::kRenderNative;
  x2::presentation::live_resolution_select_next(&settings, &window);
  CHECK(settings.width == 1280 && settings.height == 720);
  CHECK(x2::presentation::resolution_width_for(1080, 1366, 768) == 1920);

  /* No display: 16:9, because refusing to change resolution would strand the
     setting. */
  display_w = display_h = 0;
  settings.render_lines = 720;
  x2::presentation::live_resolution_select_next(&settings, &window);
  CHECK(settings.width == 1920 && settings.height == 1080);

  /* A height stored by an older build is not on the ladder; it resolves to
     Native. */
  settings.render_lines = 900;
  x2::presentation::live_resolution_select_next(&settings, &window);
  CHECK(settings.render_lines == x2::config::kRenderNative);

  /* Borderless: Native is the live surface; a preset takes the surface's
     shape. */
  display_w = 2800;
  display_h = 1840;
  surface_w = 2800;
  surface_h = 1840;
  settings = before;
  settings.window_mode = x2::config::WindowMode::Borderless;
  settings.render_lines = 2160;
  x2::presentation::live_resolution_select_next(&settings, &window);
  CHECK(settings.render_lines == x2::config::kRenderNative &&
        settings.width == 2800 && settings.height == 1840);
  x2::presentation::live_resolution_select_next(&settings, &window);
  CHECK(settings.width == 1096 && settings.height == 720);

  /* A host whose one window is the display: windowed means the display too,
     and before the window exists the display is the prediction. */
  host_fills_display = true;
  display_w = 2728;
  display_h = 1264;
  settings = before;
  CHECK(x2::presentation::render_size_resolve(&settings, nullptr) == 1 &&
        settings.width == 2728 && settings.height == 1264);
  CHECK(x2::presentation::render_size_resolve(&settings, nullptr) == 0);

  /* The surface the host actually gave differs from the prediction: the
     pump's note is followed live, once. */
  surface_w = 2680;
  surface_h = 1264;
  g_store = settings;
  reset_calls();
  x2::presentation::live_resolution_follow_surface(&window);
  CHECK(d3d_calls == 0);
  x2::presentation::live_resolution_note_surface();
  x2::presentation::live_resolution_follow_surface(&window);
  CHECK(d3d_calls == 1 && d3d_width[0] == 2680 && d3d_height[0] == 1264);
  CHECK(g_store.width == 2680 && g_store.height == 1264);
  x2::presentation::live_resolution_follow_surface(&window);
  CHECK(d3d_calls == 1);
  host_fills_display = false;
  surface_w = surface_h = 0;

  {
    char label[16];
    CHECK(x2::presentation::resolution_label(1080, label, sizeof label) == 5 &&
          strcmp(label, "1080p") == 0);
    CHECK(x2::presentation::resolution_label(x2::config::kRenderNative, label,
                                             sizeof label) == 6 &&
          strcmp(label, "Native") == 0);
    CHECK(x2::presentation::resolution_label(1080, label, 4) == 0 &&
          label[0] == '\0');
  }

  reset_calls();
  settings = changed(&before);
  CHECK(x2::presentation::live_resolution_apply(&window, &settings, &before,
                                                why, sizeof why));
  CHECK(step_count == 5 && steps[0] == STEP_D3D && steps[1] == STEP_TITLE &&
        steps[2] == STEP_WINDOW && steps[3] == STEP_SAVE &&
        steps[4] == STEP_TEXT);
  CHECK(title_width[0] == 1920 && title_height[0] == 1080);
  CHECK(settings.width == 1920 && settings.height == 1080);

  reset_calls();
  fail_d3d_call = 1;
  settings = changed(&before);
  CHECK(!x2::presentation::live_resolution_apply(&window, &settings, &before,
                                                 why, sizeof why));
  CHECK(step_count == 1 && steps[0] == STEP_D3D);
  CHECK(settings.width == 1280 && settings.height == 720);

  reset_calls();
  fail_title_call = 1;
  settings = changed(&before);
  CHECK(!x2::presentation::live_resolution_apply(&window, &settings, &before,
                                                 why, sizeof why));
  CHECK(step_count == 3 && steps[0] == STEP_D3D && steps[1] == STEP_TITLE &&
        steps[2] == STEP_D3D);
  CHECK(d3d_width[1] == 1280 && d3d_height[1] == 720);
  CHECK(settings.width == 1280 && settings.height == 720);

  reset_calls();
  fail_window_call = 1;
  settings = changed(&before);
  CHECK(!x2::presentation::live_resolution_apply(&window, &settings, &before,
                                                 why, sizeof why));
  CHECK(step_count == 6 && steps[0] == STEP_D3D && steps[1] == STEP_TITLE &&
        steps[2] == STEP_WINDOW && steps[3] == STEP_WINDOW &&
        steps[4] == STEP_TITLE && steps[5] == STEP_D3D);
  CHECK(window_width[1] == 1280 && window_height[1] == 720);
  CHECK(title_width[1] == 1280 && title_height[1] == 720);
  CHECK(d3d_width[1] == 1280 && d3d_height[1] == 720);
  CHECK(settings.width == 1280 && settings.height == 720);

  reset_calls();
  fail_save_call = 1;
  settings = changed(&before);
  CHECK(!x2::presentation::live_resolution_apply(&window, &settings, &before,
                                                 why, sizeof why));
  CHECK(step_count == 7 && steps[0] == STEP_D3D && steps[1] == STEP_TITLE &&
        steps[2] == STEP_WINDOW && steps[3] == STEP_SAVE &&
        steps[4] == STEP_WINDOW && steps[5] == STEP_TITLE &&
        steps[6] == STEP_D3D);
  CHECK(settings.width == 1280 && settings.height == 720);

  reset_calls();
  fail_window_call = 2;
  fail_save_call = 1;
  settings = changed(&before);
  CHECK(!x2::presentation::live_resolution_apply(&window, &settings, &before,
                                                 why, sizeof why));
  CHECK(d3d_calls == 2);
  CHECK(strstr(why, "rollback failed") != NULL);

  printf("test_live_resolution: %d checks passed\n", checks);
  return 0;
}
