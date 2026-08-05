/* Map a PE32 image at its own preferred base. See pe_map.c for why it refuses
   to relocate rather than falling back to another address. */
#ifndef PE_MAP_H
#define PE_MAP_H

#include <stdint.h>

typedef struct PeImage {
    uint32_t base;       /* preferred base, and where it actually landed */
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

/* Bind every import slot of a mapped image. `resolve` returns the address to
   write, or 0 when it cannot -- see pe_map.c for why 0 must not stay 0. */
int pe_bind_imports(uint32_t base,
                    uint32_t (*resolve)(const char *mod, const char *sym,
                                        int by_ordinal, uint32_t ordinal,
                                        void *ctx),
                    void *ctx, int *out_bound, int *out_poisoned);

#endif /* PE_MAP_H */
