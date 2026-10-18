#pragma once
#include "../com/ichannel.hpp"

#ifdef CIV_HAS_VERBS
#include <infiniband/verbs.h>
#include <rdma/rdma_cma.h>
#endif

#include <string>
#include <vector>

namespace civ {

    class RoCEv2Channel : public IChannel {
    public:
        struct Config {
            std::uint16_t port = 4791;
            bool          is_server = false;
            std::string   host = "127.0.0.1";
            std::size_t   buf_size = 1 << 20;
        };

        explicit RoCEv2Channel(const Config& cfg);
        ~RoCEv2Channel() override;

        void send(ByteView data) override;
        Bytes recv() override;
        void close() noexcept override;
        std::string name() const override { return "RoCEv2Channel"; }

    private:
#ifdef CIV_HAS_VERBS
        void setup_verbs();
        void connect_qp();
        void post_recv();
        union ibv_gid select_gid(ibv_context* ctx, uint8_t port);

        rdma_event_channel* cm_channel_ = nullptr;
        rdma_cm_id*         listen_id_  = nullptr;  // 服务端监听 id（不能被子连接覆盖）
        rdma_cm_id*         cm_id_      = nullptr;
        ibv_pd*             pd_         = nullptr;
        ibv_cq*             cq_         = nullptr;
        ibv_qp*             qp_         = nullptr;
        ibv_mr*             mr_         = nullptr;

        void*       buf_      = nullptr;
        std::size_t buf_size_ = 0;

        std::uint32_t local_psn_  = 0;
        std::uint32_t remote_qpn_ = 0;
        std::uint32_t remote_psn_ = 0;
        union ibv_gid local_gid_{};
        union ibv_gid remote_gid_{};
#endif
    };

} // namespace civ
