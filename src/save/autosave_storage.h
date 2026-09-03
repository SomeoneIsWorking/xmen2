#ifndef X2_AUTOSAVE_STORAGE_H
#define X2_AUTOSAVE_STORAGE_H

#include <stddef.h>
#include <stdint.h>

#define X2_AUTOSAVE_LEAF "autosave.save"
#define X2_SAVE_HEADER_BYTES 128u

typedef enum {
  X2_AUTOSAVE_FAULT_NONE = 0,
  X2_AUTOSAVE_FAULT_AFTER_HEADER,
  X2_AUTOSAVE_FAULT_AFTER_LENGTH,
  X2_AUTOSAVE_FAULT_AFTER_PAYLOAD,
  X2_AUTOSAVE_FAULT_AFTER_FILE_SYNC,
  X2_AUTOSAVE_FAULT_BEFORE_RENAME
} X2AutosaveStorageFault;

/* Publish one retail-compatible save image transactionally. The temporary
   file is created in directory, completely written and fsynced, then renamed
   over autosave.save and the directory is fsynced. Every failure before the
   rename leaves the previous autosave untouched. `fault` is a deterministic
   test seam; production passes X2_AUTOSAVE_FAULT_NONE. */
int x2_autosave_storage_publish(const char *directory, const void *header,
                                const void *payload, size_t payload_size,
                                X2AutosaveStorageFault fault);

#endif
