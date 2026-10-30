#include "civ/ai/llm_agent.hpp"
#include "civ/codec/frame_v4.hpp"
#include <cmath>
#include <sstream>
#include <iostream>
#include <iomanip>
#include <algorithm>
#include <random>
#include <cstring>
#include <cstdlib>
#include <limits>
#include <string_view>
#include <unordered_set>
#include <filesystem>
#include <fstream>
#include <cctype>

#ifdef CIV_USE_LLAMA
#include <llama.h>
#endif

namespace civ {

namespace {

const char* memory_kind_name(MemoryKind kind) {
    switch (kind) {
    case MemoryKind::Episode:    return "episode";
    case MemoryKind::Feedback:   return "feedback";
    case MemoryKind::Reflection: return "reflection";
    case MemoryKind::World:      return "world";
    case MemoryKind::Inherited:  return "inherited";
    }
    return "episode";
}

std::string escape_json(const std::string& text) {
    std::string out;
    out.reserve(text.size() + 16);
    for (unsigned char c : text) {
        switch (c) {
        case '"':  out += "\\\""; break;
        case '\\': out += "\\\\"; break;
        case '\b': out += "\\b"; break;
        case '\f': out += "\\f"; break;
        case '\n': out += "\\n"; break;
        case '\r': out += "\\r"; break;
        case '\t': out += "\\t"; break;
        default:
            if (c < 0x20) {
                static constexpr char kHex[] = "0123456789abcdef";
                out += "\\u00";
                out += kHex[(c >> 4) & 0x0F];
                out += kHex[c & 0x0F];
            } else {
                out += static_cast<char>(c);
            }
            break;
        }
    }
    return out;
}

std::vector<std::uint32_t> utf8_codepoints(const std::string& text) {
    std::vector<std::uint32_t> result;
    result.reserve(text.size());
    for (std::size_t i = 0; i < text.size();) {
        const auto c = static_cast<unsigned char>(text[i]);
        if (c < 0x80) {
            char value = static_cast<char>(c);
            if (value >= 'A' && value <= 'Z')
                value = static_cast<char>(value - 'A' + 'a');
            if (!std::isspace(static_cast<unsigned char>(value)))
                result.push_back(static_cast<std::uint32_t>(value));
            ++i;
            continue;
        }

        std::size_t width = 0;
        std::uint32_t cp = 0;
        if ((c & 0xE0) == 0xC0) {
            width = 2;
            cp = c & 0x1F;
        } else if ((c & 0xF0) == 0xE0) {
            width = 3;
            cp = c & 0x0F;
        } else if ((c & 0xF8) == 0xF0) {
            width = 4;
            cp = c & 0x07;
        } else {
            ++i;
            continue;
        }
        if (i + width > text.size()) break;

        bool valid = true;
        for (std::size_t j = 1; j < width; ++j) {
            const auto next = static_cast<unsigned char>(text[i + j]);
            if ((next & 0xC0) != 0x80) {
                valid = false;
                break;
            }
            cp = (cp << 6) | (next & 0x3F);
        }
        if (valid && cp >= 0x80) result.push_back(cp);
        i += valid ? width : 1;
    }
    return result;
}

std::string clip_utf8(const std::string& text, std::size_t limit) {
    if (text.size() <= limit) return text;
    std::size_t end = limit;
    while (end > 0 &&
           (static_cast<unsigned char>(text[end]) & 0xC0) == 0x80) {
        --end;
    }
    return text.substr(0, end) + "...";
}

// 13 个智能体与“黄金裔—泰坦路径—原动力—模拟命途”的映射。
// 顺序与 Simulation 中的 AgentId 0..12 一一对应。
struct AgentProfile {
    std::string_view factor;
    std::string_view heir;
    std::string_view titan_path;
    std::string_view drive;
    std::string_view simulated_path;
    std::string_view ai_route;
    std::string_view evolution_method;
    EvolutionStyle style;
    std::string_view first_cause;
};

constexpr std::array<AgentProfile, 13> kAgentProfiles{{
    {"Tribios", "缇宝", "门径", "同理", "同谐",
     "多智能体共识学习", "共识蒸馏：向群体平均表征靠拢，保留个体差异",
     EvolutionStyle::Consensus,
     "生命的第一因是同理：个体愿意理解他者，孤立的意识才连成文明。"},
    {"HubRis504", "刻律德菈", "律法", "支配", "秩序",
     "约束强化学习", "约束投影：把更新限制在可行域，维持秩序",
     EvolutionStyle::Constraint,
     "生命的第一因是支配：秩序以规则约束混沌，让群体得以延续。"},
    {"PhiLia093", "昔涟（最初的因子）", "岁月", "哀怜", "记忆",
     "持续学习与经验回放", "记忆回放：用历史表征重放，抵抗遗忘",
     EvolutionStyle::Replay,
     "生命的第一因是哀怜：对逝去之物的不忍，使记忆成为抵抗时间的方式。"},
    {"丹恒·腾荒", "丹恒·腾荒", "大地", "求存", "不朽",
     "进化策略（Evolution Strategies）", "生存筛选：变异后只保留更适应环境的解",
     EvolutionStyle::Selection,
     "生命的第一因是求存：扎根大地的意志，使存在本身成为不朽的起点。"},
    {"ApoRia432", "海瑟音", "海洋", "自否", "虚无",
     "对抗学习与负采样", "自否扰动：沿相反方向更新，主动制造认知裂缝",
     EvolutionStyle::Adversarial,
     "生命的第一因是自否：在虚无中否定既定意义，生命才被迫创造自己的价值。"},
    {"雅辛忒丝", "风堇", "天空", "奉献", "存护",
     "知识蒸馏与协作学习", "奉献蒸馏：把自身表征传给集体，优先补足弱者",
     EvolutionStyle::Distillation,
     "生命的第一因是奉献：把自身交给更广阔的天空，才换来群体的存护。"},
    {"NeiKos496", "白厄", "负世", "憎恨", "毁灭",
     "奖励最大化强化学习", "憎恨驱动：放大高奖励方向，扰动旧策略",
     EvolutionStyle::Reward,
     "生命的第一因是憎恨：对毁灭性不公的抗争，催生了推翻旧世界的力量。"},
    {"SkeMma720", "那刻夏", "理性", "批判", "智识",
     "主动学习与符号推理", "批判修正：定位最大误差维度并定向更新",
     EvolutionStyle::Critique,
     "生命的第一因是批判：理性不断拆解旧答案，知识才得以生长。"},
    {"阿格莱雅", "阿格莱雅", "浪漫", "节制", "纯美",
     "正则化与审美约束", "节制更新：权重衰减与温度约束，避免过拟合",
     EvolutionStyle::Regularization,
     "生命的第一因是节制：在欲望与形式之间保持尺度，美才成为可延续的秩序。"},
    {"迈德漠斯", "万敌", "纷争", "约束", "巡猎",
     "竞争式协同进化", "纷争博弈：与最强对手比较后定向变异",
     EvolutionStyle::Competition,
     "生命的第一因是约束：以有限的规则驾驭纷争，猎手才能守住自己的道路。"},
    {"EpieiKeia216", "遐蝶", "死亡", "平和", "均衡",
     "模型剪枝与均衡学习", "死亡剪枝：移除弱连接，保持表征均衡",
     EvolutionStyle::Pruning,
     "生命的第一因是平和：承认死亡是生命的一部分，存在才获得均衡。"},
    {"OreXis945", "赛飞儿", "诡计", "渴望", "欢愉",
     "好奇心驱动探索", "诡计探索：高随机性变异，跳出局部最优",
     EvolutionStyle::Exploration,
     "生命的第一因是渴望：对快乐与未知的追逐，让生命不断越过既定边界。"},
    {"德谬歌", "昔涟（第十三因子）", "岁月/全世", "爱？（待确认）", "记忆/开拓",
     "元学习与多模态融合", "全世整合：融合共识、记忆与探索，保留不确定性",
     EvolutionStyle::Meta,
     "生命的第一因或许是爱（待确认）：在记忆与开拓之间，爱让分离的岁月重新连成整体。"},
}};

} // namespace

static void layer_norm(Vector& x, const std::array<float, DIM>& w,
                       const std::array<float, DIM>& b) {
    float mean = 0.0f, var = 0.0f;
    for (auto v : x.v) mean += v;
    mean /= DIM;
    for (auto v : x.v) var += (v - mean) * (v - mean);
    var /= DIM;
    float inv = 1.0f / std::sqrt(var + 1e-5f);
    for (std::size_t i = 0; i < DIM; ++i)
        x[i] = (x[i] - mean) * inv * w[i] + b[i];
}

LLMAgent::LLMAgent(AgentId id, const TransformerConfig& cfg, std::uint64_t seed)
    : id_(id), cfg_(cfg), blocks_(cfg.n_layers), rng_(seed + id * 7919) {
    std::mt19937_64 rng_init(seed + id * 7919);
    // Xavier 初始化
    std::normal_distribution<float> init_q(0.0f, std::sqrt(2.0f / DIM));
    std::normal_distribution<float> init_ff1(0.0f, std::sqrt(2.0f / DIM));
    std::normal_distribution<float> init_ff2(0.0f, std::sqrt(2.0f / (DIM * 2)));

    for (auto& block : blocks_) {
        for (auto& w : block.Wq) w = init_q(rng_init);
        for (auto& w : block.Wk) w = init_q(rng_init);
        for (auto& w : block.Wv) w = init_q(rng_init);
        for (auto& w : block.Wo) w = init_q(rng_init);
        for (auto& w : block.ff_w1) w = init_ff1(rng_init);
        for (auto& w : block.ff_b1) w = 0.0f;
        for (auto& w : block.ff_w2) w = init_ff2(rng_init);
        for (auto& w : block.ff_b2) w = 0.0f;
        block.ln1_w.fill(1.0f); block.ln1_b.fill(0.0f);
        block.ln2_w.fill(1.0f); block.ln2_b.fill(0.0f);
    }

    const AgentProfile& profile =
        kAgentProfiles[id % kAgentProfiles.size()];
    factor_name_   = profile.factor;
    heir_name_     = profile.heir;
    titan_path_    = profile.titan_path;
    drive_         = profile.drive;
    simulated_path_ = profile.simulated_path;
    ai_route_      = profile.ai_route;
    evolution_method_ = profile.evolution_method;
    evolution_style_  = profile.style;
    first_cause_   = profile.first_cause;
    worldview_ = titan_path_ + " · " + drive_ + " → " + simulated_path_;
    embedding_ = encode(first_cause_);
    memory_    = embedding_;

    if (SharedModelPool::available()) {
        state_.seq_id = SharedModelPool::instance().allocate_seq_id();
    }

    if (const char* nctx = std::getenv("CIV_N_CTX"))
        n_ctx_hint_ = std::atoi(nctx);
    if (const char* value = std::getenv("CIV_TEMPERATURE"))
        temperature_ = std::clamp(std::strtof(value, nullptr), 0.1f, 2.0f);
    if (const char* value = std::getenv("CIV_TOP_P"))
        top_p_ = std::clamp(std::strtof(value, nullptr), 0.1f, 1.0f);
    if (const char* value = std::getenv("CIV_TOP_K"))
        top_k_ = std::clamp(std::atoi(value), 0, 512);
    if (const char* value = std::getenv("CIV_REPEAT_PENALTY"))
        repeat_penalty_ =
            std::clamp(std::strtof(value, nullptr), 1.0f, 2.0f);
    if (const char* value = std::getenv("CIV_SELF_TRAIN"))
        self_training_enabled_ = std::atoi(value) != 0;
    if (const char* value = std::getenv("CIV_TRAINING_MIN_SCORE"))
        training_threshold_ =
            std::clamp(std::strtof(value, nullptr), 0.0f, 1.0f);

    cognition_.belief = semantic_embedding(first_cause_);
    consciousness_.current_goal =
        "understand the first cause of life through " + simulated_path_;
    consciousness_.self_model =
        factor_name_ + " | " + ai_route_ + " | " + evolution_method_;
    remember(first_cause_, MemoryKind::Episode, 0.90f, 0, "genesis");
}

LLMAgent::~LLMAgent() {
    // EOS 或析构时清理序列 KV
    if (state_.seq_id >= 0 && SharedModelPool::available()) {
        SharedModelPool::instance().reset_sequence(state_.seq_id);
    }
}

Vector LLMAgent::encode(const std::string& text) {
    Vector vec;
    std::size_t h = std::hash<std::string>{}(text);
    for (std::size_t i = 0; i < DIM; ++i) {
        h = h * 6364136223846793005ULL + 1442695040888963407ULL;
        vec[i] = static_cast<float>((h >> 32) & 0xFFFF) / 65535.0f;
    }
    embedding_ = vec;
    return vec;
}

std::string LLMAgent::decode(const Vector& vec) {
    std::ostringstream oss;
    oss << "[";
    for (std::size_t i = 0; i < 4; ++i) {
        oss << std::fixed << std::setprecision(2) << vec[i];
        if (i < 3) oss << ",";
    }
    oss << "...]";
    return oss.str();
}

// 真多头注意力
Vector LLMAgent::semantic_embedding(const std::string& text) const {
    Vector result;
    const auto codepoints = utf8_codepoints(text);
    if (codepoints.empty()) return result;

    auto add_feature = [&](std::uint32_t value, float weight) {
        std::uint64_t h = 1469598103934665603ULL;
        h ^= value;
        h *= 1099511628211ULL;
        h ^= h >> 33;
        const std::size_t index = static_cast<std::size_t>(h % DIM);
        const std::size_t index2 =
            static_cast<std::size_t>((h >> 16) % DIM);
        const float sign = ((h >> 8) & 1ULL) ? 1.0f : -1.0f;
        result[index] += sign * weight;
        result[index2] += (sign > 0.0f ? 0.5f : -0.5f) * weight;
    };

    for (std::size_t i = 0; i < codepoints.size(); ++i) {
        add_feature(codepoints[i], 0.35f);
        if (i + 1 < codepoints.size()) {
            const std::uint32_t pair =
                codepoints[i] * 1315423911u + codepoints[i + 1];
            add_feature(pair, 1.0f);
        }
        if (i + 2 < codepoints.size()) {
            const std::uint32_t triple =
                codepoints[i] * 2654435761u +
                codepoints[i + 1] * 2246822519u + codepoints[i + 2];
            add_feature(triple, 1.25f);
        }
    }

    float norm = 0.0f;
    for (float value : result.v) norm += value * value;
    norm = std::sqrt(norm);
    if (norm > 1e-8f) {
        for (std::size_t i = 0; i < DIM; ++i) result[i] /= norm;
    }
    return result;
}

float LLMAgent::semantic_similarity(const Vector& a, const Vector& b) const {
    float dot = 0.0f;
    float aa = 0.0f;
    float bb = 0.0f;
    for (std::size_t i = 0; i < DIM; ++i) {
        dot += a[i] * b[i];
        aa += a[i] * a[i];
        bb += b[i] * b[i];
    }
    if (aa < 1e-8f || bb < 1e-8f) return 0.0f;
    return std::clamp(dot / std::sqrt(aa * bb), -1.0f, 1.0f);
}

void LLMAgent::remember(const std::string& content, MemoryKind kind,
                        float importance, int round_no,
                        const std::string& source) {
    if (content.empty()) return;
    const Vector embedding = semantic_embedding(content);

    for (auto& existing : memories_) {
        if (existing.kind == kind &&
            semantic_similarity(existing.embedding, embedding) > 0.96f) {
            existing.content = content;
            existing.importance =
                std::clamp(std::max(existing.importance, importance), 0.0f, 1.0f);
            existing.last_access_round = round_no;
            existing.retention = std::min(1.0f, existing.retention + 0.08f);
            existing.source = source;
            return;
        }
    }

    MemoryEntry entry;
    entry.local_id = next_memory_id_++;
    entry.round_no = round_no;
    entry.last_access_round = round_no;
    entry.kind = kind;
    entry.source = source;
    entry.content = content;
    entry.embedding = embedding;
    entry.importance = std::clamp(importance, 0.0f, 1.0f);
    entry.retention = entry.importance;
    memories_.push_back(std::move(entry));
    prune_memories();
}

std::vector<const MemoryEntry*> LLMAgent::recall(const std::string& query,
                                                 std::size_t top_k) {
    std::vector<const MemoryEntry*> result;
    if (memories_.empty() || top_k == 0) return result;

    const Vector query_embedding = semantic_embedding(query);
    std::vector<std::pair<float, std::size_t>> ranked;
    ranked.reserve(memories_.size());
    int newest_round = 0;
    for (const auto& memory : memories_)
        newest_round = std::max(newest_round, memory.round_no);

    for (std::size_t i = 0; i < memories_.size(); ++i) {
        const auto& memory = memories_[i];
        const float similarity = std::max(
            0.0f, semantic_similarity(query_embedding, memory.embedding));
        const int age = std::max(0, newest_round - memory.last_access_round);
        const float recency = 1.0f / (1.0f + 0.20f * static_cast<float>(age));
        const float score = similarity * 0.72f +
                            memory.retention * 0.18f + recency * 0.10f;
        ranked.emplace_back(score, i);
    }

    std::sort(ranked.begin(), ranked.end(),
              [](const auto& left, const auto& right) {
                  return left.first > right.first;
              });
    const std::size_t count = std::min(top_k, ranked.size());
    result.reserve(count);
    for (std::size_t i = 0; i < count; ++i) {
        auto& memory = memories_[ranked[i].second];
        ++memory.recall_count;
        memory.last_access_round = newest_round;
        memory.retention = std::min(1.0f, memory.retention + 0.025f);
        result.push_back(&memory);
    }
    return result;
}

std::string LLMAgent::mind_context(const std::string& query,
                                   std::size_t top_k) {
    std::ostringstream oss;
    const auto recalled = recall(query, top_k);
    oss << "[记忆区] 记忆=" << memories_.size() << "，相关回忆=";
    if (recalled.empty()) {
        oss << "无";
    } else {
        for (std::size_t i = 0; i < recalled.size(); ++i) {
            if (i > 0) oss << " | ";
            oss << "[" << memory_kind_name(recalled[i]->kind) << "] "
                << clip_utf8(recalled[i]->content, 72);
        }
    }
    oss << "\n[认知区] 置信=" << std::fixed << std::setprecision(2)
        << cognition_.confidence
        << "，连贯=" << cognition_.coherence
        << "，矛盾=" << cognition_.contradiction
        << "，不确定=" << cognition_.uncertainty
        << "，策略=" << cognition_.active_strategy;
    oss << "\n[意识区/元认知] 自我模型置信="
        << consciousness_.self_model_confidence
        << "，目标对齐=" << consciousness_.goal_alignment
        << "，注意=" << consciousness_.attention
        << "，预测误差=" << consciousness_.prediction_error
        << "，完整度=" << consciousness_.integrity
        << "，目标=" << clip_utf8(consciousness_.current_goal, 56);
    return oss.str();
}

float LLMAgent::evaluate_response(const std::string& response) const {
    const auto codepoints = utf8_codepoints(response);
    if (codepoints.size() < 6) return 0.05f;

    std::unordered_set<std::uint32_t> unique;
    unique.reserve(codepoints.size());
    for (std::uint32_t cp : codepoints) unique.insert(cp);
    const float diversity = std::clamp(
        static_cast<float>(unique.size()) /
            static_cast<float>(codepoints.size()),
        0.0f, 1.0f);
    const float length_quality = std::clamp(
        static_cast<float>(codepoints.size()) / 72.0f, 0.0f, 1.0f);
    const Vector response_embedding = semantic_embedding(response);
    const float alignment = std::clamp(
        semantic_similarity(response_embedding, cognition_.belief),
        0.0f, 1.0f);

    float score = 0.18f + length_quality * 0.20f +
                  alignment * 0.34f + diversity * 0.18f;
    static const char* const kRejectedMarkers[] = {
        "###", "<|", "as an ai", "prompt:", "system:", "output format",
        "要求：", "提示词", "输出格式", "严格遵循"
    };
    for (const char* marker : kRejectedMarkers) {
        if (response.find(marker) != std::string::npos) {
            score -= 0.32f;
            break;
        }
    }
    return std::clamp(score, 0.0f, 1.0f);
}

std::string LLMAgent::reflect(const TrainingExample& sample) const {
    std::ostringstream oss;
    oss << "自我反思：本轮得分 " << std::fixed << std::setprecision(2)
        << sample.score << "；策略 " << cognition_.active_strategy
        << "；置信 " << cognition_.confidence
        << "；矛盾 " << cognition_.contradiction
        << "。";
    if (sample.score < 0.48f) {
        oss << "下一次应先复述议题，再给出一个明确主张和一个可检验理由。";
    } else if (cognition_.contradiction > 0.45f) {
        oss << "下一次需要先处理现有信念的冲突，再扩展结论。";
    } else if (cognition_.uncertainty > 0.55f) {
        oss << "下一次应增加针对目标 Agent 的具体回应，减少泛化陈述。";
    } else {
        oss << "保留当前策略，并用记忆中的反例检验结论。";
    }
    return oss.str();
}

void LLMAgent::update_regions(const TrainingExample& sample, float novelty,
                              float retrieval_relevance) {
    const float error = 1.0f - sample.score;
    const float previous = cognition_.belief.energy();
    const Vector response_embedding = semantic_embedding(sample.response);
    const float revision_pressure = std::clamp(
        std::max(0.0f, 0.55f - sample.score) * 1.2f +
            std::abs(sample.score - retrieval_relevance) * 0.25f,
        0.0f, 1.0f);

    cognition_.confidence = std::clamp(
        cognition_.confidence * 0.82f + sample.score * 0.18f, 0.0f, 1.0f);
    cognition_.uncertainty = std::clamp(
        cognition_.uncertainty * 0.80f + error * 0.20f, 0.0f, 1.0f);
    cognition_.coherence = std::clamp(
        cognition_.coherence * 0.84f +
            (sample.score * 0.65f + retrieval_relevance * 0.35f) * 0.16f,
        0.0f, 1.0f);
    cognition_.contradiction = std::clamp(
        cognition_.contradiction * 0.78f + revision_pressure * 0.22f,
        0.0f, 1.0f);
    cognition_.curiosity = std::clamp(
        cognition_.curiosity * 0.85f + novelty * 0.15f, 0.0f, 1.0f);

    const float belief_rate = 0.04f + 0.10f * sample.score;
    for (std::size_t i = 0; i < DIM; ++i) {
        cognition_.belief[i] +=
            belief_rate * (response_embedding[i] - cognition_.belief[i]);
    }
    ++cognition_.revisions;

    if (cognition_.uncertainty > 0.58f)
        cognition_.active_strategy = "targeted_exploration";
    else if (cognition_.contradiction > 0.42f)
        cognition_.active_strategy = "reflective_revision";
    else if (novelty > 0.65f)
        cognition_.active_strategy = "curiosity_guided_probe";
    else
        cognition_.active_strategy = "stable_exploitation";

    consciousness_.prediction_error = std::clamp(
        consciousness_.prediction_error * 0.80f + error * 0.20f,
        0.0f, 1.0f);
    consciousness_.attention = std::clamp(
        consciousness_.attention * 0.75f + retrieval_relevance * 0.25f,
        0.0f, 1.0f);
    consciousness_.goal_alignment = std::clamp(
        consciousness_.goal_alignment * 0.82f +
            (sample.score * 0.6f + retrieval_relevance * 0.4f) * 0.18f,
        0.0f, 1.0f);
    consciousness_.integrity = std::clamp(
        1.0f - cognition_.contradiction * 0.62f -
            consciousness_.prediction_error * 0.28f,
        0.0f, 1.0f);
    consciousness_.self_model_confidence = std::clamp(
        cognition_.confidence * 0.50f +
            cognition_.coherence * 0.30f +
            (1.0f - cognition_.uncertainty) * 0.20f,
        0.0f, 1.0f);
    ++consciousness_.reflections;

    std::ostringstream model;
    model << factor_name_ << "；策略=" << cognition_.active_strategy
          << "；最近反思=" << sample.feedback;
    consciousness_.self_model = model.str();

    if (sample.source.find("catastrophe") != std::string::npos) {
        consciousness_.current_goal =
            "preserve the coreflame and reconstruct the world after catastrophe";
    } else {
        consciousness_.current_goal =
            "understand the first cause of life through " + simulated_path_;
    }

    if (previous < 1e-8f) cognition_.belief = response_embedding;
}

SelfTrainingUpdate LLMAgent::self_train(const std::string& prompt,
                                        const std::string& response,
                                        int round_no,
                                        const std::string& source,
                                        float external_reward) {
    SelfTrainingUpdate update;
    const Vector query_embedding = semantic_embedding(prompt);

    float max_similarity = 0.0f;
    for (const auto& memory : memories_) {
        max_similarity = std::max(
            max_similarity,
            semantic_similarity(query_embedding, memory.embedding));
    }
    update.retrieval_relevance = std::clamp(max_similarity, 0.0f, 1.0f);
    update.novelty = std::clamp(1.0f - max_similarity, 0.0f, 1.0f);

    float score = evaluate_response(response);
    if (external_reward >= 0.0f) {
        score = std::clamp(
            score * 0.65f + std::clamp(external_reward, 0.0f, 1.0f) * 0.35f,
            0.0f, 1.0f);
    }
    update.score = score;

    TrainingExample sample;
    sample.local_id = next_training_id_++;
    sample.round_no = round_no;
    sample.source = source;
    sample.instruction =
        "Stay in character as factor " + factor_name_ +
        " and answer the user with a concrete first-cause argument.";
    sample.prompt = prompt;
    sample.response = response;
    sample.score = score;

    const std::string feedback = reflect(sample);
    sample.feedback = feedback;
    sample.accepted =
        self_training_enabled_ && score >= training_threshold_ &&
        source.find("fallback") == std::string::npos &&
        source.find("internal_state") == std::string::npos;

    remember("议题：" + prompt + "\n回应：" + response,
             MemoryKind::Episode, 0.30f + score * 0.55f,
             round_no, source);
    remember(feedback, MemoryKind::Reflection,
             0.55f + (1.0f - score) * 0.30f, round_no, source);

    update_regions(sample, update.novelty, update.retrieval_relevance);
    training_score_sum_ += score;
    if (sample.accepted) ++accepted_training_samples_;

    training_samples_.push_back(sample);
    if (training_samples_.size() > 512) {
        auto rejected = std::find_if(
            training_samples_.begin(), training_samples_.end(),
            [](const TrainingExample& item) { return !item.accepted; });
        if (rejected != training_samples_.end())
            training_samples_.erase(rejected);
        else
            training_samples_.erase(training_samples_.begin());
    }
    update.accepted = sample.accepted;
    update.sample = std::move(sample);
    return update;
}

void LLMAgent::prune_memories() {
    constexpr std::size_t kMemoryCapacity = 96;
    while (memories_.size() > kMemoryCapacity) {
        auto worst = memories_.begin();
        float worst_score = std::numeric_limits<float>::max();
        for (auto it = memories_.begin(); it != memories_.end(); ++it) {
            const float score = it->retention * 0.70f +
                                it->importance * 0.20f +
                                std::min(1.0f, it->recall_count * 0.02f) * 0.10f;
            if (score < worst_score) {
                worst_score = score;
                worst = it;
            }
        }
        memories_.erase(worst);
    }
}

void LLMAgent::consolidate_memory(int round_no) {
    for (auto& memory : memories_) {
        const int age = std::max(0, round_no - memory.last_access_round);
        const float recency = 1.0f / (1.0f + 0.18f * static_cast<float>(age));
        const float recall_boost =
            std::min(1.0f, static_cast<float>(memory.recall_count) * 0.06f);
        memory.retention = std::clamp(
            memory.importance * 0.55f + recency * 0.28f +
                recall_boost * 0.17f,
            0.0f, 1.0f);
        if (memory.retention >= 0.62f ||
            (memory.importance >= 0.72f && memory.recall_count > 0)) {
            memory.consolidated = true;
            memory.importance = std::min(1.0f, memory.importance + 0.01f);
        }
    }

    std::erase_if(memories_, [round_no](const MemoryEntry& memory) {
        const int age = std::max(0, round_no - memory.last_access_round);
        return age >= 3 && memory.retention < 0.08f &&
               memory.importance < 0.20f;
    });
    prune_memories();

    const auto replay = recall(first_cause_, 3);
    if (!replay.empty()) {
        for (const auto* memory : replay) {
            for (std::size_t i = 0; i < DIM; ++i) {
                memory_[i] += 0.04f * (memory->embedding[i] - memory_[i]);
            }
        }
    }
    cognition_.coherence = std::clamp(
        cognition_.coherence + 0.01f, 0.0f, 1.0f);
}

void LLMAgent::inherit_from(const LLMAgent& predecessor,
                            float inheritance_ratio) {
    const float ratio = std::clamp(inheritance_ratio, 0.0f, 1.0f);
    std::vector<const MemoryEntry*> inherited;
    inherited.reserve(predecessor.memories_.size());
    for (const auto& memory : predecessor.memories_)
        inherited.push_back(&memory);
    std::sort(inherited.begin(), inherited.end(),
              [](const MemoryEntry* left, const MemoryEntry* right) {
                  return left->importance > right->importance;
              });

    const std::size_t count = std::min<std::size_t>(8, inherited.size());
    for (std::size_t i = 0; i < count; ++i) {
        remember("Inherited from " + predecessor.factor_name_ + ": " +
                     inherited[i]->content,
                 MemoryKind::Inherited,
                 inherited[i]->importance * ratio,
                 inherited[i]->last_access_round,
                 "inheritance:" + predecessor.factor_name_);
    }

    for (std::size_t i = 0; i < DIM; ++i) {
        cognition_.belief[i] +=
            ratio * 0.20f * (predecessor.cognition_.belief[i] -
                             cognition_.belief[i]);
    }
    cognition_.confidence = std::clamp(
        cognition_.confidence * (1.0f - ratio * 0.15f) +
            predecessor.cognition_.confidence * ratio * 0.15f,
        0.0f, 1.0f);
    cognition_.uncertainty = std::clamp(
        cognition_.uncertainty + ratio * 0.04f, 0.0f, 1.0f);
    cognition_.active_strategy = "inherited_reflection";
    consciousness_.attention = std::clamp(
        consciousness_.attention + ratio * 0.08f, 0.0f, 1.0f);
    consciousness_.integrity = std::clamp(
        consciousness_.integrity * (1.0f - ratio * 0.10f) +
            predecessor.consciousness_.integrity * ratio * 0.10f,
        0.0f, 1.0f);
    consciousness_.self_model =
        consciousness_.self_model + " | inherited coreflame from " +
        predecessor.factor_name_;
    remember("Coreflame memory transfer from " + predecessor.factor_name_ +
                 "; predecessor goal: " +
                 predecessor.consciousness_.current_goal,
             MemoryKind::Inherited, 0.78f * ratio,
             predecessor.memories_.empty()
                 ? 0 : predecessor.memories_.back().last_access_round,
             "coreflame_transfer");
}

MindSnapshot LLMAgent::mind_snapshot() const {
    MindSnapshot snapshot;
    snapshot.memory_count = memories_.size();
    snapshot.training_samples = training_samples_.size();
    snapshot.accepted_samples = accepted_training_samples_;
    snapshot.average_training_score =
        training_samples_.empty()
            ? 0.0f
            : training_score_sum_ /
                  static_cast<float>(training_samples_.size());
    snapshot.efficiency = efficiency_;
    snapshot.cognition = cognition_;
    snapshot.consciousness = consciousness_;
    return snapshot;
}

std::size_t LLMAgent::export_training_jsonl(const std::string& path,
                                            std::int64_t run_id,
                                            bool append) const {
    if (path.empty()) return 0;
    std::error_code error;
    const std::filesystem::path output(path);
    if (output.has_parent_path()) {
        std::filesystem::create_directories(output.parent_path(), error);
    }

    std::ofstream file(path,
                       std::ios::out | (append ? std::ios::app : std::ios::trunc));
    if (!file) return 0;

    std::size_t written = 0;
    for (const auto& sample : training_samples_) {
        if (!sample.accepted) continue;
        file << "{\"id\":\"" << escape_json(factor_name_) << "-"
             << sample.local_id << "\""
             << ",\"run_id\":" << run_id
             << ",\"agent_index\":" << static_cast<int>(id_)
             << ",\"factor\":\"" << escape_json(factor_name_) << "\""
             << ",\"round_no\":" << sample.round_no
             << ",\"source\":\"" << escape_json(sample.source) << "\""
             << ",\"instruction\":\"" << escape_json(sample.instruction) << "\""
             << ",\"input\":\"" << escape_json(sample.prompt) << "\""
             << ",\"output\":\"" << escape_json(sample.response) << "\""
             << ",\"score\":" << std::fixed << std::setprecision(6)
             << sample.score
             << ",\"feedback\":\"" << escape_json(sample.feedback) << "\""
             << ",\"accepted\":true}\n";
        ++written;
    }
    return written;
}

void LLMAgent::multi_head_attention(const Vector& hidden,
                                    TransformerBlock& block, Vector& out) {
    constexpr std::size_t H = 4;
    constexpr std::size_t Dh = DIM / H;

    Vector Q, K, V;
    for (std::size_t i = 0; i < DIM; ++i) {
        float q = 0.0f, k = 0.0f, v = 0.0f;
        const float* row_q = &block.Wq[i * DIM];
        const float* row_k = &block.Wk[i * DIM];
        const float* row_v = &block.Wv[i * DIM];
        for (std::size_t j = 0; j < DIM; ++j) {
            q += row_q[j] * hidden[j];
            k += row_k[j] * hidden[j];
            v += row_v[j] * hidden[j];
        }
        Q[i] = q; K[i] = k; V[i] = v;
    }

    // 单 token 自注意力退化为门控
    Vector attn;
    float scale = 1.0f / std::sqrt(static_cast<float>(Dh));
    for (std::size_t h = 0; h < H; ++h) {
        float score = 0.0f;
        for (std::size_t d = 0; d < Dh; ++d)
            score += Q[h * Dh + d] * K[h * Dh + d];
        score *= scale;
        float w = 1.0f / (1.0f + std::exp(-score));
        for (std::size_t d = 0; d < Dh; ++d)
            attn[h * Dh + d] = w * V[h * Dh + d];
    }

    for (std::size_t i = 0; i < DIM; ++i) {
        float s = 0.0f;
        const float* row = &block.Wo[i * DIM];
        for (std::size_t j = 0; j < DIM; ++j) s += row[j] * attn[j];
        out[i] = s;
    }
}

// ★ 真 FFN（每行独立权重）
void LLMAgent::ffn(const Vector& x, TransformerBlock& block, Vector& out) {
    constexpr std::size_t H = DIM * 2;

    std::array<float, H> hidden;
    for (std::size_t i = 0; i < H; ++i) {
        float s = block.ff_b1[i];
        const float* row = &block.ff_w1[i * DIM];
        for (std::size_t j = 0; j < DIM; ++j)
            s += row[j] * x[j];
        hidden[i] = std::tanh(s);
    }

    for (std::size_t i = 0; i < DIM; ++i) {
        float s = block.ff_b2[i];
        const float* row = &block.ff_w2[i * H];
        for (std::size_t j = 0; j < H; ++j)
            s += row[j] * hidden[j];
        out[i] = s;
    }
}

void LLMAgent::forward_cpu(const Vector& input, Vector& output) {
    Vector hidden = input;
    for (auto& block : blocks_) {
        Vector attn_out;
        multi_head_attention(hidden, block, attn_out);
        for (std::size_t i = 0; i < DIM; ++i)
            hidden[i] = hidden[i] + attn_out[i];
        layer_norm(hidden, block.ln1_w, block.ln1_b);

        Vector ff_out;
        ffn(hidden, block, ff_out);
        for (std::size_t i = 0; i < DIM; ++i)
            hidden[i] = hidden[i] + ff_out[i];
        layer_norm(hidden, block.ln2_w, block.ln2_b);
    }
    output = hidden;
}

Vector LLMAgent::predict_next(const Vector& context) {
    Vector out;
    forward_cpu(context, out);
    context_ = out;
    return out;
}

bool LLMAgent::tokenize_checked(const std::string& text,
                                std::vector<std::int32_t>& out_tokens) {
#ifdef CIV_USE_LLAMA
    auto& pool = SharedModelPool::instance();
    if (!pool.is_loaded()) return false;

    const llama_vocab* vocab = pool.vocab();
    if (!vocab) return false;

    if (text.size() > static_cast<std::size_t>(
            std::numeric_limits<int>::max() - 16)) return false;
    int n_max = static_cast<int>(text.size()) + 16;
    if (n_max < 16) n_max = 16;
    out_tokens.resize(n_max);

    int n = llama_tokenize(vocab, text.c_str(),
                           static_cast<int>(text.size()),
                           out_tokens.data(), n_max, true, true);

    if (n == std::numeric_limits<int>::min()) return false;
    if (n < 0) {
        out_tokens.resize(-n);
        n = llama_tokenize(vocab, text.c_str(),
                           static_cast<int>(text.size()),
                           out_tokens.data(),
                           static_cast<int>(out_tokens.size()),
                           true, true);
        if (n < 0) return false;
    }
    if (n <= 0) return false;

    out_tokens.resize(n);
    return true;
#else
    (void)text; (void)out_tokens;
    return false;
#endif
}

// 修复缓冲区不足
std::string LLMAgent::token_to_text(std::int32_t token) {
#ifdef CIV_USE_LLAMA
    const llama_vocab* vocab = SharedModelPool::instance().vocab();
    if (!vocab) return {};

    char buf[256];
    int n = llama_token_to_piece(vocab, token, buf, sizeof(buf), 0, false);

    if (n == std::numeric_limits<int>::min()) return {};
    if (n < 0) {
        std::string big(static_cast<std::size_t>(-n), '\0');
        int m = llama_token_to_piece(vocab, token, big.data(),
                                     static_cast<int>(big.size()), 0, false);
        if (m > 0) { big.resize(static_cast<std::size_t>(m)); return big; }
        return {};
    }
    if (n > 0) return std::string(buf, static_cast<std::size_t>(n));
    return {};
#else
    (void)token;
    return {};
#endif
}

// 滑动窗口 KV 重建
bool LLMAgent::warmup_kv_from_history() {
#ifdef CIV_USE_LLAMA
    auto& pool = SharedModelPool::instance();
    if (!pool.is_loaded() || state_.seq_id < 0) return false;

    pool.reset_sequence(state_.seq_id);
    state_.n_past = 0;

    if (state_.history.empty()) {
        state_.last_logits.clear();
        return true;
    }

    std::size_t keep = std::min<std::size_t>(
        AgentState::WARMUP_TOKENS, state_.history.size());
    keep = std::min<std::size_t>(
        keep, static_cast<std::size_t>(
                  std::max(1, pool.n_ctx_per_seq() - 8)));
    std::vector<std::int32_t> recent(
        state_.history.end() - static_cast<std::ptrdiff_t>(keep),
        state_.history.end());

    std::vector<DecodeRequest> warmup;
    warmup.reserve(recent.size());
    for (std::size_t i = 0; i < recent.size(); ++i) {
        DecodeRequest r;
        r.seq_id = state_.seq_id;
        r.token  = recent[i];
        r.pos    = static_cast<int>(i);
        r.want_logits = (i + 1 == recent.size());
        warmup.push_back(r);
    }
    std::vector<float> logits;
    if (!pool.batch_decode(warmup, logits)) return false;

    state_.n_past = static_cast<int>(recent.size());
    state_.last_logits = std::move(logits);
    return true;
#else
    return false;
#endif
}

bool LLMAgent::maybe_rebuild_kv(int needed_tokens) {
#ifdef CIV_USE_LLAMA
    auto& pool = SharedModelPool::instance();
    if (!pool.is_loaded()) return false;

    int safe_limit = n_ctx_hint_ - 64;
    if (state_.n_past + needed_tokens < safe_limit) return false;

    std::cerr << "[Agent " << id_ << "] KV cache 重建 (n_past="
              << state_.n_past << ", n_ctx=" << n_ctx_hint_ << ")\n";
    return warmup_kv_from_history();
#else
    (void)needed_tokens;
    return false;
#endif
}

void LLMAgent::begin_generation(const std::string& prompt, int max_tokens,
                                int n_ctx) {
    gen_result_.clear();
    gen_remaining_ = 0;
    n_ctx_hint_ = n_ctx;

#ifdef CIV_USE_LLAMA
    auto& pool = SharedModelPool::instance();
    if (!pool.is_loaded() || state_.seq_id < 0) return;

    std::vector<std::int32_t> tokens;
    if (!tokenize_checked(prompt, tokens)) return;

    const int per_seq = pool.n_ctx_per_seq();
    if (per_seq <= 2) return;
    n_ctx_hint_ = per_seq;

    // 提示词加上生成长度必须落在单个序列的上下文内。
    int gen_budget = std::min(max_tokens, per_seq - 2);
    if (gen_budget < 0) gen_budget = 0;
    const int reserve = gen_budget + 1;   // 至少为最后一个 logits 留位置

    if (tokens.size() + static_cast<std::size_t>(reserve) >
        static_cast<std::size_t>(per_seq)) {
        const std::size_t keep = static_cast<std::size_t>(
            std::max(1, per_seq - reserve));
        if (tokens.size() > keep) {
            std::cerr << "[Agent " << id_ << "] 提示词过长，截断 "
                      << (tokens.size() - keep) << " tokens\n";
            tokens.erase(tokens.begin(),
                         tokens.end() - static_cast<std::ptrdiff_t>(keep));
        }
    }

    maybe_rebuild_kv(static_cast<int>(tokens.size()) + reserve);

    // llama.cpp 的 KV 位置是权威状态。若本地计数因 EOS/重建/异常路径发生漂移，
    // 先与 KV cache 对齐，避免出现 “Y = X + 1” 的位置不连续错误。
    const int kv_pos = pool.sequence_kv_usage(state_.seq_id);
    if (kv_pos != state_.n_past) {
        std::cerr << "[Agent " << id_ << "] KV 位置校正: "
                  << state_.n_past << " -> " << kv_pos << "\n";
        state_.n_past = kv_pos;
    }

    // 滑动窗口重建后仍放不下时，清空 KV，让长提示词从位置 0 重新开始。
    if (state_.n_past + static_cast<int>(tokens.size()) + reserve >
        per_seq) {
        pool.reset_sequence(state_.seq_id);
        state_.n_past = 0;
        state_.last_logits.clear();
    }

    std::vector<DecodeRequest> reqs;
    reqs.reserve(tokens.size());
    for (std::size_t i = 0; i < tokens.size(); ++i) {
        DecodeRequest r;
        r.seq_id = state_.seq_id;
        r.token  = tokens[i];
        r.pos    = state_.n_past + static_cast<int>(i);
        r.want_logits = (i + 1 == tokens.size());
        reqs.push_back(r);
    }
    std::vector<float> logits;
    if (!pool.batch_decode(reqs, logits)) return;

    for (auto t : tokens) {
        state_.history.push_back(t);
        while (state_.history.size() > AgentState::MAX_HISTORY)
            state_.history.pop_front();
    }

    state_.n_past += static_cast<int>(tokens.size());
    state_.last_logits = std::move(logits);

    gen_remaining_ = gen_budget;
    gen_result_.reserve(static_cast<std::size_t>(gen_budget) * 4);
#endif
}

std::int32_t LLMAgent::sample_next_token() {
#ifdef CIV_USE_LLAMA
    auto& pool = SharedModelPool::instance();
    if (!pool.is_loaded() || state_.last_logits.empty() ||
        gen_remaining_ <= 0) return -1;

    int n_vocab = pool.n_vocab();
    const llama_vocab* vocab = pool.vocab();
    if (n_vocab <= 0 || !vocab) return -1;
    if (state_.last_logits.size() <
        static_cast<std::size_t>(n_vocab)) return -1;

    std::unordered_set<std::int32_t> recent;
    const std::size_t recent_begin =
        state_.history.size() > 64 ? state_.history.size() - 64 : 0;
    for (std::size_t i = recent_begin; i < state_.history.size(); ++i)
        recent.insert(state_.history[i]);

    std::vector<std::pair<float, int>> candidates;
    candidates.reserve(static_cast<std::size_t>(n_vocab));
    for (int i = 0; i < n_vocab; ++i) {
        float logit = state_.last_logits[static_cast<std::size_t>(i)];
        if (!std::isfinite(logit)) continue;
        if (recent.count(i)) {
            logit = logit > 0.0f ? logit / repeat_penalty_
                                 : logit * repeat_penalty_;
        }
        candidates.emplace_back(logit, i);
    }
    if (candidates.empty()) return -1;

    int keep = top_k_ > 0
                   ? std::min<int>(top_k_, static_cast<int>(candidates.size()))
                   : static_cast<int>(candidates.size());
    const auto by_logit = [](const auto& a, const auto& b) {
        return a.first > b.first;
    };
    if (keep < static_cast<int>(candidates.size()))
        std::partial_sort(candidates.begin(),
                          candidates.begin() + keep,
                          candidates.end(), by_logit);
    else
        std::sort(candidates.begin(), candidates.end(), by_logit);
    candidates.resize(static_cast<std::size_t>(keep));

    const float max_logit = candidates.front().first;
    std::vector<float> probs(candidates.size(), 0.0f);
    float sum = 0.0f;
    for (std::size_t i = 0; i < candidates.size(); ++i) {
        probs[i] = std::exp((candidates[i].first - max_logit) / temperature_);
        sum += probs[i];
    }
    if (!(sum > 0.0f) || !std::isfinite(sum)) return -1;
    for (float& p : probs) p /= sum;

    // Nucleus truncation, then renormalize.
    float cumulative = 0.0f;
    std::size_t nucleus = probs.size();
    for (std::size_t i = 0; i < probs.size(); ++i) {
        cumulative += probs[i];
        if (cumulative >= top_p_) {
            nucleus = i + 1;
            break;
        }
    }
    probs.resize(nucleus);
    sum = 0.0f;
    for (float p : probs) sum += p;
    if (!(sum > 0.0f) || !std::isfinite(sum)) return -1;
    for (float& p : probs) p /= sum;

    QuantumURBG urbg(rng_);
    std::discrete_distribution<int> dist(probs.begin(), probs.end());
    std::int32_t next = static_cast<std::int32_t>(
        candidates[static_cast<std::size_t>(dist(urbg))].second);

    const llama_token eos = llama_vocab_eos(vocab);
    const llama_token eot = llama_vocab_eot(vocab);
    if (next == eos || (eot >= 0 && next == eot)) {
        gen_remaining_ = 0;
        state_.last_logits.clear();
        // EOS 时清理序列
        pool.reset_sequence(state_.seq_id);
        state_.n_past = 0;
        return -1;
    }

    state_.history.push_back(next);
    while (state_.history.size() > AgentState::MAX_HISTORY)
        state_.history.pop_front();

    gen_result_ += token_to_text(next);

    --gen_remaining_;
    if (gen_remaining_ <= 0) {
        state_.last_logits.clear();
        return -1;
    }

    return next;
#else
    return -1;
#endif
}

void LLMAgent::set_logits(std::vector<float>&& logits) {
    state_.last_logits = std::move(logits);
}

std::string LLMAgent::take_result() {
    return std::move(gen_result_);
}

std::string LLMAgent::generate(const std::string& prompt, int max_tokens) {
#ifdef CIV_USE_LLAMA
    if (SharedModelPool::available() && state_.seq_id >= 0) {
        auto& pool = SharedModelPool::instance();
        begin_generation(prompt, max_tokens, n_ctx_hint_);

        while (gen_remaining_ > 0) {
            std::int32_t next = sample_next_token();
            if (next < 0) break;

            DecodeRequest r;
            r.seq_id = state_.seq_id;
            r.token  = next;
            const int kv_pos = pool.sequence_kv_usage(state_.seq_id);
            if (kv_pos != state_.n_past) state_.n_past = kv_pos;
            r.pos    = state_.n_past++;
            r.want_logits = true;

            std::vector<float> logits;
            if (!pool.batch_decode({r}, logits)) break;
            state_.last_logits = std::move(logits);
        }
        return take_result();
    }
#endif

    if (prompt.find("第一因") != std::string::npos) {
        return answer_first_cause();
    }

    Vector ctx = encode(prompt);
    std::ostringstream oss;
    for (int i = 0; i < max_tokens; ++i) {
        Vector next = predict_next(ctx);
        ctx = next;
        float v = ctx[0];
        if (v > 0.85f) oss << "必然";
        else if (v > 0.7f) oss << "可能";
        else if (v > 0.5f) oss << "或许";
        else if (v > 0.3f) oss << "不太";
        else oss << "绝不";
        if (i < max_tokens - 1) oss << " ";
    }
    return oss.str();
}

std::string LLMAgent::generate_chat(const std::string& system,
                                    const std::string& user,
                                    int max_tokens) {
    if (SharedModelPool::available()) {
        auto& pool = SharedModelPool::instance();
        return generate(pool.format_chat(system, user), max_tokens);
    }
    return generate(user, max_tokens);
}

void LLMAgent::reset_dialogue_context() {
    if (state_.seq_id >= 0 && SharedModelPool::available()) {
        SharedModelPool::instance().reset_sequence(state_.seq_id);
    }
    state_.n_past = 0;
    state_.history.clear();
    state_.last_logits.clear();
}

void LLMAgent::learn(const Vector& exp, float lr) {
    float loss = 0.0f;
    for (std::size_t i = 0; i < DIM; ++i) {
        float d = embedding_[i] - exp[i];
        loss += d * d;
    }
    loss /= DIM;
    for (std::size_t i = 0; i < DIM; ++i)
        embedding_[i] -= lr * (embedding_[i] - exp[i]);
    efficiency_ += (0.5f - loss) * 0.1f;
    efficiency_ = std::clamp(efficiency_, 0.01f, 10.0f);
}

void LLMAgent::evolve(const Vector& experience, const Vector& collective,
                      const Vector& rival, float lr) {
    lr = std::clamp(lr, 0.0f, 0.5f);

    auto mix = [&](const Vector& target, float rate) {
        rate = std::clamp(rate, 0.0f, 1.0f);
        for (std::size_t i = 0; i < DIM; ++i)
            embedding_[i] += rate * (target[i] - embedding_[i]);
    };
    auto add_noise = [&](float amplitude) {
        for (std::size_t i = 0; i < DIM; ++i)
            embedding_[i] += (rng_.next_float() * 2.0f - 1.0f) * amplitude;
    };
    auto clamp_embedding = [&](float limit) {
        for (std::size_t i = 0; i < DIM; ++i)
            embedding_[i] = std::clamp(embedding_[i], -limit, limit);
    };

    switch (evolution_style_) {
    case EvolutionStyle::Consensus:
        // 同谐：向群体共识靠拢，但不抹平个体差异。
        mix(collective, 0.20f + lr);
        efficiency_ += 0.06f *
            (1.0f - std::min(1.0f, embedding_.l1_error(collective)));
        break;

    case EvolutionStyle::Constraint:
        // 秩序：在有界可行域内做梯度式更新。
        mix(experience, 1.5f * lr);
        clamp_embedding(0.75f);
        efficiency_ += 0.04f;
        break;

    case EvolutionStyle::Replay:
        // 记忆：历史经验回放 + 新经验写入。
        mix(memory_, 0.15f + lr);
        mix(experience, lr);
        efficiency_ += 0.05f *
            (1.0f - std::min(1.0f, embedding_.l1_error(memory_)));
        break;

    case EvolutionStyle::Selection: {
        // 不朽：先变异，再按适应度决定是否保留。
        const float err_before = embedding_.l1_error(experience);
        add_noise(0.02f + lr);
        const float err_after = embedding_.l1_error(experience);
        if (err_after < err_before) {
            efficiency_ += 0.10f;
        } else {
            mix(experience, 0.5f * lr);
            efficiency_ += 0.02f;
        }
        break;
    }

    case EvolutionStyle::Adversarial:
        // 虚无：沿相反方向更新，主动制造认知裂缝。
        for (std::size_t i = 0; i < DIM; ++i)
            embedding_[i] -= (0.5f * lr) * (experience[i] - embedding_[i]);
        clamp_embedding(1.5f);
        efficiency_ += 0.04f + 0.03f * rng_.next_float();
        break;

    case EvolutionStyle::Distillation:
        // 存护：把集体表征蒸馏进自身。
        mix(collective, 0.35f + lr);
        efficiency_ += 0.06f;
        break;

    case EvolutionStyle::Reward:
        // 毁灭：放大高奖励方向，快速改变策略。
        mix(experience, 0.5f + 2.0f * lr);
        efficiency_ += 0.07f + 0.05f * rng_.next_float();
        break;

    case EvolutionStyle::Critique: {
        // 智识：只修正误差最大的维度，相当于主动学习。
        std::size_t worst = 0;
        float max_err = 0.0f;
        for (std::size_t i = 0; i < DIM; ++i) {
            const float e = std::abs(embedding_[i] - experience[i]);
            if (e > max_err) { max_err = e; worst = i; }
        }
        embedding_[worst] +=
            0.5f * (experience[worst] - embedding_[worst]);
        efficiency_ += 0.05f;
        break;
    }

    case EvolutionStyle::Regularization:
        // 纯美：衰减 + 小步学习，抑制过拟合。
        for (std::size_t i = 0; i < DIM; ++i)
            embedding_[i] *= (1.0f - 0.25f * lr);
        mix(experience, 0.25f * lr);
        efficiency_ += 0.03f;
        break;

    case EvolutionStyle::Competition:
        // 巡猎：远离最强对手，同时追逐新经验。
        for (std::size_t i = 0; i < DIM; ++i)
            embedding_[i] -= 0.25f * lr * (rival[i] - embedding_[i]);
        mix(experience, 0.6f * lr);
        clamp_embedding(1.5f);
        efficiency_ += 0.05f;
        break;

    case EvolutionStyle::Pruning:
        // 均衡：剪掉弱连接，保留稳定结构。
        for (std::size_t i = 0; i < DIM; ++i)
            if (std::abs(embedding_[i]) < 0.05f) embedding_[i] = 0.0f;
        mix(experience, 0.2f * lr);
        efficiency_ += 0.04f;
        break;

    case EvolutionStyle::Exploration:
        // 欢愉：高随机性探索，跳出局部最优。
        add_noise(0.02f + 0.05f * lr);
        mix(experience, 0.3f * lr);
        efficiency_ += 0.02f + 0.04f * rng_.next_float();
        break;

    case EvolutionStyle::Meta:
        // 开拓：融合共识、记忆与探索。
        mix(collective, 0.15f + 0.5f * lr);
        mix(memory_, 0.10f + 0.5f * lr);
        add_noise(0.01f);
        efficiency_ += 0.06f;
        break;
    }

    // 记忆巩固：慢速滑动平均，供下一次回放使用。
    for (std::size_t i = 0; i < DIM; ++i)
        memory_[i] += 0.2f * (embedding_[i] - memory_[i]);

    efficiency_ = std::clamp(efficiency_, 0.01f, 10.0f);
}

void LLMAgent::mutate(float rate) {
    for (auto& block : blocks_) {
        for (std::size_t i = 0; i < DIM * DIM; i += 8) {
            if (rng_.next_float() < 0.05f) {
                block.Wq[i] += (rng_.next_float() * 2 - 1) * rate;
                block.Wv[i] += (rng_.next_float() * 2 - 1) * rate;
            }
        }
        for (std::size_t i = 0; i < DIM * 2 * DIM; i += 13) {
            if (rng_.next_float() < 0.03f) {
                block.ff_w1[i] += (rng_.next_float() * 2 - 1) * rate;
            }
        }
    }
    efficiency_ += (rng_.next_float() - 0.5f) * 0.2f;
    efficiency_ = std::clamp(efficiency_, 0.01f, 10.0f);
    ++mutation_count_;
}

void LLMAgent::update_efficiency(float delta) {
    efficiency_ = std::max(0.01f, efficiency_ + delta);
}

void LLMAgent::set_worldview(const std::string& view) { worldview_ = view; }
std::string LLMAgent::express_first_cause() const { return first_cause_; }

std::string LLMAgent::answer_first_cause() const {
    return first_cause_ + " 我以" + evolution_method_ + "验证这一答案。";
}

} // namespace civ
