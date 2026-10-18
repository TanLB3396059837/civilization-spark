#include <cuda_runtime.h>
#include <math_constants.h>

__global__ void attention_kernel(const float* Q, const float* K,
                                 const float* V, float* out,
                                 int seq_len, int head_dim) {
    int idx = blockIdx.x * blockDim.x + threadIdx.x;
    if (idx >= seq_len * head_dim) return;

    int q_pos = idx / head_dim;
    int d     = idx % head_dim;

    // 数值稳定的 softmax 注意力：先求最大分数，再归一化。
    float max_score = -CUDART_INF_F;
    for (int k_pos = 0; k_pos < seq_len; ++k_pos) {
        float dot = 0.0f;
        for (int dd = 0; dd < head_dim; ++dd) {
            dot += Q[q_pos * head_dim + dd] * K[k_pos * head_dim + dd];
        }
        dot /= sqrtf(static_cast<float>(head_dim));
        max_score = fmaxf(max_score, dot);
    }

    float denom = 0.0f;
    float acc = 0.0f;
    for (int k_pos = 0; k_pos < seq_len; ++k_pos) {
        float dot = 0.0f;
        for (int dd = 0; dd < head_dim; ++dd) {
            dot += Q[q_pos * head_dim + dd] * K[k_pos * head_dim + dd];
        }
        dot /= sqrtf(static_cast<float>(head_dim));
        float w = expf(dot - max_score);
        denom += w;
        acc += w * V[k_pos * head_dim + d];
    }
    out[idx] = denom > 0.0f ? acc / denom : 0.0f;
}

extern "C" {

void civ_cuda_attention(const float* Q, const float* K, const float* V,
                        float* out, int seq_len, int head_dim) {
    int total = seq_len * head_dim;
    int block = 256;
    int grid = (total + block - 1) / block;
    attention_kernel<<<grid, block>>>(Q, K, V, out, seq_len, head_dim);
    cudaDeviceSynchronize();
}

} // extern "C"
