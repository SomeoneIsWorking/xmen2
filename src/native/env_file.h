#ifndef X2_ENV_FILE_H
#define X2_ENV_FILE_H

/* Load the nearest project .env without replacing variables the launcher
 * explicitly supplied.  Returns 1 when a file was loaded, 0 when none was
 * found, and -1 for a malformed or unreadable file. */
int x2_load_project_env(const char *argv0);

#endif
