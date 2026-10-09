/* The guest's module inventory.
 *
 * The inventory comes from the same list the installer validates against --
 * the player's own images -- and everything else about a module is read out
 * of its PE headers at run time.
 */
#pragma once

namespace x2::native {

/* Register every module this port loads, in load order. Call once, before the
   mapping loop. Returns 0, or non-zero having said which name it choked on. */
int guest_modules_register(void);

} // namespace x2::native
