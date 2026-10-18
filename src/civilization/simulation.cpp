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

namespace civ {

namespace {

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

std::string detect_quant(const std::string& path) {
    if (path.find("Q4_K_M") != std::string::npos) return "Q4_K_M";
    if (path.find("Q8_0") != std::string::npos) return "Q8_0";
    if (path.find("Q5_K_M") != std::string::npos) return "Q5_K_M";
    if (path.find("Q6_K") != std::string::npos) return "Q6_K";
    return "unknown";
}

} // namespace

Simulation::Simulation(std::uint64_t seed) {
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
                    ",\"rounds\":" + std::to_string(MAX_ROUNDS) +
                    ",\"n_ctx\":" + std::to_string(n_ctx_) + "}";
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
#ifdef CIV_HAS_POSTGRES
    delete store_;
#endif
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

void Simulation::run_full_simulation() {
    std::cout << "\n" << std::string(70, '=') << std::endl;
    std::cout << "  文明火种 v3.3 — 13 共享LLM智能体 × 批量解码 × KFL-M" << std::endl;
    std::cout << "  真FFN + KV滑动窗口 + 活跃集调度" << std::endl;
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

    for (std::size_t r = 0; r < MAX_ROUNDS; ++r) {
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

        print_separator();
        std::cout << "\n########## 第 " << round_ << " 轮世界 ##########\n" << std::endl;

        debate_phase();
        learning_phase();
        elimination_phase();
        rebirth_phase();
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

#ifdef CIV_HAS_POSTGRES
    if (store_ && run_id_ >= 0) {
        try {
            store_->finish_run(run_id_, "completed");
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
    BatchScheduler::reset_stats();
    auto t0 = std::chrono::steady_clock::now();

    std::vector<GenResult> results;
    if (SharedModelPool::instance().is_loaded()) {
        results = BatchScheduler::generate_parallel(ptrs, prompts, 16, n_ctx_);
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
        persist_metric(static_cast<int>(round_), static_cast<int>(alive[i]),
                       "efficiency", agents_[alive[i]]->efficiency(), "score");
    }

    if (SharedModelPool::instance().is_loaded()) {
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
        Vector exp;
        for (std::size_t k = 0; k < DIM; ++k)
            exp[k] = static_cast<float>(std::rand() % 1000) / 1000.0f;

        const float before = agents_[i]->efficiency();
        agents_[i]->evolve(exp, collective, rival, 0.01f);
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
    }
    std::cout << std::endl;
}

void Simulation::elimination_phase() {
    std::cout << "[阶段 3：淘汰 — 火种剥夺]\n" << std::endl;
    if (active_count_ <= 1) return;
    AgentId worst = weakest_agent();
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
    agents_[worst].reset();
    --active_count_;
}

void Simulation::rebirth_phase() {
    std::cout << "[阶段 4：世界毁灭与重生]\n" << std::endl;
    std::cout << "  熵增达到临界值，世界毁灭..." << std::endl;
    std::cout << "  文明火种被保存，下一轮即将开启...\n" << std::endl;
    persist_event(static_cast<int>(round_), -1, "rebirth", "{}");
    for (std::size_t i = 0; i < MAX_AGENTS; ++i)
        if (agents_[i]) agents_[i]->update_efficiency(0.1f);
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
                      << std::endl;
        }
    }
    std::cout << "  存活数量：" << active_count_ << " / " << MAX_AGENTS << std::endl;
}

void Simulation::print_separator() const {
    std::cout << std::string(70, '-') << std::endl;
}

} // namespace civ
