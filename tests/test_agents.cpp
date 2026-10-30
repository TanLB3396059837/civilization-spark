#include "civ/ai/llm_agent.hpp"
#include <array>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <memory>
#include <set>
#include <string>

using namespace civ;

static int g_failed = 0;

#define CHECK(cond, name) \
    do { \
        if (cond) std::cout << "  [PASS] " << name << std::endl; \
        else { std::cout << "  [FAIL] " << name << std::endl; ++g_failed; } \
    } while (0)

int main() {
    std::cout << "========== 13 因子档案与进化策略测试 ==========" << std::endl;

    TransformerConfig cfg;
    std::array<std::unique_ptr<LLMAgent>, 13> agents;
    std::set<std::string> factors;
    std::set<std::string> routes;
    std::set<std::string> methods;

    bool profiles_ok = true;
    for (std::size_t i = 0; i < agents.size(); ++i) {
        agents[i] = std::make_unique<LLMAgent>(
            static_cast<AgentId>(i), cfg, 42);

        const auto& a = *agents[i];
        if (a.factor_name().empty() || a.heir_name().empty() ||
            a.titan_path().empty() || a.drive().empty() ||
            a.simulated_path().empty() || a.ai_route().empty() ||
            a.evolution_method().empty() ||
            a.express_first_cause().empty() ||
            a.answer_first_cause().empty()) {
            profiles_ok = false;
        }

        factors.insert(a.factor_name());
        routes.insert(a.ai_route());
        methods.insert(a.evolution_method());
    }

    CHECK(profiles_ok, "13 个因子档案字段完整");
    CHECK(factors.size() == 13, "因子名称唯一");
    CHECK(routes.size() == 13, "AI 学习路线唯一");
    CHECK(methods.size() == 13, "独有进化方式唯一");

    Vector experience, collective, rival;
    for (std::size_t k = 0; k < DIM; ++k) {
        experience[k] = 0.75f;
        collective[k] = 0.25f;
        rival[k] = 0.90f;
    }

    bool evolve_ok = true;
    for (auto& agent : agents) {
        const Vector before_embedding = agent->embedding();
        agent->evolve(experience, collective, rival, 0.01f);
        const float after = agent->efficiency();

        if (!std::isfinite(after) || after < 0.01f || after > 10.0f)
            evolve_ok = false;
        if (before_embedding.l1_error(agent->embedding()) < 1e-8f)
            evolve_ok = false;

        for (float v : agent->embedding().v)
            if (!std::isfinite(v)) evolve_ok = false;
    }
    CHECK(evolve_ok, "13 种进化更新有效且数值有限");

    std::cout << "\n结果：" << (g_failed == 0 ? "全部通过" : "存在失败")
              << std::endl;
    std::cout << "\n========== memory / cognition / meta-cognition =========="
              << std::endl;
    auto& memory_agent = *agents[2];
    const std::string training_prompt =
        "Explain the first cause of life from the perspective of memory.";
    const std::string training_response =
        memory_agent.express_first_cause() +
        " Shared memory preserves experience across generations and lets "
        "the agent revise its beliefs.";
    const auto update = memory_agent.self_train(
        training_prompt, training_response, 1, "unit_test", 0.95f);
    const auto mind = memory_agent.mind_snapshot();
    CHECK(update.score > 0.0f && update.score <= 1.0f,
          "self-training reward is bounded");
    CHECK(mind.memory_count >= 2, "episodic memory was written");
    CHECK(mind.cognition.revisions > 0, "cognition region was updated");
    CHECK(mind.consciousness.reflections > 0,
          "meta-cognitive region was updated");
    CHECK(!memory_agent.mind_context(training_prompt, 3).empty(),
          "memory retrieval builds internal context");

    const auto recalled = memory_agent.recall("memory first cause", 4);
    CHECK(!recalled.empty(), "semantic memory recall returns results");

    LLMAgent successor(static_cast<AgentId>(4), cfg, 42);
    const std::size_t before_inheritance = successor.memories().size();
    successor.inherit_from(memory_agent, 0.5f);
    bool inherited_found = false;
    for (const auto& memory : successor.memories()) {
        if (memory.kind == MemoryKind::Inherited) inherited_found = true;
    }
    CHECK(successor.memories().size() > before_inheritance &&
              inherited_found,
          "coreflame inheritance transfers memories and beliefs");

    for (int i = 0; i < 110; ++i) {
        memory_agent.remember(
            "episode-" + std::to_string(i) +
                " a sufficiently different memory payload",
            MemoryKind::Episode, 0.2f + (i % 5) * 0.05f, 2,
            "capacity_test");
    }
    memory_agent.consolidate_memory(3);
    CHECK(memory_agent.memories().size() <= 96,
          "episodic memory ring buffer is bounded");

    const std::filesystem::path export_path =
        std::filesystem::temp_directory_path() /
        "civilization_spark_agent_training_test.jsonl";
    const auto exported =
        memory_agent.export_training_jsonl(export_path.string(), 999, false);
    CHECK(exported > 0, "accepted self-training sample was exported");
    std::ifstream exported_file(export_path);
    std::string first_line;
    std::getline(exported_file, first_line);
    CHECK(first_line.find("\"accepted\":true") != std::string::npos,
          "exported JSONL contains accepted sample");
    exported_file.close();
    std::error_code remove_error;
    std::filesystem::remove(export_path, remove_error);

    return g_failed == 0 ? 0 : 1;
}
