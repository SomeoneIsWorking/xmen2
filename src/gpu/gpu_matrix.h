/* Shared 4x4 row-major matrix operations used at renderer boundaries. */
#pragma once

namespace x2::gpu {

void gpu_matrix_multiply(const float a[16], const float b[16], float out[16]);

} // namespace x2::gpu
