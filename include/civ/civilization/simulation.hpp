#pragma once
#include "../ai/llm_agent.hpp"
#include <memory>
#include <array>
#include <vector>

namespace civ {

    class PostgresStore;

    class Simulation {
    public:
        static constexpr std::size_t MAX_AGENTS = 13;
        static constexpr std::size_t MAX_ROUNDS = 7;

        Simulation(std::uint64_t seed = 42);
        ~Simulation();

        void run_full_simulation();

        std::uint32_t current_round() const { return round_; }
        std::size_t surviving_agents() const { return active_count_; }

    private:
        void debate_phase();
        void roundtable_phase();
        void learning_phase();
        void elimination_phase();
        void rebirth_phase();

        void print_separator() const;
        void print_agents_status() const;
        AgentId strongest_agent() const;
        AgentId weakest_agent() const;
        void persist_event(int round_no, int agent_index,
                           const std::string& kind,
                           const std::string& payload_json);
        void persist_metric(int round_no, int agent_index,
                            const std::string& name, double value,
                            const std::string& unit,
                            const std::string& extra_json = "{}");
        std::int64_t persist_conversation_message(
            int round_no, int turn_no, int speaker_agent,
            int target_agent, std::int64_t reply_to_message_id,
            const std::string& phase, const std::string& content);

        std::array<std::unique_ptr<LLMAgent>, MAX_AGENTS> agents_;
        std::size_t active_count_ = MAX_AGENTS;
        std::uint32_t round_ = 0;
        int n_ctx_ = 2048;
        int max_rounds_ = static_cast<int>(MAX_ROUNDS);
        bool roundtable_mode_ = false;
        std::string conversation_topic_ = "何为生命第一因？";
        PostgresStore* store_ = nullptr;
        std::int64_t run_id_ = -1;
        std::int64_t round_id_ = -1;
        std::int64_t conversation_id_ = -1;
    };

} // namespace civ
