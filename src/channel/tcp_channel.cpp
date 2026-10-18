#include "civ/channel/tcp_channel.hpp"
#include <stdexcept>
#include <cstring>
#include <mutex>
#include <limits>

#ifdef _WIN32
    #include <ws2tcpip.h>
#else
    #include <sys/socket.h>
    #include <netinet/in.h>
    #include <netinet/tcp.h>
    #include <arpa/inet.h>
    #include <unistd.h>
#endif

namespace civ {

#ifdef _WIN32
static std::once_flag g_wsa_once;
static void ensure_wsa() {
    std::call_once(g_wsa_once, []() {
        WSADATA wsa;
        WSAStartup(MAKEWORD(2, 2), &wsa);
    });
}

static bool socket_valid(SocketHandle s) noexcept {
    return s != INVALID_SOCKET;
}
#else
static bool socket_valid(SocketHandle s) noexcept {
    return s >= 0;
}
#endif

TcpChannel::TcpChannel(const Config& cfg)
    : max_frame_size_(cfg.max_frame_size) {
#ifdef _WIN32
    ensure_wsa();
    sock_ = INVALID_SOCKET;
#else
    sock_ = -1;
#endif
    try {
        sock_ = socket(AF_INET, SOCK_STREAM, 0);
        if (!socket_valid(sock_)) throw std::runtime_error("socket failed");

        int one = 1;
        setsockopt(sock_, SOL_SOCKET, SO_REUSEADDR,
                   reinterpret_cast<const char*>(&one), sizeof(one));
        setsockopt(sock_, IPPROTO_TCP, TCP_NODELAY,
                   reinterpret_cast<const char*>(&one), sizeof(one));

        sockaddr_in addr{};
        addr.sin_family = AF_INET;
        addr.sin_port   = htons(cfg.port);

        if (cfg.is_server) {
            addr.sin_addr.s_addr = INADDR_ANY;
            if (bind(sock_, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) < 0)
                throw std::runtime_error("bind failed");
            if (listen(sock_, 1) < 0) throw std::runtime_error("listen failed");

            // 关闭监听 fd 避免泄漏
            SocketHandle listen_fd = sock_;
            sock_ = accept(listen_fd, nullptr, nullptr);
#ifdef _WIN32
            closesocket(listen_fd);
#else
            ::close(listen_fd);
#endif
            if (!socket_valid(sock_)) throw std::runtime_error("accept failed");
        } else {
            if (inet_pton(AF_INET, cfg.host.c_str(), &addr.sin_addr) != 1)
                throw std::runtime_error("invalid host address");
            if (connect(sock_, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) < 0)
                throw std::runtime_error("connect failed");
        }
    } catch (...) {
        close();
        throw;
    }
}

TcpChannel::~TcpChannel() { close(); }

void TcpChannel::send(ByteView data) {
    if (data.size() > max_frame_size_ ||
        data.size() > std::numeric_limits<std::uint32_t>::max())
        throw std::runtime_error("frame too large");

    std::uint32_t n = static_cast<std::uint32_t>(data.size());
    n = htonl(n);
    const char* p = reinterpret_cast<const char*>(&n);
    std::size_t left = 4;
    while (left) {
#ifdef _WIN32
        int w = ::send(sock_, p, static_cast<int>(left), 0);
#else
        #ifdef MSG_NOSIGNAL
        ssize_t w = ::send(sock_, p, left, MSG_NOSIGNAL);
        #else
        ssize_t w = ::write(sock_, p, left);
        #endif
#endif
        if (w <= 0) throw std::runtime_error("send failed");
        p += w; left -= static_cast<std::size_t>(w);
    }
    p = reinterpret_cast<const char*>(data.data());
    left = data.size();
    while (left) {
#ifdef _WIN32
        int w = ::send(sock_, p, static_cast<int>(left), 0);
#else
        #ifdef MSG_NOSIGNAL
        ssize_t w = ::send(sock_, p, left, MSG_NOSIGNAL);
        #else
        ssize_t w = ::write(sock_, p, left);
        #endif
#endif
        if (w <= 0) throw std::runtime_error("send failed");
        p += w; left -= static_cast<std::size_t>(w);
    }
}

Bytes TcpChannel::recv() {
    std::uint32_t n = 0;
    char* p = reinterpret_cast<char*>(&n);
    std::size_t left = 4;
    while (left) {
#ifdef _WIN32
        int r = ::recv(sock_, p, static_cast<int>(left), 0);
#else
        ssize_t r = ::recv(sock_, p, left, 0);
#endif
        if (r <= 0) throw std::runtime_error("recv failed");
        p += r; left -= static_cast<std::size_t>(r);
    }
    n = ntohl(n);
    if (n > max_frame_size_)
        throw std::runtime_error("incoming frame too large");
    Bytes out(n);
    p = reinterpret_cast<char*>(out.data());
    left = n;
    while (left) {
#ifdef _WIN32
        int r = ::recv(sock_, p, static_cast<int>(left), 0);
#else
        ssize_t r = ::recv(sock_, p, left, 0);
#endif
        if (r <= 0) throw std::runtime_error("recv failed");
        p += r; left -= static_cast<std::size_t>(r);
    }
    return out;
}

void TcpChannel::close() noexcept {
#ifdef _WIN32
    if (sock_ != INVALID_SOCKET) { closesocket(sock_); sock_ = INVALID_SOCKET; }
#else
    if (sock_ >= 0) { ::close(sock_); sock_ = -1; }
#endif
}

} // namespace civ
