#ifndef GUEST_MEMORY_H
#define GUEST_MEMORY_H

#include <stddef.h>
#include <stdint.h>

/* Translate the 32-bit address visible to the guest program into a host
   pointer.  On hosts that can map the low 4 GB this base remains zero. */
extern uintptr_t g_guest_memory_base;

#if defined(__EMSCRIPTEN__) || defined(X2_GUEST_MEMORY_SPARSE)
#ifndef X2_GUEST_MEMORY_SPARSE
#define X2_GUEST_MEMORY_SPARSE 1
#endif
struct X86pMem;
const struct X86pMem *guest_memory_model(void);
void *guest_memory_pointer(uint32_t address);
const void *guest_memory_const_pointer(uint32_t address);
uint32_t guest_memory_address(const void *pointer);
int guest_memory_try_read(uint32_t address, void *destination, size_t size);
void guest_memory_read(uint32_t address, void *destination, size_t size);
void guest_memory_write(uint32_t address, const void *source, size_t size);
#else
static inline void *guest_memory_pointer(uint32_t address) {
  return address ? (void *)(g_guest_memory_base + (uintptr_t)address) : NULL;
}

static inline const void *guest_memory_const_pointer(uint32_t address) {
  return address ? (const void *)(g_guest_memory_base + (uintptr_t)address)
                 : NULL;
}

static inline uint32_t guest_memory_address(const void *pointer) {
  return (uint32_t)((uintptr_t)pointer - g_guest_memory_base);
}

#endif

int guest_memory_host_address(const void *pointer, uint32_t *address);

int guest_memory_init(void);
int guest_memory_map_fixed(uint32_t address, size_t size, int protection);
int guest_memory_map_any(uint32_t first, uint32_t last, size_t alignment,
                         size_t size, int protection, uint32_t *address);
int guest_memory_protect(uint32_t address, size_t size, int protection);
int guest_memory_release(uint32_t address, size_t size);
int guest_memory_is_readable(uint32_t address, size_t size);

#endif
