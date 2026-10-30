#pragma once
#include "../core/types.hpp"
#include "../core/vector.hpp"
#include "../quantum/quantum_rng.hpp"
#include "shared_model.hpp"
#include <string>
#include <vector>
#include <array>
#include <deque>
#include <cstdint>

namespace civ {

enum class EvolutionStyle {
    Consensus,      // 多智能体共识
    Constraint,     // 约束投影
    Replay,         // 经验回放
    Selection,      // 进化筛选
    Adversarial,    // 对抗/自否
    Distillation,   // 蒸馏/协作
    Reward,         // 奖励最大化
    Critique,       // 误差批判
    Regularization, // 正则化
    Competition,    // 竞争博弈
    Pruning,        // 剪枝
    Exploration,    // 探索
    Meta            // 元学习/融合
};

struct TransformerConfig {
    std::size_t d_model    = DIM;
    std::size_t n_heads    = 4;
    std::size_t n_layers   = 2;
    std::size_t ffn_hidden = DIM * 2;
};

// 真矩阵权重（行主序）
struct TransformerBlock {
    alignas(64) std::array<float, DIM * DIM> Wq, Wk, Wv, Wo;
    alignas(64) std::array<float, DIM * 2 * DIM> ff_w1;  // [2DIM × DIM]
    alignas(64) std::array<float, DIM * 2>       ff_b1;
    alignas(64) std::array<float, DIM * 2 * DIM> ff_w2;  // [DIM × 2DIM]
    alignas(64) std::array<float, DIM>           ff_b2;
    alignas(64) std::array<float, DIM> ln1_w, ln1_b, ln2_w, ln2_b;
};

struct AgentState {
    int seq_id = -1;
    int n_past = 0;
    std::deque<std::int32_t> history;
    std::vector<float> last_logits;

    static constexpr std::size_t MAX_HISTORY   = 4096;
    static constexpr std::size_t WARMUP_TOKENS = 128;
};

enum class MemoryKind {
    Episode,
    Feedback,
    Reflection,
    World,
    Inherited
};

struct MemoryEntry {
    std::uint64_t local_id = 0;
    int round_no = 0;
    int last_access_round = 0;
    MemoryKind kind = MemoryKind::Episode;
    std::string source;
    std::string content;
    Vector embedding;
    float importance = 0.5f;
    float retention = 1.0f;
    int recall_count = 0;
    bool consolidated = false;
};

// Operational belief state. This is not a claim of human-like consciousness.
struct CognitionRegion {
    float confidence = 0.35f;
    float coherence = 0.50f;
    float contradiction = 0.00f;
    float uncertainty = 0.65f;
    float curiosity = 0.55f;
    std::string active_strategy = "observe";
    Vector belief;
    int revisions = 0;
};

// Meta-cognitive state used to regulate attention, goals and self-training.
struct ConsciousnessRegion {
    float self_model_confidence = 0.20f;
    float goal_alignment = 0.50f;
    float attention = 0.50f;
    float prediction_error = 0.50f;
    float integrity = 1.00f;
    std::string current_goal = "understand the first cause of life";
    std::string self_model = "initial self-model";
    int reflections = 0;
};

struct TrainingExample {
    std::uint64_t local_id = 0;
    int round_no = 0;
    std::string source;
    std::string instruction;
    std::string prompt;
    std::string response;
    float score = 0.0f;
    std::string feedback;
    bool accepted = false;
};

struct SelfTrainingUpdate {
    float score = 0.0f;
    float novelty = 0.0f;
    float retrieval_relevance = 0.0f;
    bool accepted = false;
    TrainingExample sample;
};

struct MindSnapshot {
    std::size_t memory_count = 0;
    std::size_t training_samples = 0;
    std::size_t accepted_samples = 0;
    float average_training_score = 0.0f;
    float efficiency = 1.0f;
    CognitionRegion cognition;
    ConsciousnessRegion consciousness;
};

class LLMAgent {
public:
    LLMAgent(AgentId id, const TransformerConfig& cfg, std::uint64_t seed = 0);
    ~LLMAgent();

    // 内置 Transformer
    Vector encode(const std::string& text);
    std::string decode(const Vector& vec);
    Vector predict_next(const Vector& context);

    // LLM
    std::string generate(const std::string& prompt, int max_tokens = 32);
    std::string generate_chat(const std::string& system,
                              const std::string& user,
                              int max_tokens = 32);
    void reset_dialogue_context();
    void begin_generation(const std::string& prompt, int max_tokens, int n_ctx);
    std::int32_t sample_next_token();
    void set_logits(std::vector<float>&& logits);
    bool is_generating() const noexcept { return gen_remaining_ > 0; }
    std::string take_result();

    // 学习与进化
    void learn(const Vector& experience, float lr = 0.001f);
    void mutate(float rate = 0.01f);

    // 效率
    float efficiency() const { return efficiency_; }
    void update_efficiency(float delta);

