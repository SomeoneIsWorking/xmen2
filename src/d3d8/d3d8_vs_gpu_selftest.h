#pragma once

namespace x2::d3d8 {

/* One VS 1.1 program drawn by the GPU's interpreter and by the CPU executor
   must give the same pixels (issue #187). */
int d3d8_vs_gpu_selftest(void);

} // namespace x2::d3d8
