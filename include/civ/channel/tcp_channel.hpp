#pragma once
#include "../com/ichannel.hpp"

#ifdef _WIN32
    #include <winsock2.h>
    using SocketHandle = SOCKET;
#else
using SocketHandle = int;
#endif

namespace civ {

    class TcpChannel : public IChannel {
    public:
        struct Config {
            std::uint16_t port = 18000;
            bool          is_server = false;
            std::string   host = "127.0.0.1";
            // 防止对端发送超大长度字段导致无界内存分配
            std::uint32_t max_frame_size = 64u * 1024u * 1024u;
        };

        explicit TcpChannel(const Config& cfg);
        ~TcpChannel() override;

        void send(ByteView data) override;
        Bytes recv() override;
        void close() noexcept override;
        std::string name() const override { return "TcpChannel"; }

    private:
        SocketHandle sock_;
        std::uint32_t max_frame_size_ = 64u * 1024u * 1024u;
    };

} // namespace civ
