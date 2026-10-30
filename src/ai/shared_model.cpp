#include "civ/ai/shared_model.hpp"
#include <iostream>
#include <stdexcept>
#include <cstring>
#include <algorithm>
#include <limits>

#ifdef CIV_USE_LLAMA
#include <llama.h>
#endif

namespace civ {

namespace {
std::atomic<SharedModelPool*> g_active_pool{nullptr};
}

SharedModelPool& SharedModelPool::instance() {
    static SharedModelPool pool;
    g_active_pool.store(&pool, std::memory_order_release);
    return pool;
}

bool SharedModelPool::available() noexcept {
    SharedModelPool* pool =
        g_active_pool.load(std::memory_order_acquire);
    return pool != nullptr && pool->is_loaded();
}

void SharedModelPool::shutdown_active() {
    SharedModelPool* pool =
        g_active_pool.load(std::memory_order_acquire);
    if (pool) pool->shutdown();
}

void SharedModelPool::load(const std::string& model_path,
                           int n_ctx, int n_gpu_layers) {
    if (loaded_) return;
    if (n_ctx <= 0)
        throw std::invalid_argument("n_ctx must be positive");

    n_ctx_per_seq_ = n_ctx;
    const std::uint64_t total_ctx =
        static_cast<std::uint64_t>(n_ctx_per_seq_) * MAX_SEQ;
    n_ctx_ = static_cast<int>(std::min<std::uint64_t>(
        total_ctx, std::numeric_limits<int>::max()));

#ifdef CIV_USE_LLAMA
    llama_backend_init();
    backend_initialized_ = true;

    auto mparams = llama_model_default_params();
    mparams.n_gpu_layers = n_gpu_layers;

    // ★ 使用新 API 名（旧名在 0.6.x 里仍是宏别名，会告警）
    model_ = llama_model_load_from_file(model_path.c_str(), mparams);
    if (!model_) {
        std::cerr << "[SharedModel] 无法加载: " << model_path << "\n";
        return;
    }

    // ★ 关键：获取 vocab 指针
    vocab_ = llama_model_get_vocab(model_);
    if (!vocab_) {
        std::cerr << "[SharedModel] 无法获取 vocab\n";
        llama_model_free(model_);
        model_ = nullptr;
        return;
    }

    auto cparams = llama_context_default_params();
    // llama.cpp 的 n_ctx 是整个上下文池的总容量：
    // 实际每序列容量 = n_ctx / n_seq_max。这里必须乘以序列数，
    // 否则 13 个 Agent 启动后每个只有约 157 个 token 的上下文。
    cparams.n_ctx     = static_cast<std::uint32_t>(n_ctx_);
    cparams.n_batch   = static_cast<std::uint32_t>(std::min(256, n_ctx_per_seq_));
    cparams.n_seq_max = MAX_SEQ;
    n_batch_ = static_cast<int>(cparams.n_batch);

    // ★ 使用新 API 名
    ctx_ = llama_init_from_model(model_, cparams);
    if (!ctx_) {
        std::cerr << "[SharedModel] 无法创建上下文\n";
        llama_model_free(model_);
        model_ = nullptr;
        vocab_ = nullptr;
        return;
    }

    // ★ 用 vocab 指针取 n_vocab
    n_vocab_ = llama_vocab_n_tokens(vocab_);
    loaded_  = true;

    batch_capacity_ = n_batch_;
    batch_cache_ = new llama_batch(
        llama_batch_init(batch_capacity_, 0, 1));

    std::cout << "[SharedModel] 加载成功 | n_vocab=" << n_vocab_
              << " | n_ctx/seq=" << n_ctx_per_seq_
              << " | n_ctx(total)=" << n_ctx_
              << " | n_seq_max=" << MAX_SEQ
              << " | n_gpu_layers=" << n_gpu_layers << "\n";
#else
    std::cerr << "[SharedModel] llama.cpp 未编译\n";
#endif
}

int SharedModelPool::allocate_seq_id() {
    const int id =
        next_seq_.fetch_add(1, std::memory_order_relaxed) % MAX_SEQ;
    seq_in_use_[id].store(true, std::memory_order_relaxed);
    return id;
}

void SharedModelPool::reset_sequence(int seq_id) {
#ifdef CIV_USE_LLAMA
    if (!ctx_) return;
    if (seq_id < 0 || seq_id >= MAX_SEQ) return;
    std::lock_guard<std::mutex> lock(decode_mutex_);

    // ★ 新 API：llama_get_memory(ctx) 返回 llama_memory_i*
    //    旧 llama_kv_cache_seq_rm 已移除
    auto* mem = llama_get_memory(ctx_);
    if (mem) {
        llama_memory_seq_rm(mem, seq_id, -1, -1);
    }
#else
    (void)seq_id;
#endif
}

bool SharedModelPool::batch_decode(const std::vector<DecodeRequest>& reqs,
                                   std::vector<float>& out_logits) {
#ifdef CIV_USE_LLAMA
    if (!loaded_ || reqs.empty()) return false;

    std::lock_guard<std::mutex> lock(decode_mutex_);

    if (n_batch_ <= 0)
        throw std::runtime_error("invalid llama batch size");

    if (static_cast<int>(reqs.size()) > batch_capacity_) {
        llama_batch_free(*batch_cache_);
        batch_capacity_ = std::min(static_cast<int>(reqs.size()), n_batch_);
        *batch_cache_ = llama_batch_init(batch_capacity_, 0, 1);
    }

    std::size_t n_logits_req = 0;
    for (const auto& r : reqs) {
        if (r.want_logits) ++n_logits_req;
    }

    out_logits.clear();
    out_logits.reserve(n_logits_req * static_cast<std::size_t>(n_vocab_));

    // n_batch 是单次 llama_decode 的逻辑上限。提示词可能很长，
    // 因此按 n_batch 分块提交，避免 GGML_ASSERT(n_tokens <= n_batch)。
    for (std::size_t off = 0; off < reqs.size(); off += n_batch_) {
        const std::size_t chunk =
            std::min<std::size_t>(n_batch_, reqs.size() - off);

        auto& b = *batch_cache_;
        b.n_tokens = static_cast<int32_t>(chunk);
        for (std::size_t i = 0; i < chunk; ++i) {
            const auto& r = reqs[off + i];
            b.token[i]     = r.token;
            b.pos[i]       = r.pos;
            b.n_seq_id[i]  = 1;
            b.seq_id[i][0] = r.seq_id;
            b.logits[i]    = r.want_logits ? 1 : 0;
        }

        if (llama_decode(ctx_, b) != 0) {
            std::cerr << "[SharedModel] llama_decode 失败 (chunk="
                      << chunk << ", total=" << reqs.size() << ")\n";
            return false;
        }

        for (std::size_t i = 0; i < chunk; ++i) {
            if (!reqs[off + i].want_logits) continue;
            const float* row =
                llama_get_logits_ith(ctx_, static_cast<int32_t>(i));
            if (!row) {
                std::cerr << "[SharedModel] 无法读取 logits\n";
                return false;
            }
            out_logits.insert(out_logits.end(), row, row + n_vocab_);
        }
    }

    return true;
#else
    (void)reqs; (void)out_logits;
    return false;
#endif
}

int SharedModelPool::sequence_kv_usage(int seq_id) const {
#ifdef CIV_USE_LLAMA
    if (seq_id < 0 || seq_id >= MAX_SEQ ||
        !seq_in_use_[seq_id].load(std::memory_order_relaxed))
        return 0;
    std::lock_guard<std::mutex> lock(decode_mutex_);
    // 新 API 不再提供简单的全局计数，改用 memory 对象的 pos 范围
    if (!ctx_) return 0;
    auto* mem = llama_get_memory(ctx_);
    if (!mem) return 0;
    // 返回该 seq 的最大位置 +1 作为近似占用
    auto pos_max = llama_memory_seq_pos_max(mem, seq_id);
    return pos_max >= 0 ? static_cast<int>(pos_max + 1) : 0;
#else
    (void)seq_id;
    return 0;
#endif
}

float SharedModelPool::kv_usage_ratio() const {
#ifdef CIV_USE_LLAMA
    std::lock_guard<std::mutex> lock(decode_mutex_);
    if (!ctx_) return 0.0f;
    auto* mem = llama_get_memory(ctx_);
    if (!mem) return 0.0f;
    // 用最大 seq 的占用量 / n_ctx 估算
    int used = 0;
    for (int s = 0; s < MAX_SEQ; ++s) {
        if (!seq_in_use_[s].load(std::memory_order_relaxed)) continue;
        auto pos_max = llama_memory_seq_pos_max(mem, s);
        if (pos_max >= 0) used += static_cast<int>(pos_max + 1);
    }
    return static_cast<float>(used) /
           static_cast<float>(std::max(1, n_ctx_));
#else
    return 0.0f;
#endif
}

std::string SharedModelPool::format_chat(const std::string& system,
                                         const std::string& user) const {
#ifdef CIV_USE_LLAMA
    if (!model_) return system + "\n\n" + user;
    const char* tmpl = llama_model_chat_template(model_, nullptr);
    if (!tmpl || !*tmpl) return system + "\n\n" + user;

    const std::string template_text(tmpl);
    const auto spark_fallback = [&]() {
        // Spark-X2.5 uses fullwidth sentence markers and <|Role|> tokens.
        const std::string start = "<｜start▁of▁sentence｜>";
        const std::string end = "<｜end▁of▁sentence｜>";
        return start + "<|System|>\n" + system + end +
               start + "<|User|>" + user + end +
               start + "<|Bot|></think>";
    };
    if (template_text.find("<|System|>") != std::string::npos &&
        template_text.find("start") != std::string::npos) {
        // The bundled llama_chat_apply_template does not know every Spark
        // template; format the known Spark role markers directly.
        const std::string start = "<｜start▁of▁sentence｜>";
        const std::string end = "<｜end▁of▁sentence｜>";
        return start + "<|System|>\n" + system + end +
               start + "<|User|>" + user + end +
               start + "<|Bot|></think>";
    }

    llama_chat_message messages[] = {
        {"system", system.c_str()},
        {"user", user.c_str()},
    };
    const std::size_t estimate =
        (system.size() + user.size()) * 2 + 512;
    std::vector<char> buffer(estimate);
    int n = llama_chat_apply_template(
        tmpl, messages, 2, true, buffer.data(),
        static_cast<int32_t>(buffer.size()));
    if (n < 0) return spark_fallback();
    if (static_cast<std::size_t>(n) > buffer.size()) {
        buffer.resize(static_cast<std::size_t>(n) + 1);
        n = llama_chat_apply_template(
            tmpl, messages, 2, true, buffer.data(),
            static_cast<int32_t>(buffer.size()));
        if (n < 0) return spark_fallback();
    }
    return std::string(buffer.data(), static_cast<std::size_t>(n));
#else
    return system + "\n\n" + user;
#endif
}

void SharedModelPool::shutdown() {
#ifdef CIV_USE_LLAMA
    std::lock_guard<std::mutex> lock(decode_mutex_);
    if (batch_cache_) {
        llama_batch_free(*batch_cache_);
        delete batch_cache_;
        batch_cache_ = nullptr;
    }
    if (ctx_) {
        llama_free(ctx_);
        ctx_ = nullptr;
    }
    if (model_) {
        llama_model_free(model_);
        model_ = nullptr;
    }
    if (backend_initialized_) {
        llama_backend_free();
        backend_initialized_ = false;
    }
#endif
    loaded_ = false;
#ifdef CIV_USE_LLAMA
    vocab_ = nullptr;
#endif
    n_vocab_ = 0;
    next_seq_.store(0, std::memory_order_relaxed);
    for (auto& in_use : seq_in_use_)
        in_use.store(false, std::memory_order_relaxed);
    g_active_pool.store(nullptr, std::memory_order_release);
}

SharedModelPool::~SharedModelPool() {
    shutdown();
}

} // namespace civ
