#include "civ/civilization/simulation.hpp"
#include <iostream>
#include <cstdlib>
#include <cstring>

int main(int argc, char* argv[]) {
    if (argc >= 2 &&
        (std::strcmp(argv[1], "-h") == 0 ||
         std::strcmp(argv[1], "--help") == 0)) {
        std::cout
            << "用法: civ_main [seed]\n\n"
            << "环境变量:\n"
            << "  CIV_LLAMA_MODEL    GGUF 模型路径（不设置则走内置 CPU 演示）\n"
            << "  CIV_N_CTX          每个 Agent 的上下文长度（默认 2048）\n"
            << "  CIV_N_GPU_LAYERS   offload 到 GPU 的层数（默认 CPU=0，CUDA=99）\n"
            << "  CIV_DEBATE_MODE    parallel（默认）或 roundtable 真正圆桌对话\n"
            << "  CIV_MAX_ROUNDS     最大轮数（默认 7）\n"
            << "  CIV_ROUNDTABLE_TOPIC  圆桌议题（Windows 建议用默认值）\n"
            << "  CIV_PG_DSN         PostgreSQL 连接串，例如\n"
            << "                     host=127.0.0.1 port=5432 dbname=civ user=civ password=civpass\n";
        return 0;
    }

    std::uint64_t seed = 42;
    if (argc >= 2)
        seed = static_cast<std::uint64_t>(
            std::strtoull(argv[1], nullptr, 10));

    std::srand(static_cast<unsigned>(seed));

    civ::Simulation sim(seed);
    sim.run_full_simulation();
    return 0;
}
