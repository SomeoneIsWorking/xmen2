#ifndef D3D8_TEXTURE_PROVENANCE_H
#define D3D8_TEXTURE_PROVENANCE_H

#include <stddef.h>
#include <stdint.h>

/* Runtime byte identity for one texture. This intentionally carries no asset
   name: dimensions, format, and a content hash do not establish provenance
   above the D3D8 resource boundary. */
typedef struct {
  int metadata_valid;
  uint32_t width, height, format, levels, faces;
  /* The complete texture install is a set of sub-resource unlocks, not just
     its base level. Keep the bounded 2D level coverage alongside the base
     fingerprint so a draw trace can distinguish a real mip chain from a
     texture whose stricter mobile sampler reaches never-uploaded mips. */
  uint64_t uploaded_level_mask;
  uint32_t upload_count;
  int level0_fingerprint_valid;
  uint64_t level0_fingerprint;
  uint64_t level0_revision;
} D3D8TextureProvenance;

void d3d8_texture_provenance_init(D3D8TextureProvenance *provenance,
                                  uint32_t width, uint32_t height,
                                  uint32_t format, uint32_t levels,
                                  uint32_t faces);

/* Commit the exact bytes accepted by the backend. Only a 2D texture's base
   level has one unambiguous fingerprint under this contract. */
void d3d8_texture_provenance_uploaded(D3D8TextureProvenance *provenance,
                                      uint32_t face, uint32_t level,
                                      const void *bytes, size_t byte_count);

#endif /* D3D8_TEXTURE_PROVENANCE_H */
