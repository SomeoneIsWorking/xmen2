/* The guest's module inventory.
 *
 * The port used to learn which modules exist from generated C: each translated
 * module carried a constructor that registered its own name, base and function
 * table. With the translator gone there is no generated code to ask, so the
 * inventory comes from the same list the installer validates against -- the
 * player's own images -- and everything else about a module is read out of its
 * PE headers at run time.
 */
#ifndef GUEST_MODULES_H
#define GUEST_MODULES_H

/* Register every module this port loads, in load order. Call once, before the
   mapping loop. Returns 0, or non-zero having said which name it choked on. */
int guest_modules_register(void);

#endif /* GUEST_MODULES_H */
