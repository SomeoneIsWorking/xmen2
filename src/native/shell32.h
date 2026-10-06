/*
 * SHELL32: where the game's saves go. See shell32.c for why it is a virtual
 * drive and not a host path.
 */
#ifndef X2_SHELL32_H
#define X2_SHELL32_H

/*
 * The drive letter this host reserves for writable game state.
 *
 * The guest is handed "S:\" and win_path() maps that one letter to the save
 * directory instead of to the install. A letter rather than a host path
 * because win_path resolves everything the guest says against $GAME_PC_DIR --
 * a POSIX path given to the guest would come back as the install plus the
 * whole thing.
 *
 * S is not arbitrary: A/B are floppies, C is where the game believes it is
 * installed, and D is conventionally the CD this title shipped on. If a title
 * is ever found using S: for something of its own, this is the one place to
 * change.
 */
#define X2_SAVE_DRIVE 'S'

/* And the drive the game believes it is installed on -- the root of
   $GAME_PC_DIR, as far as the guest is concerned. GetModuleFileNameA hands out
   paths on it, and win_path() maps every drive that is not the save drive back
   to the install, so a path the host gives the guest survives being taken
   apart and handed back. */
#define X2_GAME_DRIVE 'C'

/* The host directory behind that drive, created on first use. Empty (never
   NULL) if it could not be created -- which is reported at the point of
   failure, not here. */
const char *x2_save_dir(void);

void shell32_install(void);
void shell32_report(void);

#endif /* X2_SHELL32_H */
