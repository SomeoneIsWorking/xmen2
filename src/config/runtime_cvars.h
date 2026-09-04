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
 * Layers, from low to high, are compiled default, that file, enumerated X2_*
 * environment overrides, then --set. Registered values cover JIT diagnostics,
 * native-override A/B checks, and runtime/player knobs that previously read
 * process environment directly. C owners read through <lucent/cvar_c.h>.
 *
 * The gameplay product has one execution engine: x86port's runtime JIT.
 * Engine selection and explicit interpreter controls are intentionally absent
 * from this configuration surface. */
void x2_runtime_config_init(int argc, char **argv);

#ifdef __cplusplus
}
#endif

#endif /* X2_RUNTIME_CVARS_H */
