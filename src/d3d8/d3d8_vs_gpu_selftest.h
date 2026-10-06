#ifndef D3D8_VS_GPU_SELFTEST_H
#define D3D8_VS_GPU_SELFTEST_H

/* One VS 1.1 program drawn by the GPU's interpreter and by the CPU executor
   must give the same pixels (issue #187). */
int d3d8_vs_gpu_selftest(void);

#endif /* D3D8_VS_GPU_SELFTEST_H */
