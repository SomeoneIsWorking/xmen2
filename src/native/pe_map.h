/* Map a PE32 image at its own preferred base. See pe_map.c for why it refuses
   to relocate rather than falling back to another address. */
#ifndef PE_MAP_H
#define PE_MAP_H

#include <stdint.h>

typedef struct PeImage {
    uint32_t base;       /* where it actually landed */
    uint32_t preferred;  /* what it was linked for; differs when relocated */
    uint32_t size;       /* SizeOfImage */
    int      nsections;
} PeImage;

int  pe_map(const char *path, PeImage *out);
/* As pe_map, naming the base the caller hoped for (reporting only). */
int  pe_map_at(const char *path, uint32_t want, PeImage *out);
/* A fixed low anonymous mapping (guest stack, scratch objects). Same refusal
   rule: it lands where asked or it fails. */
int  pe_map_anon_low(uint32_t want, uint32_t size);
void pe_unmap(PeImage *img);

/* Export RVA for a name in a MAPPED image, or 0. */
uint32_t pe_export_rva(uint32_t base, const char *name);

/* The reverse: the greatest exported RVA at or below `rva`, and its name, or 0
   when the image exports nothing at or below it. An APPROXIMATION of "which
   function is this" -- the export table carries entry points and not sizes, so
   an address inside a function that is not itself exported is attributed to
   the nearest exported one below it. The answer is exact only when the return
   value equals `rva`; callers that need certainty must compare. */
uint32_t pe_export_containing(uint32_t base, uint32_t rva,
                              const char **name_out);

/* Bind every import slot of a mapped image. `resolve` returns the address to
   write, or 0 when it cannot -- see pe_map.c for why 0 must not stay 0. */
int pe_bind_imports(uint32_t base,
                    uint32_t (*resolve)(const char *mod, const char *sym,
                                        int by_ordinal, uint32_t ordinal,
                                        void *ctx),
                    void *ctx, int *out_bound, int *out_poisoned);

/* AddressOfEntryPoint of a mapped image (DllMainCRTStartup for these DLLs). */
uint32_t pe_entry_rva(uint32_t base);

/* Does the image import anything from `modname`? Orders module init. */
int pe_imports_module(uint32_t base, const char *modname);

/* IMAGE_FILE_DLL: an EXE's entry point is the program, not DllMain. */
int pe_is_dll(uint32_t base);

#endif /* PE_MAP_H */
