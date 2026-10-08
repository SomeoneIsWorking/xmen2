#pragma once

#include <cstddef>

namespace x2::save {

inline constexpr char kRetailSaveSubdirectory[] =
    "Activision/X-Men Legends 2/Save";

/* Resolve the title's retail save-leaf directory under the host writable
   storage root. The root itself also owns host config and registry data, so
   callers that enumerate or publish game saves must not use it directly. */
int retail_save_directory_from_root(const char *storage_root, char *out,
                                    size_t capacity);

/* Process-lifetime host path backed by x2::native::save_dir(). NULL means the
   storage root is unavailable or the complete retail path does not fit. */
const char *retail_save_directory(void);

} // namespace x2::save
