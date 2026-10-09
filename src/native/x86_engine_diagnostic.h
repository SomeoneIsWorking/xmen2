#pragma once

namespace x2::native {

/*
 * Route x86port's own diagnostics into this port's logger. Call once during
 * engine setup, before any execution thread exists; see the source for why the
 * library's default sink is not readable here.
 */
void x86_engine_diagnostic_install(void);

} // namespace x2::native
