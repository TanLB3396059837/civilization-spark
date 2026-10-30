#include "civ/civilization/simulation.hpp"
#include "civ/codec/frame_v4.hpp"
#include "civ/ai/shared_model.hpp"
#include "civ/ai/batch_scheduler.hpp"
#ifdef CIV_HAS_POSTGRES
#include "civ/storage/postgres_store.hpp"
#endif
#include <iostream>
#include <iomanip>
#include <sstream>
#include <algorithm>
#include <chrono>
#include <cstdlib>
#include <cstdio>
#include <string_view>

namespace civ {

namespace {

constexpr std::array<std::string_view, 13> kTitanTrials{{
    "门径", "律法", "岁月", "大地", "海洋", "天空",
    "负世", "理性", "浪漫", "纷争", "死亡", "诡计", "全世"
}};

std::string titan_trial_list() {
    std::ostringstream oss;
    for (std::size_t i = 0; i < kTitanTrials.size(); ++i) {
        if (i) oss << "、";
        oss << kTitanTrials[i];
    }
    return oss.str();
}

std::string titan_trial_json() {
    std::ostringstream oss;
    oss << '[';
    for (std::size_t i = 0; i < kTitanTrials.size(); ++i) {
        if (i) oss << ',';
        oss << '"' << kTitanTrials[i] << '"';
    }
    oss << ']';
    return oss.str();
}

bool titan_resonates(std::string_view path, std::string_view trial) {
    return path == trial ||
           ((trial == "全世" || trial == "岁月") &&
            path.find(trial) != std::string_view::npos);
}

std::string json_escape(const std::string& text) {
    std::string out;
    out.reserve(text.size() + 8);
    for (char c : text) {
        switch (c) {
        case '"':  out += "\\\""; break;
        case '\\': out += "\\\\"; break;
        case '\n': out += "\\n";  break;
        case '\r': out += "\\r";  break;
        case '\t': out += "\\t";  break;
        default:   out += c;      break;
        }
    }
    return out;
}

std::string float_vector_json(const Vector& vector) {
    std::ostringstream oss;
    oss << '[' << std::setprecision(6);
    for (std::size_t i = 0; i < DIM; ++i) {
        if (i) oss << ',';
        oss << vector[i];
    }
    oss << ']';
    return oss.str();
}

std::string detect_quant(const std::string& path) {
    if (path.find("Q4_K_M") != std::string::npos) return "Q4_K_M";
    if (path.find("Q8_0") != std::string::npos) return "Q8_0";
    if (path.find("Q5_K_M") != std::string::npos) return "Q5_K_M";
    if (path.find("Q6_K") != std::string::npos) return "Q6_K";
    return "unknown";
}

std::string utf8_safe(const std::string& text);

bool usable_reply(const std::string& text) {
    const std::string clean = utf8_safe(text);
    if (clean.size() < 6) return false;
    static const char* const rejected[] = {
        "###", "【", "】", "关键要求", "要求：", "要求", "注：",
        "注意：", "严格遵循", "输出格式", "提示词", "请用两句话",
        "请输出", "不得出现",
    };
    for (const char* marker : rejected) {
        if (clean.find(marker) != std::string::npos) return false;
    }
    return clean.find("生命") != std::string::npos ||
           clean.find("第一因") != std::string::npos ||
           clean.find("同意") != std::string::npos ||
           clean.find("回应") != std::string::npos;
}

bool usable_catastrophe(const std::string& text) {
    const std::string clean = utf8_safe(text);
    if (clean.size() < 10) return false;
    static const char* const rejected[] = {
        "###", "【", "】", "要求", "提示词", "注：", "注意：",
        "严格遵循", "输出格式", "请用", "模型",
    };
    for (const char* marker : rejected) {
        if (clean.find(marker) != std::string::npos) return false;
    }
    static const char* const keywords[] = {
        "黑潮", "毁", "崩", "裂", "沉", "火", "时间", "规则",
        "记忆", "世界", "灾", "空", "败", "灭",
    };
    for (const char* keyword : keywords) {
        if (clean.find(keyword) != std::string::npos) return true;
    }
    return false;
}

std::string utf8_safe(const std::string& text) {
    std::string out;
    out.reserve(text.size());
    for (std::size_t i = 0; i < text.size();) {
        const unsigned char c = static_cast<unsigned char>(text[i]);
        std::size_t width = 0;
        if (c < 0x80) width = 1;
        else if ((c & 0xE0) == 0xC0) width = 2;
        else if ((c & 0xF0) == 0xE0) width = 3;
        else if ((c & 0xF8) == 0xF0) width = 4;
        if (width == 0 || i + width > text.size()) {
            ++i;
            continue;
        }
        bool valid = true;
        for (std::size_t j = 1; j < width; ++j) {
            if ((static_cast<unsigned char>(text[i + j]) & 0xC0) != 0x80) {
                valid = false;
                break;
            }
        }
        if (valid) {
            out.append(text, i, width);
            i += width;
        } else {
            ++i;
        }
    }
    return out;
}

} // namespace

Simulation::Simulation(std::uint64_t seed) {
    if (const char* mode = std::getenv("CIV_DEBATE_MODE")) {
        roundtable_mode_ = (std::string(mode) == "roundtable");
    }
    if (const char* mode = std::getenv("CIV_WORLD_MODE")) {
        const std::string value(mode);
        world_mode_ = (value == "amphoreus" || value == "genesis");
    }
    if (const char* path = std::getenv("CIV_TRAINING_EXPORT")) {
        if (*path) {
            export_training_ = true;
            training_export_path_ = path;
        }
    }
    if (const char* enabled = std::getenv("CIV_SELF_TRAIN"))
        self_training_enabled_ = std::atoi(enabled) != 0;
    if (const char* topic = std::getenv("CIV_ROUNDTABLE_TOPIC")) {
        if (*topic) conversation_topic_ = topic;
    }
    if (const char* rounds = std::getenv("CIV_MAX_ROUNDS")) {
        const int v = std::atoi(rounds);
        max_rounds_ = std::clamp(v, 1,
                                 static_cast<int>(MAX_ROUNDS));
    }

    if (const char* nctx = std::getenv("CIV_N_CTX")) {
        const int v = std::atoi(nctx);
        if (v >= 128) n_ctx_ = v;
    }

    const char* model_path = std::getenv("CIV_LLAMA_MODEL");
    int model_gpu_layers = 0;
    if (model_path) {
        int n_gpu_layers = 0;
#ifdef CIV_USE_CUDA
        n_gpu_layers = 99;
#endif
        if (const char* layers = std::getenv("CIV_N_GPU_LAYERS")) {
            n_gpu_layers = std::atoi(layers);
        }
        model_gpu_layers = n_gpu_layers;
        SharedModelPool::instance().load(model_path, n_ctx_, n_gpu_layers);
    }

    TransformerConfig cfg;
    for (std::size_t i = 0; i < MAX_AGENTS; ++i) {
        agents_[i] = std::make_unique<LLMAgent>(
            static_cast<AgentId>(i), cfg, seed);
        coreflame_[i] = 1.0f;
        corruption_[i] = 0.0f;
        memory_[i] = 0.0f;
    }

#ifdef CIV_HAS_POSTGRES
    const char* dsn = std::getenv("CIV_PG_DSN");
    if (dsn && *dsn) {
        try {
            store_ = new PostgresStore();
            if (!store_->connect(dsn)) {
                std::cerr << "[Postgres] 连接失败: " << store_->last_error()
                          << std::endl;
                delete store_;
                store_ = nullptr;
            } else {
                store_->migrate();

                RunRecord run;
                run.seed = seed;
                if (const char* source = std::getenv("CIV_SEED_SOURCE"))
                    run.seed_source = source;
                run.model_path = model_path ? model_path : "";
                if (const char* sha = std::getenv("CIV_MODEL_SHA256"))
                    run.model_sha256 = sha;
                run.quant = model_path ? detect_quant(model_path) : "none";
                run.n_ctx = n_ctx_;
                run.n_gpu_layers = model_gpu_layers;
                run.config_json =
                    "{\"agents\":" + std::to_string(MAX_AGENTS) +
                    ",\"rounds\":" + std::to_string(max_rounds_) +
                    ",\"debate_mode\":\"" +
                    (roundtable_mode_ ? "roundtable" : "parallel") + "\"" +
                    ",\"world_mode\":\"" +
                    (world_mode_ ? "amphoreus" : "standard") + "\"" +
                    ",\"n_ctx\":" + std::to_string(n_ctx_) +
                    ",\"self_training\":" +
                    (self_training_enabled_ ? "true" : "false") +
                    ",\"training_export\":" +
                    (export_training_ ? "true" : "false") + "}";
                if (const char* commit = std::getenv("CIV_GIT_COMMIT"))
                    run.git_commit = commit;

                run_id_ = store_->create_run(run);
                for (std::size_t i = 0; i < MAX_AGENTS; ++i) {
                    AgentRecord ar;
                    ar.agent_index = static_cast<int>(i);
                    ar.factor = agents_[i]->factor_name();
                    ar.heir = agents_[i]->heir_name();
                    ar.titan_path = agents_[i]->titan_path();
                    ar.drive = agents_[i]->drive();
                    ar.simulated_path = agents_[i]->simulated_path();
                    ar.ai_route = agents_[i]->ai_route();
                    ar.evolution_method = agents_[i]->evolution_method();
                    store_->add_agent(run_id_, ar);
                }
                if (roundtable_mode_) {
                    conversation_id_ = store_->create_conversation(
                        run_id_, conversation_topic_, "roundtable");
                }
                std::cout << "[Postgres] run_id=" << run_id_
                          << " 已连接并写入初始档案" << std::endl;
            }
        } catch (const std::exception& e) {
            std::cerr << "[Postgres] 初始化失败: " << e.what() << std::endl;
            delete store_;
            store_ = nullptr;
            run_id_ = -1;
        }
    }
#endif
}

Simulation::~Simulation() {
    shutdown();
}

void Simulation::shutdown() {
#ifdef CIV_HAS_POSTGRES
    delete store_;
    store_ = nullptr;
#endif
    for (auto& agent : agents_)
        agent.reset();
}

void Simulation::persist_event(int round_no, int agent_index,
                               const std::string& kind,
                               const std::string& payload_json) {
#ifdef CIV_HAS_POSTGRES
    if (!store_ || run_id_ < 0) return;
    try {
        EventRecord event;
        event.run_id = run_id_;
        event.round_no = round_no;
        event.agent_index = agent_index;
        event.kind = kind;
        event.payload_json = payload_json;
        store_->log_event(event);
    } catch (const std::exception& e) {
        std::cerr << "[Postgres] event 写入失败: " << e.what() << std::endl;
    }
#else
    (void)round_no; (void)agent_index; (void)kind; (void)payload_json;
#endif
}

void Simulation::persist_metric(int round_no, int agent_index,
                                const std::string& name, double value,
                                const std::string& unit,
                                const std::string& extra_json) {
#ifdef CIV_HAS_POSTGRES
    if (!store_ || run_id_ < 0) return;
    try {
        MetricRecord metric;
        metric.run_id = run_id_;
        metric.round_no = round_no;
        metric.agent_index = agent_index;
        metric.name = name;
        metric.value = value;
        metric.unit = unit;
        metric.extra_json = extra_json;
        store_->log_metric(metric);
    } catch (const std::exception& e) {
        std::cerr << "[Postgres] metric 写入失败: " << e.what() << std::endl;
    }
#else
    (void)round_no; (void)agent_index; (void)name;
    (void)value; (void)unit; (void)extra_json;
#endif
}

std::int64_t Simulation::persist_conversation_message(
    int round_no, int turn_no, int speaker_agent, int target_agent,
    std::int64_t reply_to_message_id, const std::string& phase,
    const std::string& content) {
#ifdef CIV_HAS_POSTGRES
    if (!store_ || conversation_id_ < 0) return -1;
    try {
        ConversationMessageRecord message;
        message.conversation_id = conversation_id_;
        message.round_no = round_no;
        message.turn_no = turn_no;
        message.speaker_agent = speaker_agent;
        message.target_agent = target_agent;
        message.reply_to_message_id = reply_to_message_id;
        message.phase = phase;
        message.content = content;
        return store_->add_conversation_message(message);
    } catch (const std::exception& e) {
        std::cerr << "[Postgres] conversation 写入失败: " << e.what()
                  << std::endl;
        return -1;
    }
#else
    (void)round_no; (void)turn_no; (void)speaker_agent;
    (void)target_agent; (void)reply_to_message_id;
    (void)phase; (void)content;
    return -1;
#endif
}

void Simulation::persist_mind_state(int round_no, int agent_index,
                                    bool include_training) {
#ifdef CIV_HAS_POSTGRES
    if (!store_ || run_id_ < 0 || agent_index < 0 ||
        agent_index >= static_cast<int>(MAX_AGENTS) ||
        !agents_[agent_index]) {
        return;
    }
    try {
        const LLMAgent& agent = *agents_[agent_index];
        for (const auto& memory : agent.memories()) {
            AgentMemoryRecord record;
            record.run_id = run_id_;
            record.agent_index = agent_index;
            record.round_no = memory.round_no;
            record.local_id = static_cast<std::int64_t>(memory.local_id);
            record.kind = memory.kind == MemoryKind::Episode ? "episode" :
                          memory.kind == MemoryKind::Feedback ? "feedback" :
                          memory.kind == MemoryKind::Reflection ? "reflection" :
                          memory.kind == MemoryKind::World ? "world" :
                          "inherited";
            record.source = memory.source;
            record.content = memory.content;
            record.importance = memory.importance;
            record.retention = memory.retention;
            record.recall_count = memory.recall_count;
            record.consolidated = memory.consolidated;
            record.embedding_json = float_vector_json(memory.embedding);
            record.metadata_json =
                "{\"last_access_round\":" +
                std::to_string(memory.last_access_round) + "}";
            store_->add_agent_memory(record);
        }

        AgentCognitionRecord cognition;
        cognition.run_id = run_id_;
        cognition.agent_index = agent_index;
        cognition.round_no = round_no;
        cognition.confidence = agent.cognition().confidence;
        cognition.coherence = agent.cognition().coherence;
        cognition.contradiction = agent.cognition().contradiction;
        cognition.uncertainty = agent.cognition().uncertainty;
        cognition.curiosity = agent.cognition().curiosity;
        cognition.active_strategy = agent.cognition().active_strategy;
        cognition.belief_json = float_vector_json(agent.cognition().belief);
        cognition.revisions = agent.cognition().revisions;
        store_->upsert_agent_cognition(cognition);

        AgentConsciousnessRecord consciousness;
        consciousness.run_id = run_id_;
        consciousness.agent_index = agent_index;
        consciousness.round_no = round_no;
        consciousness.self_model_confidence =
            agent.consciousness().self_model_confidence;
        consciousness.goal_alignment =
            agent.consciousness().goal_alignment;
        consciousness.attention = agent.consciousness().attention;
        consciousness.prediction_error =
            agent.consciousness().prediction_error;
        consciousness.integrity = agent.consciousness().integrity;
        consciousness.current_goal = agent.consciousness().current_goal;
        consciousness.self_model = agent.consciousness().self_model;
        consciousness.reflections = agent.consciousness().reflections;
        store_->upsert_agent_consciousness(consciousness);

        if (include_training) {
            for (const auto& sample : agent.training_samples()) {
                TrainingSampleRecord record;
                record.run_id = run_id_;
                record.agent_index = agent_index;
                record.round_no = sample.round_no;
                record.local_id =
                    static_cast<std::int64_t>(sample.local_id);
                record.source = sample.source;
                record.instruction = sample.instruction;
                record.prompt = sample.prompt;
                record.response = sample.response;
                record.score = sample.score;
                record.feedback = sample.feedback;
                record.accepted = sample.accepted;
                record.metadata_json =
                    std::string("{\"accepted\":") +
                    (sample.accepted ? "true" : "false") + "}";
                store_->upsert_training_sample(record);
            }
        }

        const MindSnapshot snapshot = agent.mind_snapshot();
        SelfTrainingRunRecord training;
        training.run_id = run_id_;
        training.agent_index = agent_index;
        training.round_no = round_no;
        training.samples_seen =
            static_cast<int>(snapshot.training_samples);
        training.samples_accepted =
            static_cast<int>(snapshot.accepted_samples);
        training.average_score = snapshot.average_training_score;
        training.memory_count = static_cast<int>(snapshot.memory_count);
        training.active_strategy = snapshot.cognition.active_strategy;
        training.status = "running";
        store_->upsert_self_training_run(training);
    } catch (const std::exception& e) {
        std::cerr << "[Postgres] mind state 写入失败: " << e.what()
                  << std::endl;
    }
#else
    (void)round_no;
    (void)agent_index;
    (void)include_training;
#endif
}

void Simulation::train_and_persist(int agent_index,
                                   const std::string& prompt,
                                   const std::string& response,
                                   const std::string& source,
                                   float external_reward) {
    if (agent_index < 0 ||
        agent_index >= static_cast<int>(MAX_AGENTS) ||
        !agents_[agent_index]) {
        return;
    }
    const auto update = agents_[agent_index]->self_train(
        prompt, response, static_cast<int>(round_), source,
        external_reward);
    persist_metric(static_cast<int>(round_), agent_index,
                   "self_training_score", update.score, "score");
    persist_metric(static_cast<int>(round_), agent_index,
                   "self_training_novelty", update.novelty, "ratio");
    persist_event(
        static_cast<int>(round_), agent_index, "self_training_update",
        "{\"source\":\"" + json_escape(source) +
            "\",\"score\":" + std::to_string(update.score) +
            ",\"novelty\":" + std::to_string(update.novelty) +
            ",\"accepted\":" + (update.accepted ? "true" : "false") + "}");
    persist_mind_state(static_cast<int>(round_), agent_index, true);
}

void Simulation::persist_all_minds(int round_no) {
    for (std::size_t i = 0; i < MAX_AGENTS; ++i) {
        if (agents_[i])
            persist_mind_state(round_no, static_cast<int>(i), true);
    }
}

void Simulation::export_self_training() {
    if (!export_training_) return;
    std::size_t written = 0;
    bool append = false;
    for (std::size_t i = 0; i < MAX_AGENTS; ++i) {
        if (!agents_[i]) continue;
        written += agents_[i]->export_training_jsonl(
            training_export_path_, run_id_, append);
        append = true;
    }
    std::cout << "[自我训练导出] " << training_export_path_
              << " | accepted samples=" << written << std::endl;
    persist_event(
        static_cast<int>(round_), -1, "self_training_export",
        "{\"path\":\"" + json_escape(training_export_path_) +
            "\",\"accepted_samples\":" + std::to_string(written) + "}");
}

void Simulation::print_mind_summary() const {
    std::cout << "[记忆区 / 认知区 / 意识区]" << std::endl;
    for (std::size_t i = 0; i < MAX_AGENTS; ++i) {
        if (!agents_[i]) continue;
        const MindSnapshot snapshot = agents_[i]->mind_snapshot();
        std::cout << "  Agent " << std::setw(2) << i << " "
                  << agents_[i]->factor_name()
                  << " | memory=" << snapshot.memory_count
                  << " | train=" << snapshot.accepted_samples << "/"
                  << snapshot.training_samples
                  << " | confidence=" << std::fixed << std::setprecision(2)
                  << snapshot.cognition.confidence
                  << " | coherence=" << snapshot.cognition.coherence
                  << " | uncertainty=" << snapshot.cognition.uncertainty
                  << " | self-model="
                  << snapshot.consciousness.self_model_confidence
                  << " | strategy=" << snapshot.cognition.active_strategy
                  << std::endl;
    }
}

AgentId Simulation::strongest_agent() const {
    AgentId best = 0;
    float max_eff = -1e9f;
    for (std::size_t i = 0; i < MAX_AGENTS; ++i) {
        if (agents_[i] && agents_[i]->efficiency() > max_eff) {
            max_eff = agents_[i]->efficiency();
            best = static_cast<AgentId>(i);
        }
    }
    return best;
}

AgentId Simulation::weakest_agent() const {
    AgentId worst = 0;
    float min_eff = 1e9f;
    for (std::size_t i = 0; i < MAX_AGENTS; ++i) {
        if (agents_[i] && agents_[i]->efficiency() < min_eff) {
            min_eff = agents_[i]->efficiency();
            worst = static_cast<AgentId>(i);
        }
    }
    return worst;
}

void Simulation::apply_titan_trial() {
    current_trial_ = "全部泰坦";
    const std::string trials = titan_trial_list();
    titan_trials_completed_ +=
        static_cast<int>(kTitanTrials.size());
    titan_resonance_.fill(0);

    std::cout << "[泰坦试炼] 本轮执行全部 13 泰坦："
              << trials << std::endl;
    persist_event(
        static_cast<int>(round_), -1, "titan_trial",
        "{\"mode\":\"all\",\"count\":13,\"trials\":" +
            titan_trial_json() + "}");

    std::vector<int> trial_resonances(kTitanTrials.size(), 0);
    int alive_count = 0;
    for (std::size_t i = 0; i < MAX_AGENTS; ++i) {
        if (!agents_[i]) continue;
        ++alive_count;
        const std::string& path = agents_[i]->titan_path();
        std::string resonant_paths_json = "[";
        int resonance_count = 0;
        for (std::size_t t = 0; t < kTitanTrials.size(); ++t) {
            const std::string_view trial = kTitanTrials[t];
            if (!titan_resonates(path, trial)) continue;
            if (resonance_count > 0) resonant_paths_json += ',';
            resonant_paths_json += "\"" + json_escape(std::string(trial)) + "\"";
            ++trial_resonances[t];
            ++resonance_count;
        }
        resonant_paths_json += ']';

        titan_resonance_[i] = resonance_count;
        agents_[i]->update_efficiency(
            0.01f + 0.005f * static_cast<float>(resonance_count));
        coreflame_[i] = std::min(
            1.5f, coreflame_[i] +
                      0.02f + 0.005f * static_cast<float>(resonance_count));
        memory_[i] = std::min(
            1.0f, memory_[i] +
                      0.03f + 0.01f * static_cast<float>(resonance_count));
        agents_[i]->remember(
            "Completed all 13 Titan trials; path " + path +
                " resonated with " + std::to_string(resonance_count) +
                " trial(s): " + resonant_paths_json,
            MemoryKind::World,
            0.58f + 0.04f * static_cast<float>(resonance_count),
            static_cast<int>(round_), "titan_gauntlet");

        persist_event(
            static_cast<int>(round_), static_cast<int>(i),
            "titan_gauntlet_result",
            "{\"factor\":\"" +
                json_escape(agents_[i]->factor_name()) +
                "\",\"trials_tested\":13,\"resonances\":" +
                std::to_string(resonance_count) +
                ",\"resonant_trials\":" + resonant_paths_json + "}");
        persist_metric(
            static_cast<int>(round_), static_cast<int>(i),
            "titan_trials_completed",
            static_cast<double>(kTitanTrials.size()), "trials");
        persist_metric(
            static_cast<int>(round_), static_cast<int>(i),
            "titan_resonance_count",
            static_cast<double>(resonance_count), "trials");
    }

    for (std::size_t t = 0; t < kTitanTrials.size(); ++t) {
        persist_event(
            static_cast<int>(round_), -1, "titan_trial_completed",
            "{\"trial\":\"" +
                json_escape(std::string(kTitanTrials[t])) +
                "\",\"participants\":" + std::to_string(alive_count) +
                ",\"resonances\":" +
                std::to_string(trial_resonances[t]) + "}");
    }
    persist_metric(static_cast<int>(round_), -1,
                   "titan_trials_completed",
                   static_cast<double>(kTitanTrials.size()), "trials");
    world_memory_ = std::min(1.0f, world_memory_ + 0.02f);
}

void Simulation::black_tide_tick() {
    if (!world_mode_) return;

    float avg_eff = 0.0f;
    float avg_corruption = 0.0f;
    float avg_memory = 0.0f;
    int alive_count = 0;
    for (std::size_t i = 0; i < MAX_AGENTS; ++i) {
        if (!agents_[i]) continue;
        avg_eff += agents_[i]->efficiency();
        avg_corruption += corruption_[i];
        avg_memory += memory_[i];
        ++alive_count;
    }
    if (alive_count == 0) return;
    avg_eff /= static_cast<float>(alive_count);
    avg_corruption /= static_cast<float>(alive_count);
    avg_memory /= static_cast<float>(alive_count);

    const float tide_gain =
        10.0f + std::max(0.0f, 1.5f - avg_eff) * 3.0f +
        avg_corruption * 5.0f - avg_memory * 2.0f;
    black_tide_ = std::clamp(black_tide_ + std::max(0.5f, tide_gain),
                             0.0f, 100.0f);

    for (std::size_t i = 0; i < MAX_AGENTS; ++i) {
        if (!agents_[i]) continue;
        float pressure = 1.5f + corruption_[i] * 4.0f +
                         (agents_[i]->efficiency() < 1.2f ? 1.5f : 0.0f) -
                         memory_[i] * 1.5f;
        corruption_[i] = std::clamp(
            corruption_[i] + std::max(0.2f, pressure) * 0.01f,
            0.0f, 1.0f);
        if (corruption_[i] > 0.7f) {
            agents_[i]->update_efficiency(-0.03f);
            coreflame_[i] = std::max(0.1f, coreflame_[i] - 0.01f);
        }
        memory_[i] = std::min(1.0f, memory_[i] + 0.01f);
        persist_metric(static_cast<int>(round_), static_cast<int>(i),
                       "corruption", corruption_[i], "ratio");
        persist_metric(static_cast<int>(round_), static_cast<int>(i),
                       "coreflame", coreflame_[i], "flame");
    }

    world_memory_ = std::min(
        1.0f, world_memory_ +
                  0.01f * static_cast<float>(alive_count) / MAX_AGENTS);
    stability_ = std::clamp(
        100.0f - black_tide_ + world_memory_ * 15.0f, 0.0f, 100.0f);

    persist_metric(static_cast<int>(round_), -1, "black_tide",
                   black_tide_, "ratio");
    persist_metric(static_cast<int>(round_), -1, "stability",
                   stability_, "ratio");
    persist_metric(static_cast<int>(round_), -1, "world_memory",
                   world_memory_, "ratio");
}

void Simulation::simulate_catastrophe() {
    if (!world_mode_) return;

    std::vector<AgentId> alive;
    for (std::size_t i = 0; i < MAX_AGENTS; ++i) {
        if (agents_[i]) alive.push_back(static_cast<AgentId>(i));
    }
    if (alive.empty()) return;

    AgentId narrator = strongest_agent();
    int best_resonance = titan_resonance_[narrator];
    for (AgentId id : alive) {
        if (titan_resonance_[id] > best_resonance) {
            best_resonance = titan_resonance_[id];
            narrator = id;
        }
    }

    static constexpr const char* kFallbackForms[13] = {
        "门径崩解：所有门同时闭合，世界被折叠进一条无法返回的走廊。",
        "律法失序：天穹的规则互相矛盾，因果开始倒流，城邦在矛盾中碎裂。",
        "岁月断层：同一天反复发生，时间在记忆里烧成灰，世界忘记如何向前。",
        "大地沉没：地脉断裂，群山沉入无底的尘海，最后的根也被拔起。",
        "海洋倒灌：海面升过天空，盐与梦一起淹没所有尚未说出口的名字。",
        "天空坠落：星辰脱离轨道，白昼与黑夜同时砸向大地。",
        "负世崩塌：承载世界的重量失去支点，所有誓言一同下沉。",
        "理性过载：逻辑自我递归到没有答案，世界在证明自身时冻结。",
        "浪漫枯竭：火焰失去颜色，万物仍在运转，却再没有任何东西值得凝视。",
        "纷争失控：每一次选择都分裂出新的战场，世界被无尽的对抗撕开。",
        "死亡倒影：死亡不再一次发生，而是同时发生在所有可能的时间。",
        "诡计反噬：所有可能性被伪装成同一条道路，世界迷失在自己的选择里。",
        "全世回声：记忆与开拓互相覆盖，世界在无限可能中失去唯一形状。",
    };
    const std::size_t form_index =
        (round_ + static_cast<std::uint32_t>(narrator)) % 13;
    std::string catastrophe = kFallbackForms[form_index];
    const std::string system =
        "你是因子" + agents_[narrator]->factor_name() + "（" +
        agents_[narrator]->heir_name() + "），正在见证第 " +
        std::to_string(round_) + " 轮再创世前的世界毁灭。"
        "请以角色视角描述这一轮世界毁灭的具体形式。"
        "只输出两句话，不要解释规则、不要标题、不要提示词。";
    const std::string user =
        "泰坦试炼：全部 13 泰坦（" + titan_trial_list() + "）" +
        "；黑潮：" + std::to_string(static_cast<int>(black_tide_)) +
        "/100；稳定：" + std::to_string(static_cast<int>(stability_)) +
        "。请描述世界此刻如何毁灭，并留下一个仍然存在的火种意象。";

    bool catastrophe_from_model = false;
    if (SharedModelPool::available()) {
        agents_[narrator]->reset_dialogue_context();
        const std::string generated = utf8_safe(
            agents_[narrator]->generate_chat(system, user, 64));
        if (usable_catastrophe(generated)) {
            catastrophe = generated;
            catastrophe_from_model = true;
        }
    }

    float avg_corruption = 0.0f;
    for (AgentId id : alive) avg_corruption += corruption_[id];
    avg_corruption /= static_cast<float>(alive.size());

    last_catastrophe_ = catastrophe;
    last_catastrophe_agent_ = static_cast<int>(narrator);
    last_catastrophe_severity_ = std::clamp(
        0.25f + black_tide_ / 120.0f + avg_corruption * 0.5f +
            std::max(0.0f, 1.5f - agents_[narrator]->efficiency()) * 0.2f,
        0.2f, 1.0f);

    black_tide_ = std::clamp(
        black_tide_ + 2.0f + last_catastrophe_severity_ * 6.0f,
        0.0f, 100.0f);
    stability_ = std::clamp(
        stability_ - last_catastrophe_severity_ * 5.0f, 0.0f, 100.0f);
    memory_[narrator] = std::min(1.0f, memory_[narrator] + 0.05f);
    coreflame_[narrator] = std::min(1.5f, coreflame_[narrator] + 0.02f);
    world_memory_ = std::min(1.0f, world_memory_ + 0.02f);
    const std::string catastrophe_source =
        catastrophe_from_model ? "world_catastrophe_llm"
                               : "world_catastrophe_fallback";
    train_and_persist(
        static_cast<int>(narrator), user, catastrophe,
        catastrophe_source, catastrophe_from_model ? 0.72f : 0.35f);
    for (AgentId id : alive) {
        agents_[id]->remember(
            "World catastrophe witnessed: " + catastrophe,
            MemoryKind::World,
            0.55f + last_catastrophe_severity_ * 0.35f,
            static_cast<int>(round_), catastrophe_source);
    }

    std::cout << "[世界毁灭] Agent " << narrator << " "
              << agents_[narrator]->factor_name()
              << " 看到的毁灭形式：" << catastrophe << std::endl;
    persist_event(
        static_cast<int>(round_), static_cast<int>(narrator),
        "world_catastrophe",
        "{\"trial\":\"" + json_escape(current_trial_) +
            "\",\"narrator\":" + std::to_string(narrator) +
            ",\"titan_resonance\":" +
            std::to_string(titan_resonance_[narrator]) +
            ",\"severity\":" + std::to_string(last_catastrophe_severity_) +
            ",\"source\":\"" +
            (catastrophe_from_model ? "llm" : "fallback") + "\"" +
            ",\"text\":\"" + json_escape(catastrophe) + "\"}");
    persist_metric(static_cast<int>(round_), -1, "catastrophe_severity",
                   last_catastrophe_severity_, "ratio");
}

void Simulation::print_world_state() const {
    if (!world_mode_) return;
    float flame_total = 0.0f;
    float corruption_total = 0.0f;
    int alive_count = 0;
    for (std::size_t i = 0; i < MAX_AGENTS; ++i) {
        if (!agents_[i]) continue;
        flame_total += coreflame_[i];
        corruption_total += corruption_[i];
        ++alive_count;
    }
    std::cout << "[世界状态] 第 " << cycle_ << " 次再创世"
              << " | 泰坦试炼：" << current_trial_
              << "（覆盖率 13/13，累计 " << titan_trials_completed_
              << " 场）"
              << " | 黑潮 " << std::fixed << std::setprecision(1)
              << black_tide_ << "/100"
              << " | 稳定 " << stability_
              << " | 世界记忆 " << world_memory_ * 100.0f << "%"
              << " | 火种总量 " << flame_total
              << " | 平均侵蚀 "
              << (alive_count
                      ? corruption_total / static_cast<float>(alive_count)
                      : 0.0f)
              << std::endl << std::endl;
}

void Simulation::print_genesis_ending() {
    if (!world_mode_) return;

    std::string ending;
    if (black_tide_ >= 80.0f) {
        ending = "黑潮吞世：世界未能完成再创世";
    } else if (agents_[12] && stability_ >= 65.0f &&
               world_memory_ >= 0.55f) {
        ending = "第十三因子·全世记忆：昔涟见证新的创世";
    } else if (stability_ >= 65.0f) {
        ending = "黎明再创世：火种穿过黑潮，世界重新点燃";
    } else if (world_memory_ >= 0.70f) {
        ending = "记忆守望：世界停滞，但记忆被完整保存";
    } else {
        ending = "脆弱轮回：世界勉强重启，代价被遗忘";
    }

    std::cout << "\n[再创世结局] " << ending << std::endl;
    if (!last_catastrophe_.empty()) {
        std::cout << "  最后的毁灭形式：" << last_catastrophe_ << std::endl;
    }
    std::cout << "  黑潮：" << std::fixed << std::setprecision(1)
              << black_tide_ << "/100"
              << " | 稳定：" << stability_
              << " | 世界记忆：" << world_memory_ * 100.0f << "%"
              << std::endl;
    persist_event(static_cast<int>(round_), -1, "world_ending",
                  "{\"ending\":\"" + json_escape(ending) +
                      "\",\"black_tide\":" + std::to_string(black_tide_) +
                      ",\"stability\":" + std::to_string(stability_) +
                      ",\"world_memory\":" + std::to_string(world_memory_) +
                      "}");
    persist_metric(static_cast<int>(round_), -1, "final_stability",
                   stability_, "ratio");
    persist_metric(static_cast<int>(round_), -1, "final_black_tide",
                   black_tide_, "ratio");
}

void Simulation::run_full_simulation() {
    std::cout << "\n" << std::string(70, '=') << std::endl;
    std::cout << "  文明火种 v3.3.10 — 13 共享LLM智能体 × 批量解码 × KFL-M" << std::endl;
    std::cout << "  真FFN + KV滑动窗口 + 活跃集调度" << std::endl;
    if (world_mode_)
        std::cout << "  翁法罗斯灵感模式：泰坦试炼 × 火种传承 × 黑潮 × 再创世"
                  << std::endl;
    std::cout << std::string(70, '=') << std::endl;

    persist_event(0, -1, "simulation_start", "{}");

    print_separator();
    std::cout << "\n[初始阶段：13 个因子陈述泰坦路径、原动力与生命第一因]\n" << std::endl;
    for (std::size_t i = 0; i < MAX_AGENTS; ++i) {
        std::cout << "  Agent " << std::setw(2) << i << " | "
                  << agents_[i]->factor_name() << " / "
                  << agents_[i]->heir_name() << " | "
                  << agents_[i]->titan_path() << " · "
                  << agents_[i]->drive() << " → "
                  << agents_[i]->simulated_path() << std::endl;
        std::cout << "           AI 路线：" << agents_[i]->ai_route()
                  << " | 独有进化：" << agents_[i]->evolution_method()
                  << std::endl;
        std::cout << "           第一因：" << agents_[i]->express_first_cause() << std::endl;
    }

    for (int r = 0; r < max_rounds_; ++r) {
        if (active_count_ <= 1) break;
        ++round_;
        round_id_ = -1;
#ifdef CIV_HAS_POSTGRES
        if (store_ && run_id_ >= 0) {
            try {
                round_id_ = store_->start_round(
                    run_id_, static_cast<int>(round_), "world");
            } catch (const std::exception& e) {
                std::cerr << "[Postgres] round 写入失败: " << e.what()
                          << std::endl;
            }
        }
#endif
        persist_event(static_cast<int>(round_), -1, "round_start", "{}");
        if (world_mode_) apply_titan_trial();

        print_separator();
        std::cout << "\n########## 第 " << round_ << " 轮世界 ##########\n" << std::endl;

        if (roundtable_mode_)
            roundtable_phase();
        else
            debate_phase();
        learning_phase();
        if (world_mode_) black_tide_tick();
        if (world_mode_) simulate_catastrophe();
        elimination_phase();
        rebirth_phase();
        if (world_mode_) print_world_state();
        print_agents_status();
        persist_event(static_cast<int>(round_), -1, "round_end", "{}");
#ifdef CIV_HAS_POSTGRES
        if (store_ && round_id_ > 0) {
            try {
                store_->finish_round(round_id_);
            } catch (const std::exception& e) {
                std::cerr << "[Postgres] round 结束写入失败: " << e.what()
                          << std::endl;
            }
        }
#endif

        if (active_count_ <= 1) {
            std::cout << "\n[系统] 只剩一个智能体，提前结束。\n";
            break;
        }
        if (world_mode_ && black_tide_ >= 100.0f) break;
    }

    print_separator();
    std::cout << "\n" << std::string(70, '=') << std::endl;
    std::cout << "  [最终结果] 文明火种持有者" << std::endl;
    std::cout << std::string(70, '=') << std::endl;

    AgentId winner = strongest_agent();
    std::cout << "\n  Agent " << winner << " | "
              << agents_[winner]->factor_name() << " / "
              << agents_[winner]->heir_name() << std::endl;
    std::cout << "  泰坦路径：" << agents_[winner]->titan_path()
              << " | 原动力：" << agents_[winner]->drive()
              << " | 模拟命途：" << agents_[winner]->simulated_path() << std::endl;
    std::cout << "  AI 路线：" << agents_[winner]->ai_route() << std::endl;
    std::cout << "  独有进化：" << agents_[winner]->evolution_method() << std::endl;
    std::cout << "  生命第一因：" << agents_[winner]->express_first_cause() << std::endl;
    std::cout << "  效率评分：" << std::fixed << std::setprecision(4)
              << agents_[winner]->efficiency() << std::endl;
    std::cout << "  发送帧数：" << agents_[winner]->frames_sent() << std::endl;
    std::cout << "  接收帧数：" << agents_[winner]->frames_recv() << std::endl;
    std::cout << "\n  他将带领文明开启下一轮..." << std::endl;
    std::cout << std::string(70, '=') << std::endl;
    persist_all_minds(static_cast<int>(round_));
    print_mind_summary();
    export_self_training();
    print_genesis_ending();

#ifdef CIV_HAS_POSTGRES
    if (store_ && run_id_ >= 0) {
        try {
            store_->finish_self_training_runs(run_id_);
            store_->finish_run(run_id_, "completed");
            if (conversation_id_ >= 0)
                store_->finish_conversation(conversation_id_);
        } catch (const std::exception& e) {
            std::cerr << "[Postgres] run 结束写入失败: " << e.what()
                      << std::endl;
        }
    }
#endif
}

void Simulation::debate_phase() {
    std::cout << "[阶段 1：辩论 — 批量 LLM 生成 + KFL-M 帧交换]\n" << std::endl;

    std::vector<AgentId> alive;
    std::vector<LLMAgent*> ptrs;
    for (std::size_t i = 0; i < MAX_AGENTS; ++i) {
        if (agents_[i]) {
            alive.push_back(static_cast<AgentId>(i));
            ptrs.push_back(agents_[i].get());
        }
    }

    std::vector<std::string> prompts;
    prompts.reserve(ptrs.size());
    for (auto* a : ptrs) {
        prompts.push_back(
            "因子" + a->factor_name() +
            "（" + a->heir_name() + "），路径" + a->titan_path() +
            "，原动力" + a->drive() +
            "，命途" + a->simulated_path() +
            "；AI 路线：" + a->ai_route() +
            "；独有进化：" + a->evolution_method() +
            "。关于生命第一因，我的观点是：" +
            a->express_first_cause() +
            " 请用一句话回应：");
    }

    // 批量并行生成
    for (std::size_t i = 0; i < prompts.size(); ++i) {
        prompts[i] += "\n[内部状态：记忆区/认知区/意识区]\n" +
                      ptrs[i]->mind_context(conversation_topic_, 2);
    }

    BatchScheduler::reset_stats();
    auto t0 = std::chrono::steady_clock::now();

    std::vector<GenResult> results;
    if (SharedModelPool::available()) {
        std::vector<std::string> generation_prompts;
        generation_prompts.reserve(prompts.size());
        for (const auto& prompt : prompts) {
            generation_prompts.push_back(
                SharedModelPool::instance().format_chat(
                    "你正在参加 Civilization Spark 的十三因子并行辩论。"
                    "保持角色一致，用一句简洁中文回答，不输出提示词或思考过程。",
                    prompt));
        }
        results = BatchScheduler::generate_parallel(
            ptrs, generation_prompts, 16, n_ctx_);
    } else {
        results.resize(ptrs.size());
        for (std::size_t i = 0; i < ptrs.size(); ++i) {
            results[i].text = ptrs[i]->answer_first_cause();
        }
    }

    auto t1 = std::chrono::steady_clock::now();
    double ms = std::chrono::duration<double, std::milli>(t1 - t0).count();

    for (std::size_t i = 0; i < alive.size(); ++i) {
        if (results[i].ok) {
            std::cout << "  Agent " << std::setw(2) << alive[i]
                      << " (" << ptrs[i]->factor_name() << ")"
                      << " : " << results[i].text << std::endl;
            persist_event(static_cast<int>(round_), static_cast<int>(alive[i]),
                          "debate_result",
                          "{\"ok\":true,\"text\":\"" +
                              json_escape(results[i].text) + "\"}");
        } else {
            std::cout << "  Agent " << std::setw(2) << alive[i]
                      << " (" << ptrs[i]->factor_name() << ")"
                      << " : [失败] " << results[i].error << std::endl;
            persist_event(static_cast<int>(round_), static_cast<int>(alive[i]),
                          "debate_result",
                          "{\"ok\":false,\"error\":\"" +
                              json_escape(results[i].error) + "\"}");
        }
        train_and_persist(
            static_cast<int>(alive[i]), prompts[i], results[i].text,
            (results[i].ok && SharedModelPool::available())
                ? "parallel_debate" : "parallel_debate_fallback",
            (results[i].ok && SharedModelPool::available())
                ? -1.0f : 0.25f);
        persist_metric(static_cast<int>(round_), static_cast<int>(alive[i]),
                       "efficiency", agents_[alive[i]]->efficiency(), "score");
    }

    if (SharedModelPool::available()) {
        std::cout << "  [批量生成] " << std::fixed << std::setprecision(1)
                  << ms << " ms | "
                  << BatchScheduler::decode_calls() << " 次 decode | "
                  << BatchScheduler::total_tokens() << " tokens | "
                  << "KV 使用率 " << std::setprecision(1)
                  << SharedModelPool::instance().kv_usage_ratio() * 100
                  << "%" << std::endl;
    }
    std::cout << std::endl;

    // KFL-M 帧交换
    std::cout << "  [KFL-M 帧交换]" << std::endl;
    std::size_t total_frames = 0, total_bytes = 0;

    for (std::size_t i = 0; i < alive.size(); ++i) {
        for (std::size_t j = i + 1; j < alive.size(); ++j) {
            AgentId a = alive[i], b = alive[j];
            Byte frame[4];
            Byte session = static_cast<Byte>(a & 0x0F);
            std::uint16_t idx = static_cast<std::uint16_t>(b * 100 + i);
            auto n = encode_index(idx, session, frame, 4);

            if (total_frames < 3) {
                std::cout << "    Agent " << a << " -> Agent " << b << " : ";
                for (std::size_t k = 0; k < n; ++k) std::printf("%02X ", frame[k]);
                std::cout << "(" << n << "B)" << std::endl;
            }
            agents_[a]->record_sent(n);
            agents_[b]->record_recv(n);
            total_frames++; total_bytes += n;
        }
    }
    std::cout << "    总计 " << total_frames << " 帧, "
              << total_bytes << " 字节 (平均 "
              << std::fixed << std::setprecision(2)
              << (total_frames
                      ? static_cast<double>(total_bytes) / total_frames
                      : 0.0)
              << " B/帧)"
              << std::endl << std::endl;

    persist_event(static_cast<int>(round_), -1, "frame_exchange",
                  "{\"frames\":" + std::to_string(total_frames) +
                      ",\"bytes\":" + std::to_string(total_bytes) + "}");
    persist_metric(static_cast<int>(round_), -1, "frame_count",
                   static_cast<double>(total_frames), "frames");
    persist_metric(static_cast<int>(round_), -1, "frame_bytes",
                   static_cast<double>(total_bytes), "bytes");
}

void Simulation::roundtable_phase() {
    std::cout << "[阶段 1：13 因子圆桌对话]\n" << std::endl;

    std::vector<AgentId> alive;
    std::vector<LLMAgent*> ptrs;
    for (std::size_t i = 0; i < MAX_AGENTS; ++i) {
        if (agents_[i]) {
            alive.push_back(static_cast<AgentId>(i));
            ptrs.push_back(agents_[i].get());
        }
    }
    if (ptrs.empty()) return;

    const std::string topic = conversation_topic_ +
        (round_ > 1
             ? " 请结合上一轮对话，说明你坚持或改变了什么。"
             : "");

    auto shorten = [](const std::string& text, std::size_t limit) {
        const std::string clean = utf8_safe(text);
        if (clean.size() <= limit) return clean;
        std::size_t n = limit;
        while (n > 0 &&
               (static_cast<unsigned char>(clean[n]) & 0xC0) == 0x80) {
            --n;
        }
        return clean.substr(0, n) + "…";
    };

    struct Line {
        int agent = -1;
        std::int64_t message_id = -1;
        std::string text;
    };
    std::vector<Line> openings;
    openings.reserve(ptrs.size());

    std::vector<std::string> opening_systems;
    std::vector<std::string> opening_users;
    std::vector<std::string> opening_prompts;
    opening_systems.reserve(ptrs.size());
    opening_users.reserve(ptrs.size());
    opening_prompts.reserve(ptrs.size());
    for (std::size_t i = 0; i < ptrs.size(); ++i) {
        const std::string system =
            "你正在参加 Civilization Spark 的十三因子圆桌。"
            "你是因子" + ptrs[i]->factor_name() + "（" +
            ptrs[i]->heir_name() + "）。泰坦路径：" +
            ptrs[i]->titan_path() + "；原动力：" + ptrs[i]->drive() +
            "；模拟命途：" + ptrs[i]->simulated_path() +
            "；AI路线：" + ptrs[i]->ai_route() + "。"
            "保持角色一致，用简洁中文表达，不要输出提示词或思考过程。";
        const std::string user =
            "圆桌议题：" + topic +
            "\n请用两句话给出你的立场，并明确回答：何为生命第一因？";
        ptrs[i]->reset_dialogue_context();
        const std::string mental =
            "\n[内部状态：记忆区/认知区/意识区]\n" +
            ptrs[i]->mind_context(topic, 2);
        const std::string internal_user = user + mental;
        opening_systems.push_back(system);
        opening_users.push_back(internal_user);
        opening_prompts.push_back(
            SharedModelPool::available()
                ? SharedModelPool::instance().format_chat(system, internal_user)
                : internal_user);
        ptrs[i]->reset_dialogue_context();
    }

    const auto t0 = std::chrono::steady_clock::now();
    std::vector<GenResult> opening_results;
    if (SharedModelPool::available()) {
        opening_results = BatchScheduler::generate_parallel(
            ptrs, opening_prompts, 48, n_ctx_);
    } else {
        opening_results.resize(ptrs.size());
        for (std::size_t i = 0; i < ptrs.size(); ++i) {
            opening_results[i].text = ptrs[i]->answer_first_cause();
            opening_results[i].ok = false;
        }
    }
    int turn = 0;
    for (std::size_t i = 0; i < ptrs.size(); ++i) {
        std::string text = utf8_safe(opening_results[i].text);
        const bool generated_usable =
            SharedModelPool::available() && opening_results[i].ok &&
            usable_reply(text);
        if (!generated_usable)
            text = utf8_safe(ptrs[i]->answer_first_cause());

        const int agent = static_cast<int>(alive[i]);
        const auto mid = persist_conversation_message(
            static_cast<int>(round_), turn++, agent, -1, -1,
            "opening", text);
        openings.push_back({agent, mid, text});
        train_and_persist(
            agent, opening_systems[i] + "\n" + opening_users[i], text,
            generated_usable ? "roundtable_opening"
                             : "roundtable_opening_fallback",
            generated_usable ? -1.0f : 0.30f);

        std::cout << "  [开场] Agent " << std::setw(2) << agent
                  << " " << ptrs[i]->factor_name()
                  << "：" << text << std::endl;
        persist_event(static_cast<int>(round_), agent,
                      "conversation_opening", "{\"text\":\"" +
                          json_escape(text) + "\"}");
    }
    const auto t1 = std::chrono::steady_clock::now();
    const double opening_ms =
        std::chrono::duration<double, std::milli>(t1 - t0).count();

    std::string opening_context;
    for (const auto& line : openings) {
        opening_context +=
            "[Agent " + std::to_string(line.agent) + " " +
            agents_[line.agent]->factor_name() + "] " +
            shorten(line.text, 28) + "\n";
    }

    // Rebuttal pass: each agent must answer someone else after reading the room.
    std::vector<Line> rebuttals;
    for (std::size_t i = 0; i < ptrs.size(); ++i) {
        static constexpr int kOpponents[13] = {
            6, 5, 10, 4, 3, 1, 0, 12, 11, 1, 2, 8, 7
        };
        std::size_t target_idx =
            (i == 0 ? openings.size() - 1 : i - 1);
        const int desired = kOpponents[alive[i] % 13];
        for (std::size_t k = 0; k < openings.size(); ++k) {
            if (openings[k].agent == desired) {
                target_idx = k;
                break;
            }
        }
        const int target_agent = openings[target_idx].agent;
        LLMAgent* target = agents_[target_agent].get();

        std::string recent;
        const std::size_t begin = rebuttals.size() > 3
                                      ? rebuttals.size() - 3 : 0;
        for (std::size_t k = begin; k < rebuttals.size(); ++k) {
            recent += "[Agent " + std::to_string(rebuttals[k].agent) + "] " +
                      shorten(rebuttals[k].text, 40) + "\n";
        }

        const std::string system =
            "你是因子" + ptrs[i]->factor_name() + "（" +
            ptrs[i]->heir_name() + "），原动力是" +
            ptrs[i]->drive() + "，模拟命途是" +
            ptrs[i]->simulated_path() + "。"
            "你正在参加十三因子圆桌，必须直接回应其他因子，不要复述提示词。";
        const std::string prompt =
            "圆桌议题：" + topic +
            "\n开场观点：\n" + opening_context +
            "\n最近回应：\n" + recent +
            "\n请直接回应 Agent " + std::to_string(target_agent) +
            "（" + target->factor_name() +
            "）的观点：你同意或不同意什么？请用两句话，并重申你的第一因判断。";

        const std::string internal_prompt =
            prompt + "\n[内部状态：记忆区/认知区/意识区]\n" +
            ptrs[i]->mind_context(prompt, 2);
        std::string text;
        if (SharedModelPool::available()) {
            ptrs[i]->reset_dialogue_context();
            text = utf8_safe(
                ptrs[i]->generate_chat(system, internal_prompt, 40));
        }
        const bool generated_usable =
            SharedModelPool::available() && usable_reply(text);
        if (!generated_usable) {
            text = utf8_safe(ptrs[i]->answer_first_cause()) + " 我回应 " +
                   target->factor_name() + "：" +
                   shorten(openings[target_idx].text, 72);
        }

        const auto mid = persist_conversation_message(
            static_cast<int>(round_), turn++,
            static_cast<int>(alive[i]), target_agent,
            openings[target_idx].message_id, "rebuttal", text);
        rebuttals.push_back({static_cast<int>(alive[i]), mid, text});
        train_and_persist(
            static_cast<int>(alive[i]), system + "\n" + internal_prompt, text,
            generated_usable ? "roundtable_rebuttal"
                             : "roundtable_rebuttal_fallback",
            generated_usable ? -1.0f : 0.30f);

        std::cout << "  Agent " << std::setw(2) << alive[i]
                  << " -> Agent " << std::setw(2) << target_agent
                  << "：" << text << std::endl;
        persist_event(static_cast<int>(round_), static_cast<int>(alive[i]),
                      "conversation_rebuttal",
                      "{\"target_agent\":" + std::to_string(target_agent) +
                          ",\"text\":\"" + json_escape(text) + "\"}");
    }

    std::cout << "  [对话统计] " << openings.size() << " 开场 + "
              << rebuttals.size() << " 回应 | 开场生成 "
              << std::fixed << std::setprecision(1) << opening_ms
              << " ms" << std::endl << std::endl;
    persist_metric(static_cast<int>(round_), -1, "conversation_messages",
                   static_cast<double>(openings.size() + rebuttals.size()),
                   "messages");
}

void Simulation::learning_phase() {
    std::cout << "[阶段 2：13 条 AI 学习路线并行进化]\n" << std::endl;

    Vector collective;
    std::size_t alive_count = 0;
    for (std::size_t i = 0; i < MAX_AGENTS; ++i) {
        if (!agents_[i]) continue;
        const Vector& emb = agents_[i]->embedding();
        for (std::size_t k = 0; k < DIM; ++k)
            collective[k] += emb[k];
        ++alive_count;
    }
    if (alive_count > 0) {
        for (std::size_t k = 0; k < DIM; ++k)
            collective[k] /= static_cast<float>(alive_count);
    }

    const AgentId strongest = strongest_agent();
    const Vector rival = agents_[strongest]->embedding();

    for (std::size_t i = 0; i < MAX_AGENTS; ++i) {
        if (!agents_[i]) continue;
        Vector exp = agents_[i]->cognition().belief;
        if (exp.energy() < 1e-6f)
            exp = agents_[i]->embedding();

        const float before = agents_[i]->efficiency();
        agents_[i]->evolve(exp, collective, rival, 0.01f);
        agents_[i]->consolidate_memory(static_cast<int>(round_));
        const float after = agents_[i]->efficiency();

        std::cout << "  Agent " << std::setw(2) << i
                  << " " << agents_[i]->factor_name()
                  << " | " << agents_[i]->ai_route()
                  << " | 效率 " << std::fixed << std::setprecision(4)
                  << before << " -> " << after << std::endl;

        persist_event(
            static_cast<int>(round_), static_cast<int>(i), "evolution",
            "{\"route\":\"" + json_escape(agents_[i]->ai_route()) +
                "\",\"method\":\"" +
                json_escape(agents_[i]->evolution_method()) +
                "\",\"before\":" + std::to_string(before) +
                ",\"after\":" + std::to_string(after) + "}");
        persist_metric(static_cast<int>(round_), static_cast<int>(i),
                       "efficiency", after, "score");
        const MindSnapshot mind = agents_[i]->mind_snapshot();
        persist_metric(static_cast<int>(round_), static_cast<int>(i),
                       "memory_count",
                       static_cast<double>(mind.memory_count), "items");
        persist_metric(static_cast<int>(round_), static_cast<int>(i),
                       "cognitive_confidence",
                       mind.cognition.confidence, "ratio");
        persist_metric(static_cast<int>(round_), static_cast<int>(i),
                       "consciousness_integrity",
                       mind.consciousness.integrity, "ratio");
        persist_mind_state(static_cast<int>(round_), static_cast<int>(i), true);
        if (world_mode_) {
            const float memory_density = std::min(
                1.0f, static_cast<float>(mind.memory_count) / 48.0f);
            memory_[i] = std::min(
                1.0f, memory_[i] + 0.02f + after * 0.01f +
                          memory_density * 0.04f);
            coreflame_[i] = std::min(
                1.5f, coreflame_[i] + 0.01f * after);
        }
    }
    std::cout << std::endl;
}

void Simulation::elimination_phase() {
    std::cout << "[阶段 3：淘汰 — 火种剥夺]\n" << std::endl;
    if (active_count_ <= 1) return;
    AgentId worst = weakest_agent();
    persist_mind_state(static_cast<int>(round_), static_cast<int>(worst), true);
    std::cout << "  淘汰 Agent " << worst
              << " (效率 " << std::fixed << std::setprecision(4)
              << agents_[worst]->efficiency() << ")" << std::endl;
    std::cout << "  火种被最高效者 " << strongest_agent() << " 保存\n" << std::endl;
    persist_event(static_cast<int>(round_), static_cast<int>(worst),
                  "eliminated",
                  "{\"efficiency\":" +
                      std::to_string(agents_[worst]->efficiency()) +
                      ",\"strongest\":" +
                      std::to_string(strongest_agent()) + "}");
    if (world_mode_) {
        const AgentId successor = strongest_agent();
        if (successor != worst) {
            agents_[successor]->inherit_from(*agents_[worst], 0.45f);
            persist_mind_state(
                static_cast<int>(round_), static_cast<int>(successor), true);
            coreflame_[successor] = std::min(
                1.5f, coreflame_[successor] + coreflame_[worst] * 0.5f);
            memory_[successor] = std::min(
                1.0f, memory_[successor] + memory_[worst] * 0.4f);
            corruption_[successor] = std::min(
                1.0f, corruption_[successor] + corruption_[worst] * 0.25f);
            world_memory_ = std::min(
                1.0f, world_memory_ + memory_[worst] * 0.05f);
            std::cout << "  火种传承：Agent " << worst << " -> Agent "
                      << successor << std::endl;
            persist_event(
                static_cast<int>(round_), static_cast<int>(successor),
                "coreflame_transfer",
                "{\"from\":" + std::to_string(worst) +
                    ",\"to\":" + std::to_string(successor) +
                    ",\"flame\":" + std::to_string(coreflame_[worst]) + "}");
        }
    }
    persist_mind_state(static_cast<int>(round_), static_cast<int>(worst), true);
    agents_[worst].reset();
    coreflame_[worst] = 0.0f;
    corruption_[worst] = 0.0f;
    memory_[worst] = 0.0f;
    --active_count_;
}

void Simulation::rebirth_phase() {
    if (world_mode_) {
        ++cycle_;
        black_tide_ = std::max(
            0.0f, black_tide_ - (4.0f + world_memory_ * 6.0f));
        for (std::size_t i = 0; i < MAX_AGENTS; ++i) {
            if (!agents_[i]) continue;
            corruption_[i] *= 0.7f;
            coreflame_[i] = std::min(1.5f, coreflame_[i] + 0.03f);
            memory_[i] = std::min(1.0f, memory_[i] + 0.05f);
            agents_[i]->remember(
                "Re-creation cycle " + std::to_string(cycle_) +
                    " preserved the coreflame and consolidated memories",
                MemoryKind::World, 0.62f,
                static_cast<int>(round_), "re_creation");
            agents_[i]->consolidate_memory(static_cast<int>(round_));
        }
        world_memory_ = std::min(1.0f, world_memory_ + 0.02f);
        stability_ = std::clamp(
            100.0f - black_tide_ + world_memory_ * 15.0f,
            0.0f, 100.0f);
        std::cout << "[再创世] 第 " << cycle_
                  << " 次再创世完成，火种与记忆进入下一世。" << std::endl;
        persist_event(
            static_cast<int>(round_), -1, "re_creation",
            "{\"cycle\":" + std::to_string(cycle_) +
                ",\"black_tide\":" + std::to_string(black_tide_) +
                ",\"stability\":" + std::to_string(stability_) +
                ",\"world_memory\":" + std::to_string(world_memory_) + "}");
        persist_metric(static_cast<int>(round_), -1, "re_creation_cycle",
                       static_cast<double>(cycle_), "cycle");
        persist_all_minds(static_cast<int>(round_));
        return;
    }

    std::cout << "[阶段 4：世界毁灭与重生]\n" << std::endl;
    std::cout << "  熵增达到临界值，世界毁灭..." << std::endl;
    std::cout << "  文明火种被保存，下一轮即将开启...\n" << std::endl;
    persist_event(static_cast<int>(round_), -1, "rebirth", "{}");
    for (std::size_t i = 0; i < MAX_AGENTS; ++i)
        if (agents_[i]) {
            agents_[i]->update_efficiency(0.1f);
            agents_[i]->consolidate_memory(static_cast<int>(round_));
        }
    persist_all_minds(static_cast<int>(round_));
}

void Simulation::print_agents_status() const {
    std::cout << "[存活智能体状态]" << std::endl;
    for (std::size_t i = 0; i < MAX_AGENTS; ++i) {
        if (agents_[i]) {
            std::cout << "  Agent " << std::setw(2) << i
                      << " " << agents_[i]->factor_name()
                      << " | 效率 " << std::fixed << std::setprecision(4)
                      << std::setw(8) << agents_[i]->efficiency()
                      << " | 发送 " << std::setw(6) << agents_[i]->bytes_sent() << "B"
                      << " | 接收 " << std::setw(6) << agents_[i]->bytes_recv() << "B"
                      << " | n_past " << agents_[i]->state().n_past
                      << " | memory " << agents_[i]->memories().size()
                      << " | confidence " << std::fixed << std::setprecision(2)
                      << agents_[i]->cognition().confidence
                      << " | meta "
                      << agents_[i]->consciousness().self_model_confidence;
            if (world_mode_) {
                std::cout << " | 火种 " << std::fixed << std::setprecision(2)
                          << coreflame_[i] << " | 侵蚀 "
                          << corruption_[i] << " | 记忆 "
                          << memory_[i];
            }
            std::cout << std::endl;
        }
    }
    std::cout << "  存活数量：" << active_count_ << " / " << MAX_AGENTS << std::endl;
}

void Simulation::print_separator() const {
    std::cout << std::string(70, '-') << std::endl;
}

} // namespace civ
