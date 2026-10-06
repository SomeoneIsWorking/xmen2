#include "control_screenshot.h"
#include "gpu_capture.h"

#include <stdio.h>
#include <string.h>

static int checks, failures;
static int request_ok = 1, capture_status, requests, discards;
static const unsigned char pixels[8] = {
    0x00, 0x00, 0xff, 0xff, 0x00, 0xff, 0x00, 0xff,
};

#define CHECK(condition)                                                       \
  do {                                                                         \
    checks++;                                                                  \
    if (!(condition)) {                                                        \
      fprintf(stderr, "%s:%d: CHECK failed: %s\n", __FILE__, __LINE__,         \
              #condition);                                                     \
      failures++;                                                              \
    }                                                                          \
  } while (0)

int gpu_capture_request(char *why, int whyn) {
  requests++;
  if (!request_ok && why && whyn > 0)
    snprintf(why, (size_t)whyn, "no GPU device yet");
  return request_ok;
}

int gpu_capture_result(const unsigned char **bgra, uint32_t *width,
                       uint32_t *height, char *why, int whyn) {
  if (capture_status < 0) {
    if (why && whyn > 0)
      snprintf(why, (size_t)whyn, "copy failed");
    return -1;
  }
  if (!capture_status)
    return 0;
  *bgra = pixels;
  *width = 2;
  *height = 1;
  return 1;
}

void gpu_capture_discard(void) { discards++; }

static void reset_mock(void) {
  request_ok = 1;
  capture_status = 0;
  requests = discards = 0;
}

int main(void) {
  X2ControlScreenshot shot = {0};
  const unsigned char *png;
  size_t bytes;
  char why[96];

  reset_mock();
  request_ok = 0;
  CHECK(x2_control_screenshot_poll(&shot, why, sizeof why) ==
        X2_CONTROL_SCREENSHOT_FAILED);
  CHECK(strstr(why, "no GPU") != NULL);
  CHECK(requests == 1 && !shot.armed);

  reset_mock();
  CHECK(x2_control_screenshot_poll(&shot, why, sizeof why) ==
        X2_CONTROL_SCREENSHOT_PENDING);
  CHECK(shot.armed && requests == 1);
  CHECK(x2_control_screenshot_poll(&shot, why, sizeof why) ==
        X2_CONTROL_SCREENSHOT_PENDING);
  CHECK(requests == 1 && discards == 0);
  capture_status = 1;
  CHECK(x2_control_screenshot_poll(&shot, why, sizeof why) ==
        X2_CONTROL_SCREENSHOT_READY);
  CHECK(!shot.armed && discards == 1);
  png = x2_control_screenshot_png(&shot, &bytes);
  CHECK(png != NULL && bytes > 24);
  CHECK(!memcmp(png, "\x89PNG\r\n\x1a\n", 8));
  CHECK(png[16] == 0 && png[17] == 0 && png[18] == 0 && png[19] == 2);
  CHECK(png[20] == 0 && png[21] == 0 && png[22] == 0 && png[23] == 1);

  reset_mock();
  CHECK(x2_control_screenshot_poll(&shot, why, sizeof why) ==
        X2_CONTROL_SCREENSHOT_PENDING);
  x2_control_screenshot_abandon(&shot);
  CHECK(!shot.armed && discards == 1);

  reset_mock();
  CHECK(x2_control_screenshot_poll(&shot, why, sizeof why) ==
        X2_CONTROL_SCREENSHOT_PENDING);
  capture_status = -1;
  CHECK(x2_control_screenshot_poll(&shot, why, sizeof why) ==
        X2_CONTROL_SCREENSHOT_FAILED);
  CHECK(strstr(why, "copy failed") != NULL && discards == 1);

  printf("control screenshot: %d checks, %d failure(s)\n", checks, failures);
  return failures ? 1 : 0;
}
