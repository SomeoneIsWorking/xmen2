#ifndef X2_DIRECTORY_CREATE_H
#define X2_DIRECTORY_CREATE_H

/* Create a directory and any missing parents, as `mkdir -p` does. Returns
 * non-zero on success, including when the directory already exists.
 *
 * This deliberately owns no platform-path policy and pulls in no dependency:
 * run-artifact writers need it as much as the user-data resolver does, and a
 * second copy in each of them is how the two drift. */
int x2_directory_create(const char *path);

#endif /* X2_DIRECTORY_CREATE_H */