    // 世界观
    void set_worldview(const std::string& view);
    const std::string& worldview() const { return worldview_; }
    const std::string& factor_name() const { return factor_name_; }
    const std::string& heir_name() const { return heir_name_; }
    const std::string& titan_path() const { return titan_path_; }
    const std::string& drive() const { return drive_; }
    const std::string& simulated_path() const { return simulated_path_; }
    const std::string& ai_route() const { return ai_route_; }
    const std::string& evolution_method() const { return evolution_method_; }
    EvolutionStyle evolution_style() const { return evolution_style_; }
    const Vector& embedding() const { return embedding_; }
    std::string express_first_cause() const;
    std::string answer_first_cause() const;

    // Memory, cognition and meta-cognitive self-training regions.
    void remember(const std::string& content, MemoryKind kind,
                  float importance, int round_no,
                  const std::string& source = "internal");
    std::vector<const MemoryEntry*> recall(const std::string& query,
                                           std::size_t top_k = 3);
    std::string mind_context(const std::string& query,
                             std::size_t top_k = 3);
    SelfTrainingUpdate self_train(const std::string& prompt,
                                  const std::string& response,
                                  int round_no,
                                  const std::string& source,
                                  float external_reward = -1.0f);
    void consolidate_memory(int round_no);
    void inherit_from(const LLMAgent& predecessor, float inheritance_ratio = 0.5f);
    MindSnapshot mind_snapshot() const;
    std::size_t export_training_jsonl(const std::string& path,
                                      std::int64_t run_id,
                                      bool append = false) const;

    const std::deque<MemoryEntry>& memories() const noexcept { return memories_; }
    const CognitionRegion& cognition() const noexcept { return cognition_; }
    const ConsciousnessRegion& consciousness() const noexcept {
        return consciousness_;
    }
    const std::vector<TrainingExample>& training_samples() const noexcept {
        return training_samples_;
    }

    // 按因子自带的 AI 学习路线执行一次进化。
    // collective: 当前存活因子的平均表征；rival: 最强因子的表征。
    void evolve(const Vector& experience, const Vector& collective,
                const Vector& rival, float lr = 0.01f);

    AgentId id() const { return id_; }

    // 通信统计
    void record_sent(std::size_t b) { bytes_sent_ += b; ++frames_sent_; }
    void record_recv(std::size_t b) { bytes_recv_ += b; ++frames_recv_; }
    std::size_t bytes_sent() const { return bytes_sent_; }
    std::size_t bytes_recv() const { return bytes_recv_; }
    std::size_t frames_sent() const { return frames_sent_; }
    std::size_t frames_recv() const { return frames_recv_; }

    AgentState& state() noexcept { return state_; }
    const AgentState& state() const noexcept { return state_; }

private:
    AgentId id_;
    TransformerConfig cfg_;
    std::vector<TransformerBlock> blocks_;
    Vector embedding_;
    Vector context_;
    float efficiency_ = 1.0f;
    std::string worldview_;
    std::string factor_name_;
    std::string heir_name_;
    std::string titan_path_;
    std::string drive_;
    std::string simulated_path_;
    std::string ai_route_;
    std::string evolution_method_;
    EvolutionStyle evolution_style_ = EvolutionStyle::Consensus;
    std::string first_cause_;
    Vector memory_;   // 经验回放/记忆巩固
    std::uint32_t mutation_count_ = 0;
    QuantumRNG rng_;

    std::deque<MemoryEntry> memories_;
    CognitionRegion cognition_;
    ConsciousnessRegion consciousness_;
    std::vector<TrainingExample> training_samples_;
    std::uint64_t next_memory_id_ = 1;
    std::uint64_t next_training_id_ = 1;
    std::size_t accepted_training_samples_ = 0;
    float training_score_sum_ = 0.0f;
    bool self_training_enabled_ = true;
    float training_threshold_ = 0.62f;

    AgentState state_;

    int  gen_remaining_ = 0;
    std::string gen_result_;
    int  n_ctx_hint_ = 2048;
    float temperature_ = 0.75f;
    float top_p_ = 0.9f;
    int   top_k_ = 40;
    float repeat_penalty_ = 1.1f;

    std::size_t bytes_sent_ = 0, bytes_recv_ = 0;
    std::size_t frames_sent_ = 0, frames_recv_ = 0;

    void forward_cpu(const Vector& input, Vector& output);
    void multi_head_attention(const Vector& hidden, TransformerBlock& block,
                              Vector& out);
    void ffn(const Vector& x, TransformerBlock& block, Vector& out);

    bool tokenize_checked(const std::string& text,
                          std::vector<std::int32_t>& out_tokens);
    std::string token_to_text(std::int32_t token);
    bool maybe_rebuild_kv(int needed_tokens);
    bool warmup_kv_from_history();
    Vector semantic_embedding(const std::string& text) const;
    float semantic_similarity(const Vector& a, const Vector& b) const;
    float evaluate_response(const std::string& response) const;
    std::string reflect(const TrainingExample& sample) const;
    void update_regions(const TrainingExample& sample, float novelty,
                        float retrieval_relevance);
    void prune_memories();
};

} // namespace civ
