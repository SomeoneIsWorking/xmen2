#include "movie_image_layout.h"

#include <stdio.h>

int main(void)
{
    size_t pitch = 0;
    int failures = 0;
    failures += !x2_movie_image_pitch(640, 480, 2097152, &pitch);
    failures += pitch != 4096;
    failures += x2_movie_image_pitch(640, 480, 2097151, &pitch);
    failures += !x2_movie_image_pitch(256, 256, 262144, &pitch);
    failures += pitch != 1024;
    failures += x2_movie_image_pitch(0, 480, 2097152, &pitch);
    printf("movie igImage layout: %s -- display dimensions map to the "
           "power-of-two allocation and its real row pitch\n",
           failures ? "FAILED" : "PASSED");
    return failures != 0;
}
