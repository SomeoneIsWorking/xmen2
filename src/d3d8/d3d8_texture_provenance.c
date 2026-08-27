#include "d3d8_texture_provenance.h"

#include <string.h>

static uint64_t fnv1a64(const void *bytes, size_t count)
{
    const uint8_t *p = bytes;
    uint64_t value = UINT64_C(14695981039346656037);
    size_t i;
    for (i = 0; i < count; ++i) {
        value ^= p[i];
        value *= UINT64_C(1099511628211);
    }
    return value;
}

void d3d8_texture_provenance_init(D3D8TextureProvenance *provenance,
                                  uint32_t width, uint32_t height,
                                  uint32_t format, uint32_t levels,
                                  uint32_t faces)
{
    if (!provenance) return;
    memset(provenance, 0, sizeof *provenance);
    provenance->metadata_valid = 1;
    provenance->width = width;
    provenance->height = height;
    provenance->format = format;
    provenance->levels = levels;
    provenance->faces = faces;
}

void d3d8_texture_provenance_uploaded(D3D8TextureProvenance *provenance,
                                      uint32_t face, uint32_t level,
                                      const void *bytes, size_t byte_count)
{
    if (!provenance || !bytes || provenance->faces != 1
            || face != 0 || level != 0)
        return;
    provenance->level0_fingerprint = fnv1a64(bytes, byte_count);
    provenance->level0_fingerprint_valid = 1;
    provenance->level0_revision++;
}
