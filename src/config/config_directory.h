#pragma once

namespace x2::config {

/* Return the per-user configuration directory for this port. The path is
 * thread-local storage owned by Lucent and remains valid until this thread's
 * next call to the same Lucent path function. */
const char *config_directory(void);

/* Create the directory and any missing parents. */
int config_directory_ensure(void);

} // namespace x2::config
