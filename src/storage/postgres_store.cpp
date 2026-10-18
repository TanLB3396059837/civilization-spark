#include "civ/storage/postgres_store.hpp"

#include <libpq-fe.h>

#include <stdexcept>

namespace civ {

namespace {

const char* const kSchemaStatements[] = {
R"SQL(
CREATE TABLE IF NOT EXISTS runs (
    run_id        BIGSERIAL PRIMARY KEY,
    seed          BIGINT NOT NULL,
    model_path    TEXT,
    model_sha256  CHAR(64),
    quant         TEXT,
    n_ctx         INTEGER NOT NULL DEFAULT 2048,
    n_gpu_layers  INTEGER NOT NULL DEFAULT 0,
    config        JSONB NOT NULL DEFAULT '{}'::jsonb,
    git_commit    TEXT,
    status        TEXT NOT NULL DEFAULT 'running',
    error         TEXT,
    started_at    TIMESTAMPTZ NOT NULL DEFAULT now(),
    finished_at   TIMESTAMPTZ
)
)SQL",
R"SQL(
CREATE TABLE IF NOT EXISTS agents (
    agent_id          BIGSERIAL PRIMARY KEY,
    run_id            BIGINT NOT NULL REFERENCES runs(run_id) ON DELETE CASCADE,
    agent_index       INTEGER NOT NULL,
    factor            TEXT NOT NULL,
    heir              TEXT NOT NULL,
    titan_path        TEXT NOT NULL,
    drive             TEXT NOT NULL,
    simulated_path    TEXT NOT NULL,
    ai_route          TEXT NOT NULL,
    evolution_method  TEXT NOT NULL,
    UNIQUE (run_id, agent_index)
)
)SQL",
R"SQL(
CREATE TABLE IF NOT EXISTS rounds (
    round_id     BIGSERIAL PRIMARY KEY,
    run_id       BIGINT NOT NULL REFERENCES runs(run_id) ON DELETE CASCADE,
    round_no     INTEGER NOT NULL,
    phase        TEXT NOT NULL,
    started_at   TIMESTAMPTZ NOT NULL DEFAULT now(),
    finished_at  TIMESTAMPTZ,
    UNIQUE (run_id, round_no)
)
)SQL",
R"SQL(
CREATE TABLE IF NOT EXISTS events (
    event_id     BIGSERIAL PRIMARY KEY,
    run_id       BIGINT NOT NULL REFERENCES runs(run_id) ON DELETE CASCADE,
    round_no     INTEGER,
    agent_index  INTEGER,
    kind         TEXT NOT NULL,
    payload      JSONB NOT NULL DEFAULT '{}'::jsonb,
    created_at   TIMESTAMPTZ NOT NULL DEFAULT now()
)
)SQL",
R"SQL(
CREATE INDEX IF NOT EXISTS events_run_round_idx
    ON events (run_id, round_no, event_id)
)SQL",
R"SQL(
CREATE TABLE IF NOT EXISTS metrics (
    metric_id    BIGSERIAL PRIMARY KEY,
    run_id       BIGINT NOT NULL REFERENCES runs(run_id) ON DELETE CASCADE,
    round_no     INTEGER,
    agent_index  INTEGER,
    name         TEXT NOT NULL,
    value        DOUBLE PRECISION NOT NULL,
    unit         TEXT,
    extra        JSONB NOT NULL DEFAULT '{}'::jsonb,
    created_at   TIMESTAMPTZ NOT NULL DEFAULT now()
)
)SQL",
R"SQL(
CREATE INDEX IF NOT EXISTS metrics_run_round_idx
    ON metrics (run_id, round_no, name)
)SQL",
R"SQL(
CREATE TABLE IF NOT EXISTS benchmarks (
    benchmark_id  BIGSERIAL PRIMARY KEY,
    name          TEXT NOT NULL,
    config        JSONB NOT NULL DEFAULT '{}'::jsonb,
    created_at    TIMESTAMPTZ NOT NULL DEFAULT now()
)
)SQL",
R"SQL(
CREATE TABLE IF NOT EXISTS benchmark_results (
    result_id     BIGSERIAL PRIMARY KEY,
    benchmark_id  BIGINT NOT NULL REFERENCES benchmarks(benchmark_id) ON DELETE CASCADE,
    run_id        BIGINT REFERENCES runs(run_id) ON DELETE CASCADE,
    agent_index   INTEGER,
    metric        TEXT NOT NULL,
    value         DOUBLE PRECISION,
    extra         JSONB NOT NULL DEFAULT '{}'::jsonb,
    created_at    TIMESTAMPTZ NOT NULL DEFAULT now()
)
)SQL",
R"SQL(
CREATE INDEX IF NOT EXISTS benchmark_results_benchmark_idx
    ON benchmark_results (benchmark_id, metric)
)SQL",
};

} // namespace

