#include "civ/channel/multi_channel.hpp"

namespace civ {

    MultiChannel::MultiChannel(
        std::array<std::unique_ptr<IChannel>, NUM_CH> channels)
        : channels_(std::move(channels)) {}

    MultiChannel::~MultiChannel() = default;

    void MultiChannel::send(ByteView data) {
        auto idx = send_idx_.fetch_add(1, std::memory_order_relaxed) % NUM_CH;
        channels_[idx]->send(data);
    }

    Bytes MultiChannel::recv() {
        auto r = channels_[recv_idx_]->recv();
        recv_idx_ = (recv_idx_ + 1) % NUM_CH;
        return r;
    }

    void MultiChannel::close() noexcept {
        for (auto& c : channels_) if (c) c->close();
    }

} // namespace civ