#ifndef X86WATCH_MEMORY_H
#define X86WATCH_MEMORY_H

#include <stddef.h>
#include <stdint.h>

#define X86_MEMWATCH_MAX_OFFSETS 8

typedef uint32_t (*x86_memwatch_read32_fn)(void *context, uint32_t address);

typedef struct X86MemWatch {
    uint32_t root;
    uint32_t offsets[X86_MEMWATCH_MAX_OFFSETS];
    unsigned offset_count;
} X86MemWatch;

int x86_memwatch_parse(X86MemWatch *watch, const char *spec,
                       char *error, size_t error_size);
int x86_memwatch_read(const X86MemWatch *watch, uint32_t mapped_root,
                      x86_memwatch_read32_fn read32, void *context,
                      uint32_t *resolved_address, uint32_t *value);

#endif
