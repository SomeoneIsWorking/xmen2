#include "gpu_matrix.h"

#include <string.h>

void gpu_matrix_multiply(const float a[16], const float b[16], float out[16])
{
    float result[16];
    int row, column, k;
    for (row = 0; row < 4; row++)
        for (column = 0; column < 4; column++) {
            float value = 0.0f;
            for (k = 0; k < 4; k++)
                value += a[row * 4 + k] * b[k * 4 + column];
            result[row * 4 + column] = value;
        }
    memcpy(out, result, sizeof result);
}
