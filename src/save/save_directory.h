#ifndef X2_SAVE_DIRECTORY_H
#define X2_SAVE_DIRECTORY_H

#include <stddef.h>

#define X2_RETAIL_SAVE_SUBDIRECTORY "Activision/X-Men Legends 2/Save"

/* Resolve the title's retail save-leaf directory under the host writable
   storage root. The root itself also owns host config and registry data, so
   callers that enumerate or publish game saves must not use it directly. */
int x2_retail_save_directory_from_root(const char *storage_root,
                                       char *out, size_t capacity);

/* Process-lifetime host path backed by x2_save_dir(). NULL means the storage
   root is unavailable or the complete retail path does not fit. */
const char *x2_retail_save_directory(void);

#endif
