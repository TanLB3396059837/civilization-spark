#pragma once
#include "../core/types.hpp"
#include <memory>
#include <mutex>
#include <vector>
#include <atomic>
#include <cstdint>

#ifdef CIV_USE_LLAMA
struct llama_model;
struct llama_context;
struct llama_batch;
struct llama_vocab;                       // ★ 新增
typedef int32_t llama_token;
#endif

namespace civ {

    struct DecodeRequest {
        int          seq_id;
        std::int32_t token;
        int          pos;
        bool         want_logits;
    };

    class SharedModelPool {
    public:
        static SharedModelPool& instance();

        void load(const std::string& model_path, int n_ctx = 2048,
                  int n_gpu_layers = 99);

        bool is_loaded() const noexcept { return loaded_; }

        static constexpr int MAX_SEQ = 16;
        int allocate_seq_id();
        void reset_sequence(int seq_id);

        bool batch_decode(const std::vector<DecodeRequest>& reqs,
                          std::vector<float>& out_logits);

        int n_vocab() const noexcept { return n_vocab_; }
        int n_ctx_per_seq() const noexcept { return n_ctx_per_seq_; }
        std::mutex& decode_mutex() { return decode_mutex_; }
        std::string format_chat(const std::string& system,
                                const std::string& user) const;

        int sequence_kv_usage(int seq_id) const;
        float kv_usage_ratio() const;

#ifdef CIV_USE_LLAMA
        llama_model*         model() const noexcept { return model_; }
        llama_context*       context() const noexcept { return ctx_; }
        const llama_vocab*   vocab() const noexcept { return vocab_; }   // ★ 新增
#endif

        ~SharedModelPool();

    private:
        SharedModelPool() = default;
        SharedModelPool(const SharedModelPool&) = delete;
        SharedModelPool& operator=(const SharedModelPool&) = delete;

        bool loaded_ = false;
        bool backend_initialized_ = false;
        int  n_vocab_ = 0;
        int  n_ctx_ = 2048;          // 整个上下文池的总容量
        int  n_ctx_per_seq_ = 2048;  // 每个 Agent 可用的上下文长度
        int  n_batch_ = 256;
        std::atomic<int> next_seq_{0};
        std::array<std::atomic<bool>, MAX_SEQ> seq_in_use_{};
        mutable std::mutex decode_mutex_;

#ifdef CIV_USE_LLAMA
        llama_model*         model_ = nullptr;
        llama_context*       ctx_   = nullptr;
        const llama_vocab*   vocab_ = nullptr;   // ★ 新增
        llama_batch*         batch_cache_ = nullptr;
        int                  batch_capacity_ = 0;
#endif
    };

} // namespace civ
