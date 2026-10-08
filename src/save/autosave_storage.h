#pragma once

#include <cstddef>
#include <cstdint>

namespace x2::save {

inline constexpr char kAutosaveLeaf[] = "autosave.save";
inline constexpr unsigned kSaveHeaderBytes = 128u;

enum class AutosaveStorageFault : int {
  None = 0,
  AfterHeader,
  AfterLength,
  AfterPayload,
  AfterFileSync,
  BeforeRename
};

/* Publish one retail-compatible save image transactionally. The temporary
   file is created in directory, completely written and fsynced, then renamed
   over autosave.save and the directory is fsynced (on Windows a write-through
   MoveFileEx replaces it instead). Every failure before the
   rename leaves the previous autosave untouched. `fault` is a deterministic
   test seam; production passes AutosaveStorageFault::None. */
int autosave_storage_publish(const char *directory, const void *header,
                             const void *payload, size_t payload_size,
                             AutosaveStorageFault fault);

} // namespace x2::save
