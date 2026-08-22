#include "movie_image_layout.h"

#include <stdint.h>

static int power_of_two_at_least(int value, size_t *result)
{
    size_t power = 1;
    if (value <= 0) return 0;
    while (power < (size_t)value) {
        if (power > SIZE_MAX / 2u) return 0;
        power *= 2u;
    }
    *result = power;
    return 1;
}

int x2_movie_image_pitch(int width, int height, size_t allocation_bytes,
                         size_t *pitch)
{
    size_t storage_width, storage_height, required;
    if (!pitch || !power_of_two_at_least(width, &storage_width)
            || !power_of_two_at_least(height, &storage_height)
            || storage_width > SIZE_MAX / 4u
            || storage_height > SIZE_MAX / (storage_width * 4u)) return 0;
    required = storage_width * storage_height * 4u;
    if (allocation_bytes < required) return 0;
    *pitch = storage_width * 4u;
    return 1;
}
