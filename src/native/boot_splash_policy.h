#pragma once

#include <cstdint>

namespace x2::native {

/* Presentation policy for the boot's own branding, composed by the console
 * command override in startup.cpp:
 *
 *   - an env-gated trace of the boot's console commands, so a boot question
 *     ("who loads the legal map?") is answered from a run instead of guessed;
 *
 *   - the refusal of the boot's own "openmenu loading" after a boot-mode
 *     dispatch, so the splash spinner never opens, bounded so a later
 *     gameplay load keeps its screen: consumed by the first match, or the
 *     window expires after 16 further commands (the boot issues five), the
 *     expiry reported rather than silent. */

/* Trace one console command (no-ops unless X2_BOOT_CMD_TRACE=1). */
void boot_splash_trace(std::uint32_t command);

/* Arm the refusal window; call right after a boot-mode dispatch took over. */
void boot_splash_arm();

/* 1 when this command is the refused boot loading menu -- the caller then
 * completes the command's retail contract (EAX=1, RET 0x4) without running
 * it. Handles the window's expiry internally and reports it. */
int boot_splash_refuse(std::uint32_t command);

} // namespace x2::native
