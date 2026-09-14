/*
 * What the game is told its command line was.
 *
 * This used to be the HOST process's command line, read from
 * /proc/self/cmdline, on the reasoning that a fabricated line would make the
 * guest disagree with the process it is running in. That reasoning confuses two
 * different programs. The guest is XMen2.exe; this process is x2native, and its
 * arguments are the port's options -- --appimage <dir>, --set k=v, the
 * diagnostic flags -- which a 2005 PC title is never handed when a player
 * launches it, and which it would have to parse if it were.
 *
 * So the guest gets the game's program name and no port options. If the port
 * ever needs to hand the game real arguments, that is an explicit, documented
 * option here rather than everything the host happens to have been called
 * with.
 *
 * Correction (issue #151): the first version of this file attributed the
 * browser's args-run splash plateau to these leaked tokens. That was wrong --
 * the plateau was the web entry point's `argc == 2` request test dropping
 * `--test-deadzone` when diagnostics were appended (fixed in
 * src/web/web_main.cpp). The leak was still a boundary defect on the desktop
 * host, where /proc/self/cmdline really does return the port's own line.
 */
#include "guest_command_line.h"

#include "install_requirements.h"

#include <stdio.h>

const char *guest_command_line(void) {
  static char line[128];
  static int built;

  if (!built) {
    /* Refuse to hand over an empty line: a guest that reads it would parse a
       program name of "" and a CRT that derives argv[0] from it would build an
       argument vector starting with nothing. The name comes from the generated
       image list, which is also what the loader maps. */
    if (X2_INSTALL_MAIN_IMAGE[0] == '\0') {
      snprintf(line, sizeof line, "\"%s\"", "XMen2.exe");
    } else {
      snprintf(line, sizeof line, "\"%s\"", X2_INSTALL_MAIN_IMAGE);
    }
    built = 1;
  }
  return line;
}
