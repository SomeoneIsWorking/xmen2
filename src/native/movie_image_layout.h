#ifndef X2_MOVIE_IMAGE_LAYOUT_H
#define X2_MOVIE_IMAGE_LAYOUT_H

#include <stddef.h>

/* libMovie configures the igImage with display dimensions, while format 0x65
   allocates power-of-two storage. Return its byte pitch only when the guest
   allocation can hold that evidenced layout. */
int x2_movie_image_pitch(int width, int height, size_t allocation_bytes,
                         size_t *pitch);

#endif
