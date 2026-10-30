#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace civ {

struct RunRecord {
    std::uint64_t seed = 0;
    std::string   seed_source = "classical";
    std::string   model_path;
    std::string   model_sha256;
    std::string   quant;
    int           n_ctx = 2048;
    int           n_gpu_layers = 0;
    std::string   config_json = "{}";
    std::string   git_commit;
};

struct AgentRecord {
    int         agent_index = 0;
    std::string factor;
    std::string heir;
    std::string titan_path;
    std::string drive;
    std::string simulated_path;
    std::string ai_route;
    std::string evolution_method;
};

struct EventRecord {
    std::int64_t run_id = -1;
    int          round_no = 0;
    int          agent_index = -1;
    std::string  kind;
    std::string  payload_json = "{}";
};

struct MetricRecord {
    std::int64_t run_id = -1;
    int          round_no = 0;
    int          agent_index = -1;
    std::string  name;
    double       value = 0.0;
    std::string  unit;
    std::string  extra_json = "{}";
};

struct QuantumJobRecord {
    std::string  platform = "originqc";
    std::string  platform_job_id;
    std::string  backend;
    int          programs = 1;
    int          shots_per_program = 10;
    std::string  status;
    std::string  counts_json = "{}";
    std::string  probs_json = "{}";
    double       qpu_runtime_ms = 0.0;
    std::uint64_t derived_seed = 0;
    std::int64_t run_id = -1;
};

struct QuantumSampleRecord {
    std::int64_t quantum_job_id = -1;
    int          program_index = 0;
    std::string  bitstring;
    int          count = 0;
};

struct ConversationMessageRecord {
    std::int64_t conversation_id = -1;
    int          round_no = 0;
    int          turn_no = 0;
    int          speaker_agent = -1;
    int          target_agent = -1;
    std::int64_t reply_to_message_id = -1;
    std::string  phase;
    std::string  content;
};

// 一条 libpq 连接。非线程安全：每个线程使用独立实例。
class PostgresStore {
public:
    PostgresStore() = default;
    ~PostgresStore();

    PostgresStore(const PostgresStore&) = delete;
    PostgresStore& operator=(const PostgresStore&) = delete;

    bool connect(const std::string& dsn);
    bool is_connected() const noexcept { return conn_ != nullptr; }
    const std::string& last_error() const noexcept { return last_error_; }

    void migrate();

    std::int64_t create_run(const RunRecord& run);
    void finish_run(std::int64_t run_id, const std::string& status,
                    const std::string& error = {});

    std::int64_t add_agent(std::int64_t run_id, const AgentRecord& agent);
    std::int64_t start_round(std::int64_t run_id, int round_no,
                             const std::string& phase);
    void finish_round(std::int64_t round_id);

    void log_event(const EventRecord& event);
    void log_metric(const MetricRecord& metric);

    std::int64_t record_quantum_job(const QuantumJobRecord& job);
    void add_quantum_sample(const QuantumSampleRecord& sample);

    std::int64_t create_conversation(std::int64_t run_id,
                                     const std::string& topic,
                                     const std::string& mode);
    std::int64_t add_conversation_message(
        const ConversationMessageRecord& message);
    void finish_conversation(std::int64_t conversation_id);

    std::int64_t create_benchmark(const std::string& name,
                                  const std::string& config_json);
    void add_benchmark_result(std::int64_t benchmark_id, std::int64_t run_id,
                              int agent_index, const std::string& metric,
                              double value,
                              const std::string& extra_json = "{}");

    // 供 benchmark/测试使用的通用查询。
    std::int64_t scalar(const std::string& sql,
                        const std::vector<std::string>& params = {});
    void execute(const std::string& sql,
                 const std::vector<std::string>& params = {});

private:
    void* exec_params(const std::string& sql,
                      const std::vector<std::string>& params,
                      bool expect_rows);
    std::int64_t exec_id(const std::string& sql,
                         const std::vector<std::string>& params);
    void exec_command(const std::string& sql,
                      const std::vector<std::string>& params);

    void* conn_ = nullptr;
    std::string last_error_;
};

} // namespace civ
