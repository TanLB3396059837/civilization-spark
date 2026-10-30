#include "civ/storage/postgres_store.hpp"

#include <cstdlib>
#include <atomic>
#include <iostream>
#include <string>
#include <thread>
#include <vector>

using namespace civ;

static int g_failed = 0;

#define CHECK(cond, name) \
    do { \
        if (cond) std::cout << "  [PASS] " << name << std::endl; \
        else { std::cout << "  [FAIL] " << name << std::endl; ++g_failed; } \
    } while (0)

int main() {
    std::cout << "========== PostgreSQL 持久化测试 ==========" << std::endl;

    const char* dsn_env = std::getenv("CIV_TEST_PG_DSN");
    if (!dsn_env || !*dsn_env) {
        std::cout << "  [SKIP] 未设置 CIV_TEST_PG_DSN" << std::endl;
        return 0;
    }

    try {
        PostgresStore store;
        if (!store.connect(dsn_env)) {
            std::cerr << "connect failed: " << store.last_error() << std::endl;
            return 1;
        }
        store.migrate();

        RunRecord run;
        run.seed = 42;
        run.seed_source = "originqc";
        run.model_path = "models/Spark-X2.5-4B-Q4_K_M.gguf";
        run.quant = "Q4_K_M";
        run.n_ctx = 512;
        run.config_json = R"({"test":true,"agents":13})";

        const std::int64_t run_id = store.create_run(run);
        CHECK(run_id > 0, "创建 run");

        AgentRecord agent;
        agent.agent_index = 0;
        agent.factor = "Tribios";
        agent.heir = "缇宝";
        agent.titan_path = "门径";
        agent.drive = "同理";
        agent.simulated_path = "同谐";
        agent.ai_route = "多智能体共识学习";
        agent.evolution_method = "共识蒸馏";
        CHECK(store.add_agent(run_id, agent) > 0, "创建 agent");

        const std::int64_t round_id = store.start_round(run_id, 1, "debate");
        CHECK(round_id > 0, "创建 round");
        store.finish_round(round_id);

        EventRecord event;
        event.run_id = run_id;
        event.round_no = 1;
        event.agent_index = 0;
        event.kind = "debate_result";
        event.payload_json = R"({"text":"生命的第一因是同理"})";
        store.log_event(event);

        MetricRecord metric;
        metric.run_id = run_id;
        metric.round_no = 1;
        metric.agent_index = 0;
        metric.name = "efficiency";
        metric.value = 1.234;
        metric.unit = "score";
        store.log_metric(metric);

        const std::int64_t benchmark_id =
            store.create_benchmark("smoke", R"({"repeat":1})");
        store.add_benchmark_result(benchmark_id, run_id, 0,
                                   "tokens_per_second", 12.5);

        QuantumJobRecord quantum;
        quantum.platform_job_id = "test-originqc-job";
        quantum.backend = "WK_C180";
        quantum.programs = 2;
        quantum.shots_per_program = 10;
        quantum.status = "FINISHED";
        quantum.counts_json = R"({"0":8,"1":12})";
        quantum.probs_json = R"([{"0":0.4,"1":0.6},{"0":0.6,"1":0.4}])";
        quantum.qpu_runtime_ms = 47.0;
        quantum.derived_seed = 12345;
        quantum.run_id = run_id;
        const std::int64_t quantum_job_id =
            store.record_quantum_job(quantum);
        CHECK(quantum_job_id > 0, "创建 quantum job");

        QuantumSampleRecord sample;
        sample.quantum_job_id = quantum_job_id;
        sample.program_index = 0;
        sample.bitstring = "0";
        sample.count = 4;
        store.add_quantum_sample(sample);

        const std::int64_t conversation_id =
            store.create_conversation(run_id, "何为生命第一因？", "roundtable");
        CHECK(conversation_id > 0, "创建 conversation");

        ConversationMessageRecord opening;
        opening.conversation_id = conversation_id;
        opening.round_no = 1;
        opening.turn_no = 0;
        opening.speaker_agent = 0;
        opening.phase = "opening";
        opening.content = "生命的第一因是同理。";
        const std::int64_t opening_id =
            store.add_conversation_message(opening);

        ConversationMessageRecord rebuttal;
        rebuttal.conversation_id = conversation_id;
        rebuttal.round_no = 1;
        rebuttal.turn_no = 1;
        rebuttal.speaker_agent = 1;
        rebuttal.target_agent = 0;
        rebuttal.reply_to_message_id = opening_id;
        rebuttal.phase = "rebuttal";
        rebuttal.content = "我回应 Agent 0：秩序同样能保全生命。";
        CHECK(store.add_conversation_message(rebuttal) > 0,
              "创建 conversation message");
        store.finish_conversation(conversation_id);

        CHECK(store.scalar("SELECT COUNT(*) FROM runs WHERE run_id=$1::bigint",
                           {std::to_string(run_id)}) == 1, "run 可查询");
        CHECK(store.scalar("SELECT COUNT(*) FROM agents WHERE run_id=$1::bigint",
                           {std::to_string(run_id)}) == 1, "agent 可查询");
        CHECK(store.scalar("SELECT COUNT(*) FROM events WHERE run_id=$1::bigint",
                           {std::to_string(run_id)}) == 1, "event 可查询");
        CHECK(store.scalar("SELECT COUNT(*) FROM metrics WHERE run_id=$1::bigint",
                           {std::to_string(run_id)}) == 1, "metric 可查询");
        CHECK(store.scalar(
                  "SELECT COUNT(*) FROM benchmark_results WHERE benchmark_id=$1::bigint",
                  {std::to_string(benchmark_id)}) == 1, "benchmark result 可查询");
        CHECK(store.scalar(
                  "SELECT COUNT(*) FROM quantum_jobs WHERE quantum_job_id=$1::bigint",
                  {std::to_string(quantum_job_id)}) == 1, "quantum job 可查询");
        CHECK(store.scalar(
                  "SELECT COUNT(*) FROM quantum_samples WHERE quantum_job_id=$1::bigint",
                  {std::to_string(quantum_job_id)}) == 1, "quantum sample 可查询");
        CHECK(store.scalar(
                  "SELECT COUNT(*) FROM conversation_messages "
                  "WHERE conversation_id=$1::bigint",
                  {std::to_string(conversation_id)}) == 2,
              "conversation messages 可查询");
        CHECK(store.scalar(
                  "SELECT COUNT(*) FROM conversations "
                  "WHERE conversation_id=$1::bigint AND finished_at IS NOT NULL",
                  {std::to_string(conversation_id)}) == 1,
              "conversation 可查询");
        CHECK(store.scalar(
                  "SELECT COUNT(*) FROM runs WHERE run_id=$1::bigint "
                  "AND seed_source='originqc'",
                  {std::to_string(run_id)}) == 1, "run seed_source 可查询");

        store.finish_run(run_id, "completed");

        // 清理测试数据（级联删除 agent/round/event/metric/result）
        store.execute("DELETE FROM benchmarks WHERE benchmark_id=$1::bigint",
                      {std::to_string(benchmark_id)});
        store.execute("DELETE FROM quantum_jobs WHERE quantum_job_id=$1::bigint",
                      {std::to_string(quantum_job_id)});
        store.execute("DELETE FROM runs WHERE run_id=$1::bigint",
                      {std::to_string(run_id)});

        // 多连接并发写入：验证每个线程独立 libpq 连接时的隔离性。
        std::atomic<int> concurrent_failures{0};
        std::vector<std::thread> workers;
        for (int i = 0; i < 4; ++i) {
            workers.emplace_back([&, i]() {
                try {
                    PostgresStore local;
                    if (!local.connect(dsn_env)) {
                        ++concurrent_failures;
                        return;
                    }
                    RunRecord r;
                    r.seed = 100 + i;
                    r.config_json = R"({"concurrent":true})";
                    const std::int64_t id = local.create_run(r);
                    EventRecord e;
                    e.run_id = id;
                    e.kind = "concurrent_write";
                    local.log_event(e);
                    local.finish_run(id, "completed");
                } catch (...) {
                    ++concurrent_failures;
                }
            });
        }
        for (auto& t : workers) t.join();
        CHECK(concurrent_failures.load() == 0, "4 连接并发写入");
        CHECK(store.scalar(
                  "SELECT COUNT(*) FROM runs WHERE seed >= 100 AND seed < 104") == 4,
              "并发 run 数量");
        store.execute("DELETE FROM runs WHERE seed >= 100 AND seed < 104");
    } catch (const std::exception& e) {
        std::cerr << "  [FAIL] " << e.what() << std::endl;
        return 1;
    }

    std::cout << "\n结果：" << (g_failed == 0 ? "全部通过" : "存在失败")
              << std::endl;
    return g_failed == 0 ? 0 : 1;
}
