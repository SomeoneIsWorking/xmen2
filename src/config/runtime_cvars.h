#ifndef X2_RUNTIME_CVARS_H
#define X2_RUNTIME_CVARS_H

#ifdef __cplusplus
extern "C" {
#endif

/* Registers the port's runtime CVars, loads <configdir>/x2native-runtime.conf,
 * and applies every `--set NAME=VALUE` token found in argv. Call once from
 * main() after option parsing and the .env load, before any guest code or
 * engine setup.
 *
 * Runtime/engine knobs are not player settings: this file is separate from
 * x2native.conf (owned by settings_store.c).
 *
 * CVars, with their layers (low to high: compiled default < that file <
 * environment X2_* < --set):
 *   engine      string  ""       -- ""(build default) | substrate | interpreter | jit
 *   jit.cache   bool    true     -- off re-translates every block
 *   jit.verify  bool    false    -- per-block shadow-interpreter compare
 *
 * C code reads an effective value through <lucent/cvar_c.h>
 * (lucent_cvar_text("engine"), lucent_cvar_flag("jit.cache", 1), ...). */
void x2_runtime_config_init(int argc, char **argv);

#ifdef __cplusplus
}
#endif

#endif /* X2_RUNTIME_CVARS_H */
