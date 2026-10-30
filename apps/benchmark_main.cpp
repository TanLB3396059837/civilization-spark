#include "civ/civilization/simulation.hpp"
#include "civ/ai/batch_scheduler.hpp"
#ifdef CIV_HAS_POSTGRES
#include "civ/storage/postgres_store.hpp"
#endif

#include <algorithm>
#include <chrono>
#include <cstdlib>
#include <iostream>
#include <memory>
#include <string>
#include <vector>

namespace {

std::vector<std::uint64_t> parse_seeds() {
    std::vector<std::uint64_t> seeds;
    if (const char* text = std::getenv("CIV_BENCH_SEEDS")) {
        std::string s(text);
        std::size_t pos = 0;
        while (pos <= s.size()) {
            const std::size_t comma = s.find(',', pos);
            const std::string part =
                s.substr(pos, comma == std::string::npos
                                  ? std::string::npos : comma - pos);
            if (!part.empty())
                seeds.push_back(std::strtoull(part.c_str(), nullptr, 10));
            if (comma == std::string::npos) break;
            pos = comma + 1;
        }
    }
    if (seeds.empty()) seeds = {42, 43, 44};
    return seeds;
}

} // namespace

int main() {
    const std::vector<std::uint64_t> seeds = parse_seeds();

    int repeat = 1;
    if (const char* text = std::getenv("CIV_BENCH_REPEAT"))
        repeat = std::max(1, std::atoi(text));

#ifdef CIV_HAS_POSTGRES
    std::unique_ptr<civ::PostgresStore> store;
    std::int64_t benchmark_id = -1;
    if (const char* dsn = std::getenv("CIV_PG_DSN"); dsn && *dsn) {
        try {
            store = std::make_unique<civ::PostgresStore>();
            if (store->connect(dsn)) {
                store->migrate();
                benchmark_id = store->create_benchmark(
                    "civ_bench",
                    "{\"seeds\":" + std::to_string(seeds.size()) +
                        ",\"repeat\":" + std::to_string(repeat) + "}");
            } else {
                std::cerr << "[bench] PostgreSQL: " << store->last_error()
                          << std::endl;
                store.reset();
            }
        } catch (const std::exception& e) {
            std::cerr << "[bench] PostgreSQL: " << e.what() << std::endl;
            store.reset();
        }
    }
#endif

    std::cout << "seed,repeat,wall_ms,rounds,survivors\n";
    for (std::uint64_t seed : seeds) {
        for (int r = 0; r < repeat; ++r) {
            const auto start = std::chrono::steady_clock::now();
            civ::Simulation sim(seed);
            sim.run_full_simulation();
            const auto end = std::chrono::steady_clock::now();
            const double wall_ms =
                std::chrono::duration<double, std::milli>(end - start).count();

            std::cout << seed << "," << (r + 1) << "," << wall_ms << ","
                      << sim.current_round() << ","
                      << sim.surviving_agents() << "\n";
            std::cout.flush();

#ifdef CIV_HAS_POSTGRES
            if (store && benchmark_id > 0) {
                try {
                    const std::int64_t run_id = store->scalar(
                        "SELECT run_id FROM runs WHERE seed=$1::bigint "
                        "ORDER BY run_id DESC LIMIT 1",
                        {std::to_string(seed)});
                    if (run_id > 0) {
                        store->add_benchmark_result(
                            benchmark_id, run_id, -1, "wall_ms", wall_ms,
                            "{\"repeat\":" + std::to_string(r + 1) + "}");
                        store->add_benchmark_result(
                            benchmark_id, run_id, -1, "survivors",
                            static_cast<double>(sim.surviving_agents()));
                    }
                } catch (const std::exception& e) {
                    std::cerr << "[bench] result 写入失败: " << e.what()
                              << std::endl;
                }
            }
#endif
            sim.shutdown();
        }
    }
    if (civ::SharedModelPool::available())
        civ::SharedModelPool::shutdown_active();
    return 0;
}
