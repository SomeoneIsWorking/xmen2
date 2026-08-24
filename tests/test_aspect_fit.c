#include "aspect_fit.h"

#include <assert.h>
#include <stdint.h>
#include <stdio.h>

static int checks;

#define CHECK(c) do { assert(c); checks++; } while (0)

static X2AspectRect fit(uint32_t ow, uint32_t oh, uint32_t iw, uint32_t ih)
{
    X2AspectRect rect;
    CHECK(x2_aspect_fit(ow, oh, iw, ih, &rect));
    return rect;
}

int main(void)
{
    X2AspectRect rect;

    rect = fit(1280, 720, 800, 600);
    CHECK(rect.x == 160 && rect.y == 0);
    CHECK(rect.width == 960 && rect.height == 720);

    rect = fit(800, 800, 800, 600);
    CHECK(rect.x == 0 && rect.y == 100);
    CHECK(rect.width == 800 && rect.height == 600);

    rect = fit(1920, 1080, 1280, 720);
    CHECK(rect.x == 0 && rect.y == 0);
    CHECK(rect.width == 1920 && rect.height == 1080);

    /* RmlUi consumes this same fit as its 1280x720 design-space dp ratio:
       1080/720 = 1.5 and 2160/720 = 3. Resolution changes must not make text
       smaller relative to the output. */
    rect = fit(3840, 2160, 1280, 720);
    CHECK(rect.x == 0 && rect.y == 0);
    CHECK(rect.width == 3840 && rect.height == 2160);

    rect = fit(1001, 701, 4, 3);
    CHECK(rect.x == 33 && rect.y == 0);
    CHECK(rect.width == 934 && rect.height == 701);
    CHECK(rect.x * 2u + rect.width <= 1001);
    CHECK(rect.y * 2u + rect.height <= 701);

    CHECK(!x2_aspect_fit(0, 720, 800, 600, &rect));
    CHECK(!x2_aspect_fit(1280, 0, 800, 600, &rect));
    CHECK(!x2_aspect_fit(1280, 720, 0, 600, &rect));
    CHECK(!x2_aspect_fit(1280, 720, 800, 0, &rect));
    CHECK(!x2_aspect_fit(1280, 720, 800, 600, NULL));
    CHECK(!x2_aspect_fit(1, 1, UINT32_MAX, 1, &rect));

    printf("test_aspect_fit: %d checks passed\n", checks);
    return 0;
}
