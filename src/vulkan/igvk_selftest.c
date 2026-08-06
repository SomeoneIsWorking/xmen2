/*
 * A self-test for the host side of the renderer.
 *
 * ## Why this exists
 *
 * igvk_device.c implements a frame -- acquire a swapchain image, open a
 * render pass clearing to a colour, submit -- and at the time it was written
 * the game had never reached a frame, so not one line of it had ever run. The
 * engine stops earlier, in the exe. Untested code that looks finished is the
 * thing this project's rules exist to prevent, and "it will be exercised when
 * the game gets there" is exactly the promise that never gets kept.
 *
 * igvk_device.c takes no guest state on purpose -- that is what makes this
 * possible. The frame path can be driven from host code with no engine, no
 * ARK, and no recompiled bodies involved at all.
 *
 * ## What this DOES prove
 *
 * That a GPU device can be created, a swapchain claimed on a real window, and
 * frames acquired, cleared and presented, on this machine.
 *
 * ## What it does NOT prove, and must not be read as proving
 *
 * That the ENGINE's frame maps onto this correctly. The engine drives these
 * calls in its own order through slots 174/177/186/175, and whether that
 * order produces the right picture is a question only a real run answers.
 * This is a check on the host half, and the report says so in those words.
 */
#include "igvk_device.h"

#include <stdio.h>

#ifdef X2_WITH_SDL
#include <SDL3/SDL.h>
#endif

#define FRAMES 3

int igvk_device_selftest(void)
{
#ifndef X2_WITH_SDL
    printf("igVk selftest: SKIPPED -- built without SDL, so there is no "
           "device to test. This is not a pass.\n");
    return 77;
#else
    SDL_Window *w = NULL;
    int i, presented_ok = 0, began = 0;

    printf("\n=== igVk selftest: the host frame path, with no engine "
           "involved ===\n");

    if (!igvk_device_create()) {
        printf("igVk selftest: FAILED -- no GPU device. Nothing below could "
               "have run.\n");
        return 1;
    }

    /*
     * Our own window. The guest has not made one at this point, and borrowing
     * a window this test did not create would leave it claimed afterwards.
     *
     * SHOWN, not hidden, and that is not cosmetic: the first version passed
     * SDL_WINDOW_HIDDEN so as not to disturb a headless harness, and every
     * frame then came back with no swapchain texture -- 3 of 3 skipped, 0
     * presented. A hidden window has nothing to present to, so SDL has no
     * image to hand out. The test caught it on its first run, which is the
     * argument for the test existing. It is on screen for well under a
     * second.
     */
    w = SDL_CreateWindow("igvk selftest", 320, 240, 0);
    if (!w) {
        /*
         * SKIP, not FAIL: no display is a property of where this ran, not of
         * the renderer. Distinct from the no-GPU case above, which stays a
         * FAILURE -- a machine with no Vulkan cannot run this port at all,
         * and skipping would hide exactly the thing worth knowing.
         */
        printf("igVk selftest: SKIPPED -- no window could be created (%s), so "
               "there is no surface to present to. The GPU device WAS created, "
               "so this is the environment, not the renderer. Nothing below "
               "was checked.\n", SDL_GetError());
        igvk_device_destroy();
        return 77;
    }
    if (!igvk_device_attach_window(w)) {
        printf("igVk selftest: FAILED -- the swapchain could not be claimed "
               "on a window that was created successfully.\n");
        SDL_DestroyWindow(w);
        igvk_device_destroy();
        return 1;
    }

    for (i = 0; i < FRAMES; i++) {
        if (!igvk_frame_begin()) continue;      /* counted by the device */
        began++;
        /* A different colour each frame, so a frame that is silently reused
           rather than re-cleared would be visible to a capture. */
        igvk_frame_clear(1u, i == 0 ? 1.0f : 0.0f, i == 1 ? 1.0f : 0.0f,
                         i == 2 ? 1.0f : 0.0f, 1.0f, 1.0f, 0u);
        igvk_frame_viewport(0, 0, 320, 240, 0.0f, 1.0f);
        if (!igvk_frame_in_progress()) {
            printf("igVk selftest: FAILED -- frame %d reported no frame in "
                   "progress between begin and end.\n", i);
            break;
        }
        igvk_frame_end();
        if (igvk_frame_in_progress()) {
            printf("igVk selftest: FAILED -- frame %d was still open after "
                   "igvk_frame_end.\n", i);
            break;
        }
        presented_ok++;
    }

    igvk_device_report();
    igvk_device_attach_window(NULL);
    SDL_DestroyWindow(w);
    igvk_device_destroy();

    if (presented_ok != FRAMES) {
        printf("igVk selftest: FAILED -- %d of %d frames completed (%d were "
               "begun). A frame path that cannot present in isolation will "
               "not present under the engine either.\n",
               presented_ok, FRAMES, began);
        return 1;
    }
    printf("igVk selftest: PASSED -- %d frames acquired, cleared and "
           "presented.\n"
           "  This proves the HOST half only. Whether the engine's own order "
           "of\n"
           "  beginDraw / clearRenderDestination / setViewport / endDraw "
           "produces the\n"
           "  right picture is not tested here and cannot be until a real run "
           "reaches a frame.\n", presented_ok);
    return 0;
#endif
}
