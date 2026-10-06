/* Shared 4x4 row-major matrix operations used at renderer boundaries. */
#ifndef GPU_MATRIX_H
#define GPU_MATRIX_H

void gpu_matrix_multiply(const float a[16], const float b[16], float out[16]);

#endif
