#ifndef X2_X86_ENGINE_DIAGNOSTIC_H
#define X2_X86_ENGINE_DIAGNOSTIC_H

#ifdef __cplusplus
extern "C" {
#endif

/*
 * Route x86port's own diagnostics into this port's logger. Call once during
 * engine setup, before any execution thread exists; see the source for why the
 * library's default sink is not readable here.
 */
void x86_engine_diagnostic_install(void);

#ifdef __cplusplus
}
#endif

#endif
