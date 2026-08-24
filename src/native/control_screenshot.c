#include "control_screenshot.h"

#include "control_png.h"
#include "gpu_capture.h"

#include <stdio.h>
#include <stdlib.h>

int x2_control_screenshot_poll(X2ControlScreenshot *shot,
                               char *why, int whyn)
{
    const unsigned char *bgra = NULL;
    uint32_t width = 0, height = 0;
    int result;

    if (!shot) {
        if (why && whyn > 0)
            snprintf(why, (size_t)whyn,
                     "the screenshot state owner is missing");
        return X2_CONTROL_SCREENSHOT_FAILED;
    }
    if (!shot->armed) {
        if (!gpu_capture_request(why, whyn))
            return X2_CONTROL_SCREENSHOT_FAILED;
        shot->armed = 1;
        return X2_CONTROL_SCREENSHOT_PENDING;
    }
    result = gpu_capture_result(&bgra, &width, &height, why, whyn);
    if (result == 0) return X2_CONTROL_SCREENSHOT_PENDING;
    shot->armed = 0;
    if (result < 0) {
        gpu_capture_discard();
        return X2_CONTROL_SCREENSHOT_FAILED;
    }

    free(shot->png);
    shot->png = control_png_from_bgra(bgra, width, height, &shot->png_bytes);
    gpu_capture_discard();
    if (!shot->png) {
        shot->png_bytes = 0;
        if (why && whyn > 0)
            snprintf(why, (size_t)whyn,
                     "PNG encode of the bounded %ux%u capture failed",
                     width, height);
        return X2_CONTROL_SCREENSHOT_FAILED;
    }
    shot->width = width;
    shot->height = height;
    return X2_CONTROL_SCREENSHOT_READY;
}

void x2_control_screenshot_abandon(X2ControlScreenshot *shot)
{
    if (!shot || !shot->armed) return;
    gpu_capture_discard();
    shot->armed = 0;
}

const unsigned char *x2_control_screenshot_png(
    const X2ControlScreenshot *shot, size_t *bytes)
{
    if (bytes) *bytes = shot ? shot->png_bytes : 0;
    return shot ? shot->png : NULL;
}