PostgresStore::~PostgresStore() {
    if (conn_) {
        PQfinish(static_cast<PGconn*>(conn_));
        conn_ = nullptr;
    }
}

bool PostgresStore::connect(const std::string& dsn) {
    if (conn_) {
        PQfinish(static_cast<PGconn*>(conn_));
        conn_ = nullptr;
    }

    PGconn* conn = PQconnectdb(dsn.c_str());
    if (!conn || PQstatus(conn) != CONNECTION_OK) {
        last_error_ = conn ? PQerrorMessage(conn) : "PQconnectdb failed";
        if (conn) PQfinish(conn);
        conn_ = nullptr;
        return false;
    }

    conn_ = conn;
    if (PQsetClientEncoding(conn, "UTF8") != 0) {
        last_error_ = PQerrorMessage(conn);
        PQfinish(conn);
        conn_ = nullptr;
        return false;
    }
    last_error_.clear();
    return true;
}

void PostgresStore::migrate() {
    for (const char* sql : kSchemaStatements) {
        exec_command(sql, {});
    }
}

void* PostgresStore::exec_params(const std::string& sql,
                                 const std::vector<std::string>& params,
                                 bool expect_rows) {
    if (!conn_) throw std::runtime_error("PostgreSQL not connected");

    std::vector<const char*> values;
    values.reserve(params.size());
    for (const auto& p : params) values.push_back(p.c_str());

    PGresult* result = PQexecParams(
        static_cast<PGconn*>(conn_), sql.c_str(),
        static_cast<int>(params.size()), nullptr,
        values.empty() ? nullptr : values.data(),
        nullptr, nullptr, 0);

    if (!result) {
        last_error_ = PQerrorMessage(static_cast<PGconn*>(conn_));
        throw std::runtime_error(last_error_);
    }

    const ExecStatusType status = PQresultStatus(result);
    const bool ok = expect_rows ? (status == PGRES_TUPLES_OK)
                                : (status == PGRES_COMMAND_OK ||
                                   status == PGRES_TUPLES_OK);
    if (!ok) {
        last_error_ = PQresultErrorMessage(result);
        PQclear(result);
        throw std::runtime_error(last_error_);
    }
    return result;
}

std::int64_t PostgresStore::exec_id(const std::string& sql,
                                    const std::vector<std::string>& params) {
    PGresult* result =
        static_cast<PGresult*>(exec_params(sql, params, true));
    std::int64_t id = -1;
    if (PQntuples(result) > 0 && PQnfields(result) > 0)
        id = std::stoll(PQgetvalue(result, 0, 0));
    PQclear(result);
    return id;
}

void PostgresStore::exec_command(const std::string& sql,
                                 const std::vector<std::string>& params) {
    PGresult* result =
        static_cast<PGresult*>(exec_params(sql, params, false));
    PQclear(result);
}

std::int64_t PostgresStore::create_run(const RunRecord& run) {
    return exec_id(
        "INSERT INTO runs "
        "(seed, model_path, model_sha256, quant, n_ctx, n_gpu_layers, config, git_commit) "
        "VALUES ($1::bigint, NULLIF($2,''), NULLIF($3,''), NULLIF($4,''), "
        "$5::int, $6::int, $7::jsonb, NULLIF($8,'')) RETURNING run_id",
        {std::to_string(run.seed), run.model_path, run.model_sha256,
         run.quant, std::to_string(run.n_ctx),
         std::to_string(run.n_gpu_layers), run.config_json, run.git_commit});
}

void PostgresStore::finish_run(std::int64_t run_id, const std::string& status,
                               const std::string& error) {
    exec_command(
        "UPDATE runs SET status=$2, error=NULLIF($3,''), finished_at=now() "
        "WHERE run_id=$1::bigint",
        {std::to_string(run_id), status, error});
}

