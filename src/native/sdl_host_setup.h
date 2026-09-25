/*
 * sdl_host_setup.h -- SDL's process-wide configuration, before SDL starts.
 *
 * TOUCH. SDL can synthesize mouse events for every finger contact.
 * TouchControls owns those contacts (including the one deliberate
 * portrait-pointer path), so forwarding SDL's duplicate mouse stream makes an
 * action-pad tap also reach the retail world-click handler. The translation is
 * disabled before SDL creates its event sources; real mouse events are
 * unchanged.
 *
 * LOG. SDL's own messages -- its GPU backends' validation errors among them --
 * go through the port's logger, channel "sdl", like every other line. SDL's
 * default writes straight to stderr: in the browser that is a synchronous
 * crossing per line from the game's thread and never reaches the batched page
 * console (src/web/browser_log.cpp), so a WebGPU error raised every frame cost
 * the guest worker about 1.7% while no captured log showed a word of it.
 *
 * BROWSER EVENT SOURCES. SDL's video, joystick and sensor backends register
 * DOM listeners that dispatch to the thread which started the subsystem.
 * Every guest thread is a pthread of its own, so a lazy start from, say,
 * the thread that first enumerates pads bound every later keydown, resize or
 * gamepadconnected to that thread's mailbox, and the first event after it
 * exited aborted the page ("emscripten_proxy_async failed", #148). In the
 * browser they start here instead, on the thread that runs x2native_main --
 * it outlives every guest thread, and the runtime removes the listeners when
 * it exits -- and stay started for the run; later starts only count a
 * reference.
 *
 * VIDEO, for a windowed run, starts here too and stays started: the display's
 * shape decides the width of the configured resolution, and that is published
 * to the game (display_mode_seed.c) before any window exists.
 */
#ifndef X2_SDL_HOST_SETUP_H
#define X2_SDL_HOST_SETUP_H

/* Returns 1, or 0 having logged why SDL refused. A build without SDL has
   nothing to configure. */
int sdl_host_setup(int window);

#endif /* X2_SDL_HOST_SETUP_H */
