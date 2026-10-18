#pragma once
#include "llm_agent.hpp"
#include <vector>
#include <string>

namespace civ {

    struct GenResult {
        std::string text;
        bool        ok = true;
        std::string error;
    };

    class BatchScheduler {
    public:
        static std::vector<GenResult> generate_parallel(
            std::vector<LLMAgent*> agents,
            const std::vector<std::string>& prompts,
            int max_tokens,
            int n_ctx = 2048);

        static int decode_calls()  { return g_decode_calls; }
        static int total_tokens()  { return g_total_tokens; }
        static void reset_stats()  { g_decode_calls = 0; g_total_tokens = 0; }

    private:
        static bool step(std::vector<LLMAgent*>& active);

        static int g_decode_calls;
        static int g_total_tokens;
    };

} // namespace civ