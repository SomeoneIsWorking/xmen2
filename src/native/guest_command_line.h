#pragma once

namespace x2::native {

/*
 * The command line the GUEST process sees.
 *
 * Not this process's. The port's options -- --appimage <dir>, --set k=v, the
 * diagnostic flags -- are the port's business, and the guest is a different
 * program that never saw them on its own platform.
 *
 * The returned pointer is stable for the life of the process.
 */
const char *guest_command_line(void);

} // namespace x2::native
