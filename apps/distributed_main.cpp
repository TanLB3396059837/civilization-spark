#include "civ/civilization/simulation.hpp"
#include "civ/channel/rocev2_channel.hpp"
#include <iostream>
#include <cstdlib>
#include <vector>

int main(int argc, char* argv[]) {
    if (argc < 3) {
        std::cerr << "用法: " << argv[0] << " <role> <port> [host]\n";
        std::cerr << "  role: server | client\n";
        return 1;
    }

    std::string role = argv[1];
    std::uint16_t port = static_cast<std::uint16_t>(std::atoi(argv[2]));
    std::string host = (argc >= 4) ? argv[3] : "127.0.0.1";

    std::cout << "======== 分布式文明火种 ========\n";
    std::cout << "角色: " << role << ", 端口: " << port << "\n";

    try {
#ifdef CIV_HAS_VERBS
        civ::RoCEv2Channel::Config cfg{};
        cfg.port = port;
        cfg.is_server = (role == "server");
        cfg.host = host;

        civ::RoCEv2Channel ch(cfg);
        std::cout << "RDMA 连接建立\n";

        if (cfg.is_server) {
            auto data = ch.recv();
            std::cout << "收到 " << data.size() << " 字节\n";
        } else {
            std::vector<civ::Byte> data(64, 0xAB);
            ch.send(data);
            std::cout << "发送 " << data.size() << " 字节\n";
        }
#else
        std::cout << "RDMA 不可用\n";
#endif
    } catch (const std::exception& e) {
        std::cerr << "错误: " << e.what() << "\n";
        return 1;
    }

    return 0;
}