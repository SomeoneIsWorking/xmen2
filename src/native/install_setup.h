#pragma once

namespace x2::native {

/* First-run gate for main(): when this is the packaged product route and
 * GAME_PC_DIR is neither configured nor already in the environment, use the
 * remembered installation or ask for one (the setup screen on desktop, the
 * setup activity's source on Android) and publish it into GAME_PC_DIR.
 * Returns 0 to continue, 1 if the player selected nothing (caller exits 0),
 * 2 on a publish error (caller exits 1). Any other launch shape returns 0. */
int install_setup_resolve_env(int appimage_product, int have_install_dir);

} // namespace x2::native
