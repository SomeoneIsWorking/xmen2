/* Internal to the ADVAPI32 subsystem: the flat store's shape, shared by
 * advapi32.c (guest imports + store), advapi32_enum.c (the RegEnum* imports)
 * and advapi32_host_store.c (the one boot-time host publication). Nothing
 * outside ADVAPI32 may include this.
 */
#ifndef X2_ADVAPI32_INTERNAL_H
#define X2_ADVAPI32_INTERNAL_H

#include <stdint.h>

#define MAX_VALUES 256
#define MAX_PATH_ 256
#define MAX_NAME_ 96
#define MAX_DATA_ 512

typedef struct {
  int used;
  char path[MAX_PATH_]; /* HIVE\key\subkey, no trailing sep */
  char name[MAX_NAME_]; /* "" is the key's default value */
  uint32_t type;
  uint32_t len;
  unsigned char data[MAX_DATA_];
} RegValue;

/* Keys that exist but hold no value still have to be findable, or a
   RegCreateKey followed by RegOpenKey reports the key missing. They are
   recorded as a value row with a name of "\x01" that nothing enumerates. */
#define KEY_MARK "\001"

/* The store, owned by advapi32.c. Lazy-loading on first touch is part of the
   contract -- every entry point here may run before any other. */
void advapi32_store_load(void);
void advapi32_store_save(void);
RegValue *advapi32_store_find(const char *path, const char *name);
RegValue *advapi32_store_put(const char *path, const char *name);

/* A write made through the store by the HOST, not by a guest import: counts
   in the exit report and marks the store dirty exactly as a guest write
   would, so a published value survives like one the game saved itself. */
void advapi32_store_note_write(void);

#endif /* X2_ADVAPI32_INTERNAL_H */
