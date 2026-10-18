# RoCEv2 测试指南

## 1. 硬件与系统要求

RoCEv2 需要：

- 两块支持 RoCE 的网卡（常见为 NVIDIA/Mellanox ConnectX 系列）；
- 支持 PFC/ECN 的交换机，或两台机器直连；
- Linux（推荐 Ubuntu 22.04/24.04）；
- `rdma-core`、`libibverbs-dev`、`librdmacm-dev`、`perftest`。

VMware 虚拟机默认虚拟网卡通常不支持 RoCE。要在虚拟机测试，需要 PCIe
passthrough 或 SR-IOV VF；否则请使用两台物理机，或先用 Soft-RoCE（RXE）做功能验证。

## 2. 检查设备

```bash
lspci | grep -i -E 'mellanox|infiniband'
ibv_devices
ibv_devinfo
rdma link show
ip addr
```

确认端口状态为 `ACTIVE`，并记录设备名（例如 `mlx5_0`）和 GID。

## 3. 安装依赖

```bash
sudo apt update
sudo apt install -y build-essential cmake ninja-build \
  libibverbs-dev librdmacm-dev rdma-core perftest
```

## 4. 配置 RoCEv2

RoCEv2 使用 UDP 封装。确认网卡/交换机已启用 PFC/ECN，并设置：

```bash
# 查看当前模式
cma_roce_mode -d mlx5_0 -p 1

# 切换到 RoCEv2（不同驱动/系统参数可能不同）
sudo cma_roce_mode -d mlx5_0 -p 1 -m 2

# 查看 GID 表，选择 IPv4-mapped GID
show_gids
```

两台机器应位于同一子网，MTU 一致。项目 `RoCEv2Channel` 会优先选择 IPv4-mapped GID。

## 5. 先用 perftest 验证链路

在服务端：

```bash
ib_send_bw -d mlx5_0 -x <gid_index>
```

在客户端：

```bash
ib_send_bw -d mlx5_0 -x <gid_index> <server-ip>
```

再测试 write/read：

```bash
ib_write_bw -d mlx5_0 -x <gid_index> <server-ip>
ib_read_bw  -d mlx5_0 -x <gid_index> <server-ip>
```

如果 perftest 不通，先排查 PFC、GID、MTU、防火墙和交换机配置，不要直接调试项目代码。

## 6. 构建并运行项目通道

确保 CMake 找到 RDMA：

```bash
cmake -S . -B build-gpu -G Ninja \
  -DCMAKE_BUILD_TYPE=Release \
  -DCIV_ENABLE_CUDA=ON \
  -DCIV_ENABLE_POSTGRES=ON
cmake --build build-gpu --parallel
```

服务端：

```bash
./build-gpu/civ_distributed server 7471
```

客户端：

```bash
./build-gpu/civ_distributed client 7471 <server-ip>
```

项目当前使用 RDMA CM + RC QP + SEND/RECV，一次发送 64 字节并在服务端打印长度。
它不是一个完整的基准工具；先用它验证连接、QP 交换和收发，再用 perftest 测吞吐/延迟。

## 7. 建议的测试矩阵

| 测试 | 目标 |
| --- | --- |
| `ibv_devices` / `ibv_devinfo` | 设备、端口、GID 正常 |
| `rping -s` / `rping -c` | RDMA CM 连接正常 |
| `ib_send_bw` | SEND/RECV 吞吐 |
| `ib_write_bw` / `ib_read_bw` | RDMA READ/WRITE 吞吐 |
| 项目 server/client | QP 信息交换和 64B 消息 |
| 双向/多消息循环 | 当前实现的边界和重连行为 |
| 断链/重启 | 错误处理与资源释放 |

## 8. Soft-RoCE（没有 RDMA 网卡时）

Linux 可以用 RXE 做功能验证：

```bash
sudo modprobe rdma_rxe
sudo rdma link add rxe0 type rxe netdev eth0
ibv_devices
```

Soft-RoCE 走软件模拟，性能不代表真实硬件；它适合验证连接、QP 和协议逻辑。

## 9. 当前实现限制

- 单缓冲区、一次一条 SEND/RECV，没有 ring buffer；
- 没有 RDMA WRITE/READ，只有 SEND/RECV；
- 没有多消息边界、重连、认证和加密；
- `civ_distributed` 是单向 smoke test，不是吞吐基准；
- 没有硬件运行时验证时，代码只能保证编译和静态逻辑。
