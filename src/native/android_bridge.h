#ifndef X2_ANDROID_BRIDGE_H
#define X2_ANDROID_BRIDGE_H

#ifdef __cplusplus
extern "C" {
#endif

/* Android's Activity supplies these absolute app-private paths before SDL
 * starts. Desktop builds return NULL and keep their normal picker contract. */
const char *x2_android_install_source(void);

/* Route stdout/stderr to logcat. Android discards a process's stdio, so every
 * refusal the port prints on its way to exit() is otherwise invisible and a
 * deliberate exit is indistinguishable from a crash. No-op off Android. */
void x2_android_log_stdio(void);

#ifdef __cplusplus
}
#endif

#endif /* X2_ANDROID_BRIDGE_H */
