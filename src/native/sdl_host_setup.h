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
 */
#ifndef X2_SDL_HOST_SETUP_H
#define X2_SDL_HOST_SETUP_H

/* Returns 1, or 0 having logged why SDL refused. A build without SDL has
   nothing to configure. */
int sdl_host_setup(void);

#endif /* X2_SDL_HOST_SETUP_H */
