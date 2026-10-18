#include "civ/channel/rocev2_channel.hpp"

#ifdef CIV_HAS_VERBS

#include <cstring>
#include <cstdio>
#include <cstdlib>
#include <cerrno>
#include <stdexcept>
#include <arpa/inet.h>
#include <netinet/in.h>
#include <netdb.h>
#include <unistd.h>

namespace civ {

// RDMA 错误用返回值，不用 errno
[[noreturn]] static void throw_rdma(const char* msg, int err) {
    const int e = (err < 0 && errno != 0) ? errno : (err < 0 ? -err : err);
    throw std::runtime_error(
        std::string("RDMA: ") + msg + ": " + std::strerror(e));
}

#define CHECK_RDMA(expr, msg) \
    do { int _r = (expr); if (_r) throw_rdma(msg, _r); } while (0)

// QP 信息交换结构体（26 字节 < 56 字节 private_data 限制）
#pragma pack(push, 1)
struct QPInfo {
    std::uint32_t qpn;
    std::uint32_t psn;
    std::uint16_t lid;
    std::uint8_t  gid[16];
};
#pragma pack(pop)

RoCEv2Channel::RoCEv2Channel(const Config& cfg) {
    buf_size_ = cfg.buf_size;
    if (buf_size_ == 0) buf_size_ = 1 << 20;
    // aligned_alloc 要求 size 是 alignment 的整数倍
    buf_size_ = (buf_size_ + 4095u) & ~static_cast<std::size_t>(4095u);

    try {
    cm_channel_ = rdma_create_event_channel();
    if (!cm_channel_) throw std::runtime_error("rdma_create_event_channel failed");

    CHECK_RDMA(rdma_create_id(cm_channel_, &cm_id_, nullptr, RDMA_PS_TCP),
               "rdma_create_id");

    if (cfg.is_server) {
        listen_id_ = cm_id_;
        sockaddr_in addr{};
        addr.sin_family      = AF_INET;
        addr.sin_port        = htons(cfg.port);
        addr.sin_addr.s_addr = INADDR_ANY;
        CHECK_RDMA(rdma_bind_addr(cm_id_,
            reinterpret_cast<sockaddr*>(&addr)), "rdma_bind_addr");
        CHECK_RDMA(rdma_listen(cm_id_, 8), "rdma_listen");

        rdma_cm_event* event = nullptr;
        CHECK_RDMA(rdma_get_cm_event(cm_channel_, &event), "get_cm_event");
        if (event->event != RDMA_CM_EVENT_CONNECT_REQUEST) {
            rdma_ack_cm_event(event);
            throw std::runtime_error("expected CONNECT_REQUEST");
        }
        cm_id_ = event->id;
        rdma_ack_cm_event(event);

        setup_verbs();

        // 通过 private_data 交换 QP 信息
        QPInfo local_info{};
        local_info.qpn = qp_->qp_num;
        local_info.psn = local_psn_;
        local_info.lid = 0;
        std::memcpy(local_info.gid, local_gid_.raw, 16);

        rdma_conn_param cp{};
        cp.responder_resources = 1;
        cp.initiator_depth     = 1;
        cp.retry_count         = 7;
        cp.rnr_retry_count     = 7;
        cp.private_data        = &local_info;
        cp.private_data_len    = sizeof(local_info);

        CHECK_RDMA(rdma_accept(cm_id_, &cp), "rdma_accept");

        CHECK_RDMA(rdma_get_cm_event(cm_channel_, &event), "get_cm_event");
        if (event->event != RDMA_CM_EVENT_ESTABLISHED) {
            rdma_ack_cm_event(event);
            throw std::runtime_error("expected ESTABLISHED");
        }
        if (event->param.conn.private_data_len >= sizeof(QPInfo)) {
            auto* peer = static_cast<const QPInfo*>(event->param.conn.private_data);
            remote_qpn_ = peer->qpn;
            remote_psn_ = peer->psn;
            std::memcpy(remote_gid_.raw, peer->gid, 16);
        }
        rdma_ack_cm_event(event);
    } else {
        addrinfo hints{};
        hints.ai_family   = AF_INET;
        hints.ai_socktype = SOCK_STREAM;
        addrinfo* res = nullptr;
        char port_str[16];
        std::snprintf(port_str, sizeof(port_str), "%u", cfg.port);
        if (getaddrinfo(cfg.host.c_str(), port_str, &hints, &res) != 0)
            throw std::runtime_error("getaddrinfo failed");

        int err = 0;
        for (auto* t = res; t; t = t->ai_next) {
            err = rdma_resolve_addr(cm_id_, nullptr, t->ai_addr, 2000);
            if (!err) break;
        }
        freeaddrinfo(res);
        CHECK_RDMA(err, "rdma_resolve_addr");

        rdma_cm_event* event = nullptr;
        CHECK_RDMA(rdma_get_cm_event(cm_channel_, &event), "get_cm_event");
        if (event->event != RDMA_CM_EVENT_ADDR_RESOLVED) {
            rdma_ack_cm_event(event);
            throw std::runtime_error("addr resolve failed");
        }
        rdma_ack_cm_event(event);

        CHECK_RDMA(rdma_resolve_route(cm_id_, 2000), "rdma_resolve_route");
        CHECK_RDMA(rdma_get_cm_event(cm_channel_, &event), "get_cm_event");
        if (event->event != RDMA_CM_EVENT_ROUTE_RESOLVED) {
            rdma_ack_cm_event(event);
            throw std::runtime_error("route resolve failed");
        }
        rdma_ack_cm_event(event);

        setup_verbs();

        QPInfo local_info{};
        local_info.qpn = qp_->qp_num;
        local_info.psn = local_psn_;
        local_info.lid = 0;
        std::memcpy(local_info.gid, local_gid_.raw, 16);

        rdma_conn_param cp{};
        cp.responder_resources = 1;
        cp.initiator_depth     = 1;
        cp.retry_count         = 7;
        cp.rnr_retry_count     = 7;
        cp.private_data        = &local_info;
        cp.private_data_len    = sizeof(local_info);

        CHECK_RDMA(rdma_connect(cm_id_, &cp), "rdma_connect");

        CHECK_RDMA(rdma_get_cm_event(cm_channel_, &event), "get_cm_event");
        if (event->event != RDMA_CM_EVENT_ESTABLISHED) {
            rdma_ack_cm_event(event);
            throw std::runtime_error("connection failed");
        }
        if (event->param.conn.private_data_len >= sizeof(QPInfo)) {
            auto* peer = static_cast<const QPInfo*>(event->param.conn.private_data);
            remote_qpn_ = peer->qpn;
            remote_psn_ = peer->psn;
            std::memcpy(remote_gid_.raw, peer->gid, 16);
        }
        rdma_ack_cm_event(event);
    }

    connect_qp();
    post_recv();
    } catch (...) {
        // 构造函数抛出时析构函数不会执行，必须在这里释放已创建的 RDMA 资源。
        close();
        throw;
    }
}

void RoCEv2Channel::setup_verbs() {
    ibv_context* ctx = cm_id_->verbs;
    uint8_t port = cm_id_->port_num;

    pd_ = ibv_alloc_pd(ctx);
    if (!pd_) throw std::runtime_error("ibv_alloc_pd failed");

    cq_ = ibv_create_cq(ctx, 64, nullptr, nullptr, 0);
    if (!cq_) throw std::runtime_error("ibv_create_cq failed");

    buf_ = std::aligned_alloc(4096, buf_size_);
    if (!buf_) throw std::runtime_error("aligned_alloc failed");

    mr_ = ibv_reg_mr(pd_, buf_, buf_size_,
                     IBV_ACCESS_LOCAL_WRITE | IBV_ACCESS_REMOTE_WRITE);
    if (!mr_) { std::free(buf_); throw std::runtime_error("ibv_reg_mr failed"); }

    ibv_qp_init_attr qa{};
    qa.send_cq          = cq_;
    qa.recv_cq          = cq_;
    qa.qp_type          = IBV_QPT_RC;
    qa.cap.max_send_wr  = 128;
    qa.cap.max_recv_wr  = 128;
    qa.cap.max_send_sge = 1;
    qa.cap.max_recv_sge = 1;

    CHECK_RDMA(rdma_create_qp(cm_id_, pd_, &qa), "rdma_create_qp");
    qp_ = cm_id_->qp;
    local_gid_ = select_gid(ctx, port);
    local_psn_ = static_cast<std::uint32_t>(std::rand()) & 0xFFFFFF;
}

union ibv_gid RoCEv2Channel::select_gid(ibv_context* ctx, uint8_t port) {
    union ibv_gid gid{};
    ibv_port_attr pa{};
    if (ibv_query_port(ctx, port, &pa))
        throw std::runtime_error("ibv_query_port failed");

