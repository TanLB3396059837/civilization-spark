#include "civ/ai/llm_agent.hpp"
#include <array>
#include <cmath>
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
    return g_failed == 0 ? 0 : 1;
}