std::int64_t PostgresStore::add_agent(std::int64_t run_id,
                                      const AgentRecord& agent) {
    return exec_id(
        "INSERT INTO agents "
        "(run_id, agent_index, factor, heir, titan_path, drive, "
        " simulated_path, ai_route, evolution_method) "
        "VALUES ($1::bigint, $2::int, $3, $4, $5, $6, $7, $8, $9) "
        "ON CONFLICT (run_id, agent_index) DO UPDATE SET "
        "factor=EXCLUDED.factor, heir=EXCLUDED.heir, "
        "titan_path=EXCLUDED.titan_path, drive=EXCLUDED.drive, "
        "simulated_path=EXCLUDED.simulated_path, "
        "ai_route=EXCLUDED.ai_route, "
        "evolution_method=EXCLUDED.evolution_method "
        "RETURNING agent_id",
        {std::to_string(run_id), std::to_string(agent.agent_index),
         agent.factor, agent.heir, agent.titan_path, agent.drive,
         agent.simulated_path, agent.ai_route, agent.evolution_method});
}

std::int64_t PostgresStore::start_round(std::int64_t run_id, int round_no,
                                        const std::string& phase) {
    return exec_id(
        "INSERT INTO rounds (run_id, round_no, phase) "
        "VALUES ($1::bigint, $2::int, $3) "
        "ON CONFLICT (run_id, round_no) DO UPDATE SET "
        "phase=EXCLUDED.phase, started_at=now(), finished_at=NULL "
        "RETURNING round_id",
        {std::to_string(run_id), std::to_string(round_no), phase});
}

void PostgresStore::finish_round(std::int64_t round_id) {
    exec_command("UPDATE rounds SET finished_at=now() WHERE round_id=$1::bigint",
                 {std::to_string(round_id)});
}

void PostgresStore::log_event(const EventRecord& event) {
    exec_command(
        "INSERT INTO events (run_id, round_no, agent_index, kind, payload) "
        "VALUES ($1::bigint, NULLIF($2::int,-1), NULLIF($3::int,-1), $4, $5::jsonb)",
        {std::to_string(event.run_id), std::to_string(event.round_no),
         std::to_string(event.agent_index), event.kind, event.payload_json});
}

void PostgresStore::log_metric(const MetricRecord& metric) {
    exec_command(
        "INSERT INTO metrics "
        "(run_id, round_no, agent_index, name, value, unit, extra) "
        "VALUES ($1::bigint, NULLIF($2::int,-1), NULLIF($3::int,-1), "
        "$4, $5::double precision, NULLIF($6,''), $7::jsonb)",
        {std::to_string(metric.run_id), std::to_string(metric.round_no),
         std::to_string(metric.agent_index), metric.name,
         std::to_string(metric.value), metric.unit, metric.extra_json});
}

std::int64_t PostgresStore::create_benchmark(const std::string& name,
                                             const std::string& config_json) {
    return exec_id(
        "INSERT INTO benchmarks (name, config) VALUES ($1, $2::jsonb) "
        "RETURNING benchmark_id",
        {name, config_json});
}

void PostgresStore::add_benchmark_result(
    std::int64_t benchmark_id, std::int64_t run_id, int agent_index,
    const std::string& metric, double value, const std::string& extra_json) {
    exec_command(
        "INSERT INTO benchmark_results "
        "(benchmark_id, run_id, agent_index, metric, value, extra) "
        "VALUES ($1::bigint, NULLIF($2::bigint,-1), NULLIF($3::int,-1), "
        "$4, $5::double precision, $6::jsonb)",
        {std::to_string(benchmark_id), std::to_string(run_id),
         std::to_string(agent_index), metric,
         std::to_string(value), extra_json});
}

std::int64_t PostgresStore::scalar(const std::string& sql,
                                   const std::vector<std::string>& params) {
    PGresult* result =
        static_cast<PGresult*>(exec_params(sql, params, true));
    std::int64_t value = 0;
    if (PQntuples(result) > 0 && PQnfields(result) > 0)
        value = std::stoll(PQgetvalue(result, 0, 0));
    PQclear(result);
    return value;
}

void PostgresStore::execute(const std::string& sql,
                            const std::vector<std::string>& params) {
    exec_command(sql, params);
}

} // namespace civ