    for (int i = 0; i < pa.gid_tbl_len; ++i) {
        union ibv_gid tmp{};
        if (ibv_query_gid(ctx, port, i, &tmp)) continue;
        bool is_v4 = (tmp.raw[0] == 0 && tmp.raw[1] == 0 &&
                      tmp.raw[2] == 0 && tmp.raw[3] == 0 &&
                      tmp.raw[4] == 0 && tmp.raw[5] == 0 &&
                      tmp.raw[6] == 0 && tmp.raw[7] == 0 &&
                      tmp.raw[8] == 0 && tmp.raw[9] == 0 &&
                      tmp.raw[10] == 0xFF && tmp.raw[11] == 0xFF);
        if (is_v4) { gid = tmp; break; }
        if (gid.global.interface_id == 0) gid = tmp;
    }
    return gid;
}

void RoCEv2Channel::connect_qp() {
    ibv_qp_attr attr{};
    attr.qp_state        = IBV_QPS_INIT;
    attr.pkey_index      = 0;
    attr.port_num        = cm_id_->port_num;
    attr.qp_access_flags = IBV_ACCESS_LOCAL_WRITE;
    if (ibv_modify_qp(qp_, &attr,
            IBV_QP_STATE | IBV_QP_PKEY_INDEX |
            IBV_QP_PORT | IBV_QP_ACCESS_FLAGS))
        throw std::runtime_error("modify_qp INIT failed");

    attr = {};
    attr.qp_state           = IBV_QPS_RTR;
    attr.path_mtu           = IBV_MTU_4096;
    attr.dest_qp_num        = remote_qpn_;      // 对端 QPN
    attr.rq_psn             = remote_psn_;      // 对端 PSN
    attr.max_dest_rd_atomic = 1;
    attr.min_rnr_timer      = 12;
    attr.ah_attr.is_global  = 1;
    attr.ah_attr.grh.dgid   = remote_gid_;      // 对端 GID
    attr.ah_attr.grh.sgid_index = 0;
    attr.ah_attr.grh.hop_limit  = 64;
    attr.ah_attr.port_num   = cm_id_->port_num;
    if (ibv_modify_qp(qp_, &attr,
            IBV_QP_STATE | IBV_QP_AV | IBV_QP_PATH_MTU |
            IBV_QP_DEST_QPN | IBV_QP_RQ_PSN |
            IBV_QP_MAX_DEST_RD_ATOMIC | IBV_QP_MIN_RNR_TIMER))
        throw std::runtime_error("modify_qp RTR failed");

    attr = {};
    attr.qp_state      = IBV_QPS_RTS;
    attr.timeout       = 14;
    attr.retry_cnt     = 7;
    attr.rnr_retry     = 7;
    attr.sq_psn        = local_psn_;
    attr.max_rd_atomic = 1;
    if (ibv_modify_qp(qp_, &attr,
            IBV_QP_STATE | IBV_QP_TIMEOUT | IBV_QP_RETRY_CNT |
            IBV_QP_RNR_RETRY | IBV_QP_SQ_PSN | IBV_QP_MAX_QP_RD_ATOMIC))
        throw std::runtime_error("modify_qp RTS failed");
}

void RoCEv2Channel::post_recv() {
    ibv_recv_wr wr{}, *bad = nullptr;
    ibv_sge sge{};
    sge.addr   = reinterpret_cast<std::uintptr_t>(buf_);
    sge.length = static_cast<std::uint32_t>(buf_size_);
    sge.lkey   = mr_->lkey;
    wr.wr_id   = 0;
    wr.sg_list = &sge;
    wr.num_sge = 1;
    if (ibv_post_recv(qp_, &wr, &bad))
        throw std::runtime_error("ibv_post_recv failed");
}

void RoCEv2Channel::send(ByteView data) {
    if (data.size() > buf_size_) throw std::runtime_error("frame too large");
    std::memcpy(buf_, data.data(), data.size());

    ibv_send_wr wr{}, *bad = nullptr;
    ibv_sge sge{};
    sge.addr   = reinterpret_cast<std::uintptr_t>(buf_);
    sge.length = static_cast<std::uint32_t>(data.size());
    sge.lkey   = mr_->lkey;
    wr.wr_id      = 1;
    wr.opcode     = IBV_WR_SEND;
    wr.send_flags = IBV_SEND_SIGNALED;
    wr.sg_list    = &sge;
    wr.num_sge    = 1;

    if (ibv_post_send(qp_, &wr, &bad))
        throw std::runtime_error("ibv_post_send failed");

    ibv_wc wc{};
    int n;
    do {
        n = ibv_poll_cq(cq_, 1, &wc);
        if (n > 0 && wc.wr_id != 1) n = 0;   // 跳过接收完成事件
    } while (n == 0);
    if (wc.status != IBV_WC_SUCCESS)
        throw std::runtime_error("send failed: " + std::to_string(wc.status));
}

Bytes RoCEv2Channel::recv() {
    ibv_wc wc{};
    int n;
    do {
        n = ibv_poll_cq(cq_, 1, &wc);
        if (n > 0 && wc.wr_id != 0) n = 0;   // 跳过发送完成事件
    } while (n == 0);
    if (wc.status != IBV_WC_SUCCESS)
        throw std::runtime_error("recv failed: " + std::to_string(wc.status));
    if (wc.byte_len > buf_size_)
        throw std::runtime_error("recv length exceeds buffer");

    Bytes out(wc.byte_len);
    std::memcpy(out.data(), buf_, wc.byte_len);
    post_recv();
    return out;
}

void RoCEv2Channel::close() noexcept {
    if (qp_) {
        if (cm_id_) rdma_destroy_qp(cm_id_);
        else        ibv_destroy_qp(qp_);
        qp_ = nullptr;
    }
    if (mr_)  { ibv_dereg_mr(mr_); mr_ = nullptr; }
    if (cq_)  { ibv_destroy_cq(cq_); cq_ = nullptr; }
    if (pd_)  { ibv_dealloc_pd(pd_); pd_ = nullptr; }
    rdma_cm_id* accepted_id = cm_id_;
    if (accepted_id) { rdma_destroy_id(accepted_id); cm_id_ = nullptr; }
    if (listen_id_ && listen_id_ != accepted_id) {
        rdma_destroy_id(listen_id_);
    }
    listen_id_ = nullptr;
    if (cm_channel_) { rdma_destroy_event_channel(cm_channel_); cm_channel_ = nullptr; }
    if (buf_) { std::free(buf_); buf_ = nullptr; }
}

RoCEv2Channel::~RoCEv2Channel() { close(); }

} // namespace civ

#endif
