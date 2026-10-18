#pragma once
#include "../com/ichannel.hpp"
#include <memory>
#include <array>
#include <atomic>

namespace civ {

    class MultiChannel : public IChannel {
    public:
        static constexpr std::size_t NUM_CH = 4;

        explicit MultiChannel(
            std::array<std::unique_ptr<IChannel>, NUM_CH> channels);
        ~MultiChannel() override;

        void send(ByteView data) override;
        Bytes recv() override;
        void close() noexcept override;
        std::string name() const override { return "MultiChannel"; }

    private:
        std::array<std::unique_ptr<IChannel>, NUM_CH> channels_;
        std::atomic<std::size_t> send_idx_{0};
        std::size_t recv_idx_ = 0;
    };

} // namespace civ