#include <cuda_runtime.h>

#include <cmath>
#include <cstdio>
#include <vector>

extern "C" {
void civ_cuda_elementwise(const float* w, const float* x, float* y, int dim);
void civ_cuda_attention(const float* Q, const float* K, const float* V,
                        float* out, int seq_len, int head_dim);
}

namespace {

bool elementwise_test() {
    constexpr int n = 1024;
    std::vector<float> w(n), x(n), ref(n), out(n);
    for (int i = 0; i < n; ++i) {
        w[i] = 0.001f * static_cast<float>(i % 17);
        x[i] = 0.002f * static_cast<float>(i % 23);
        ref[i] = w[i] * x[i];
    }

    float *dw = nullptr, *dx = nullptr, *dy = nullptr;
    cudaMalloc(&dw, n * sizeof(float));
    cudaMalloc(&dx, n * sizeof(float));
    cudaMalloc(&dy, n * sizeof(float));
    cudaMemcpy(dw, w.data(), n * sizeof(float), cudaMemcpyHostToDevice);
    cudaMemcpy(dx, x.data(), n * sizeof(float), cudaMemcpyHostToDevice);
    civ_cuda_elementwise(dw, dx, dy, n);
    cudaMemcpy(out.data(), dy, n * sizeof(float), cudaMemcpyDeviceToHost);
    cudaFree(dw); cudaFree(dx); cudaFree(dy);

    float max_err = 0.0f;
    for (int i = 0; i < n; ++i)
        max_err = std::fmax(max_err, std::fabs(out[i] - ref[i]));
    std::printf("  elementwise max_err=%g\n", max_err);
    return max_err < 1e-5f;
}

bool attention_test() {
    constexpr int seq = 8;
    constexpr int hd = 16;
    constexpr int n = seq * hd;
    std::vector<float> Q(n), K(n), V(n), ref(n), out(n);

    for (int i = 0; i < n; ++i) {
        Q[i] = std::sin(0.1f * static_cast<float>(i));
        K[i] = std::cos(0.07f * static_cast<float>(i));
        V[i] = 0.01f * static_cast<float>(i % 11);
    }

    for (int q = 0; q < seq; ++q) {
        float max_score = -1e30f;
        for (int k = 0; k < seq; ++k) {
            float dot = 0.0f;
            for (int d = 0; d < hd; ++d)
                dot += Q[q * hd + d] * K[k * hd + d];
            dot /= std::sqrt(static_cast<float>(hd));
            max_score = std::fmax(max_score, dot);
        }
        float denom = 0.0f;
        for (int k = 0; k < seq; ++k) {
            float dot = 0.0f;
            for (int d = 0; d < hd; ++d)
                dot += Q[q * hd + d] * K[k * hd + d];
            dot /= std::sqrt(static_cast<float>(hd));
            denom += std::exp(dot - max_score);
        }
        for (int d = 0; d < hd; ++d) {
            float acc = 0.0f;
            for (int k = 0; k < seq; ++k) {
                float dot = 0.0f;
                for (int dd = 0; dd < hd; ++dd)
                    dot += Q[q * hd + dd] * K[k * hd + dd];
                dot /= std::sqrt(static_cast<float>(hd));
                acc += std::exp(dot - max_score) * V[k * hd + d];
            }
            ref[q * hd + d] = acc / denom;
        }
    }

    float *dQ = nullptr, *dK = nullptr, *dV = nullptr, *dO = nullptr;
    cudaMalloc(&dQ, n * sizeof(float));
    cudaMalloc(&dK, n * sizeof(float));
    cudaMalloc(&dV, n * sizeof(float));
    cudaMalloc(&dO, n * sizeof(float));
    cudaMemcpy(dQ, Q.data(), n * sizeof(float), cudaMemcpyHostToDevice);
    cudaMemcpy(dK, K.data(), n * sizeof(float), cudaMemcpyHostToDevice);
    cudaMemcpy(dV, V.data(), n * sizeof(float), cudaMemcpyHostToDevice);
    civ_cuda_attention(dQ, dK, dV, dO, seq, hd);
    cudaMemcpy(out.data(), dO, n * sizeof(float), cudaMemcpyDeviceToHost);
    cudaFree(dQ); cudaFree(dK); cudaFree(dV); cudaFree(dO);

    float max_err = 0.0f;
    for (int i = 0; i < n; ++i)
        max_err = std::fmax(max_err, std::fabs(out[i] - ref[i]));
    std::printf("  attention max_err=%g\n", max_err);
    return max_err < 1e-4f;
}

} // namespace

int main() {
    std::printf("========== CUDA kernel 正确性测试 ==========\n");
    const bool ok1 = elementwise_test();
    const bool ok2 = attention_test();
    std::printf("  [%s] elementwise\n", ok1 ? "PASS" : "FAIL");
    std::printf("  [%s] attention softmax\n", ok2 ? "PASS" : "FAIL");
    return (ok1 && ok2) ? 0 : 1;
}
