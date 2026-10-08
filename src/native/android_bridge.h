#pragma once

namespace x2::native {

/* Android's Activity supplies these absolute app-private paths before SDL
 * starts. Desktop builds return NULL and keep their normal picker contract. */
const char *android_install_source();

/* Route stdout/stderr to logcat. Android discards a process's stdio, so every
 * refusal the port prints on its way to exit() is otherwise invisible and a
 * deliberate exit is indistinguishable from a crash. No-op off Android. */
void android_log_stdio();

} // namespace x2::native
