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
        void shutdown();

        std::uint32_t current_round() const { return round_; }
        std::size_t surviving_agents() const { return active_count_; }

    private:
        void debate_phase();
        void roundtable_phase();
        void learning_phase();
        void elimination_phase();
        void rebirth_phase();
        void apply_titan_trial();
        void black_tide_tick();
        void simulate_catastrophe();
        void print_world_state() const;
        void print_genesis_ending();
        void train_and_persist(int agent_index, const std::string& prompt,
                               const std::string& response,
                               const std::string& source,
                               float external_reward = -1.0f);
        void persist_mind_state(int round_no, int agent_index,
                                bool include_training = true);
        void persist_all_minds(int round_no);
        void export_self_training();
        void print_mind_summary() const;

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
        bool world_mode_ = false;
        int cycle_ = 0;
        float black_tide_ = 0.0f;
        float stability_ = 100.0f;
        float world_memory_ = 0.0f;
        std::string last_catastrophe_;
        int last_catastrophe_agent_ = -1;
        float last_catastrophe_severity_ = 0.0f;
        std::string current_trial_;
        std::array<float, MAX_AGENTS> coreflame_{};
        std::array<float, MAX_AGENTS> corruption_{};
        std::array<float, MAX_AGENTS> memory_{};
        std::array<int, MAX_AGENTS> titan_resonance_{};
        int titan_trials_completed_ = 0;
        bool self_training_enabled_ = true;
        bool export_training_ = false;
        std::string training_export_path_;
        std::string conversation_topic_ = "何为生命第一因？";
        PostgresStore* store_ = nullptr;
        std::int64_t run_id_ = -1;
        std::int64_t round_id_ = -1;
        std::int64_t conversation_id_ = -1;
    };

} // namespace civ
