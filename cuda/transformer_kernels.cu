#include <cuda_runtime.h>
#include <cstdio>

#define CUDA_CHECK(call) \
    do { \
        cudaError_t err = call; \
        if (err != cudaSuccess) { \
            std::fprintf(stderr, "CUDA error at %s:%d: %s\n", \
                         __FILE__, __LINE__, cudaGetErrorString(err)); \
        } \
    } while (0)

__global__ void elementwise_kernel(const float* w, const float* x,
                                   float* y, int dim) {
    int i = blockIdx.x * blockDim.x + threadIdx.x;
    if (i >= dim) return;
    y[i] = w[i] * x[i];
}

__global__ void residual_ln_kernel(const float* x, const float* residual,
                                   const float* ln_w, const float* ln_b,
                                   float* out, int dim) {
    int i = blockIdx.x * blockDim.x + threadIdx.x;
    if (i >= dim) return;
    float val = x[i] + residual[i];
    out[i] = val * ln_w[i] + ln_b[i];
}

extern "C" {

void civ_cuda_elementwise(const float* w, const float* x, float* y, int dim) {
    int block = 256;
    int grid = (dim + block - 1) / block;
    elementwise_kernel<<<grid, block>>>(w, x, y, dim);
    CUDA_CHECK(cudaDeviceSynchronize());
}

void civ_cuda_residual_ln(const float* x, const float* residual,
                          const float* ln_w, const float* ln_b,
                          float* out, int dim) {
    int block = 256;
    int grid = (dim + block - 1) / block;
    residual_ln_kernel<<<grid, block>>>(x, residual, ln_w, ln_b, out, dim);
    CUDA_CHECK(cudaDeviceSynchronize());
}

} // extern "C"