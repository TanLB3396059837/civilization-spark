#pragma once
#include "../com/ichannel.hpp"
#include <atomic>
#include <string>

#ifdef _WIN32
    #define WIN32_LEAN_AND_MEAN
    #include <windows.h>
#endif

namespace civ {

    class ShmChannel : public IChannel {
    public:
        ShmChannel(const std::string& name, bool create,
                   std::size_t size = 1 << 20);
        ~ShmChannel() override;

        void send(ByteView data) override;
        Bytes recv() override;
        void close() noexcept override;
        std::string name() const override { return "ShmChannel"; }

    private:
        struct alignas(64) Header {
            std::atomic<std::uint64_t> write_seq{0};
            std::atomic<std::uint64_t> read_seq{0};
            std::atomic<std::uint32_t> frame_len{0};
            std::uint32_t pad{0};
        };

        std::string name_;
        std::size_t size_;
        bool    creator_ = false;
        void*   ptr_  = nullptr;
        Header* hdr_  = nullptr;
        Byte*   data_ = nullptr;

#ifdef _WIN32
        HANDLE hMap_ = nullptr;
#else
        int fd_ = -1;
#endif
    };

} // namespace civ
