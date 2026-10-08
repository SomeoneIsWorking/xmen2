#pragma once

#include <cstddef>

namespace x2::native {

/* libMovie configures the igImage with display dimensions, while format 0x65
   allocates power-of-two storage. Return its byte pitch only when the guest
   allocation can hold that evidenced layout. */
int movie_image_pitch(int width, int height, size_t allocation_bytes,
                      size_t *pitch);

} // namespace x2::native
