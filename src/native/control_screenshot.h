/* Control-channel screenshot state, separate from HTTP and GPU ownership. */
#ifndef X2_CONTROL_SCREENSHOT_H
#define X2_CONTROL_SCREENSHOT_H

#include <stddef.h>

typedef struct X2ControlScreenshot {
    int armed;
    unsigned char *png;
    size_t png_bytes;
    unsigned width;
    unsigned height;
} X2ControlScreenshot;

enum {
    X2_CONTROL_SCREENSHOT_FAILED = -1,
    X2_CONTROL_SCREENSHOT_PENDING = 0,
    X2_CONTROL_SCREENSHOT_READY = 1,
};

/* Arm once, then poll from subsequent render-frame completion points. */
int x2_control_screenshot_poll(X2ControlScreenshot *shot,
                               char *why, int whyn);

/* Cancel a timed-out HTTP request on the render thread. */
void x2_control_screenshot_abandon(X2ControlScreenshot *shot);

const unsigned char *x2_control_screenshot_png(
    const X2ControlScreenshot *shot, size_t *bytes);

#endif
