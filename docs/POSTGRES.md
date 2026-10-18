# PostgreSQL 持久化与多节点验证

## 1. 连接方式

项目通过 `libpq` 连接 PostgreSQL，不依赖 ORM。运行时设置：

```bash
export CIV_PG_DSN="host=127.0.0.1 port=5432 dbname=civ user=civ password=civpass"
```

没有设置 `CIV_PG_DSN` 时，模拟仍可运行，只是不写数据库。

当前本机测试实例的 `pg_hba.conf` 使用 `trust`，只适合本机开发；生产/公网环境应改为
`scram-sha-256`，并限制 5432/5433 的防火墙来源。

## 2. 表结构

| 表 | 内容 |
| --- | --- |
| `runs` | 一次实验：seed、模型、量化、上下文、GPU 层数、状态、时间 |
| `agents` | 13 个因子的身份、AI 路线、独有进化方式 |
| `rounds` | 轮次、阶段、开始/结束时间 |
| `events` | 辩论结果、进化、淘汰、重生、帧交换 |
| `metrics` | 效率、帧数、帧字节数等数值指标 |
| `benchmarks` | benchmark 配置 |
| `benchmark_results` | 每次 benchmark 的 wall_ms、survivors 等结果 |

Schema 由 `PostgresStore::migrate()` 幂等创建，不需要手动执行 SQL。

## 3. 本地 Windows 启动

本机测试实例位于 `D:\pgsql`，服务名：

- 主节点：`postgresql-x64-18`，端口 5432，数据目录 `D:\pgsql\data2`
- 只读 standby：`postgresql-x64-18-node2`，端口 5433，数据目录 `D:\pgsql\data3`

连接主节点：

```powershell
$env:PATH="D:\pgsql\pgsql\bin;$env:PATH"
$env:CIV_PG_DSN="host=127.0.0.1 port=5432 dbname=civ user=civ password=civpass"
```

## 4. 多节点流复制

主节点写入后，standby 通过 WAL 流式复制；standby 是只读的。

创建 standby 的核心命令：

```bash
pg_basebackup -h 127.0.0.1 -p 5432 -U postgres \
  -D /var/lib/postgresql/18/standby -R -X stream -c fast
```

验证：

```sql
-- 主节点
INSERT INTO replication_probe(note) VALUES ('primary-write-ok');

-- standby
SELECT * FROM replication_probe;
-- 应看到 primary-write-ok

-- standby 写入应失败
INSERT INTO replication_probe(note) VALUES ('should-fail');
-- ERROR: cannot execute INSERT in a read-only transaction
```

## 5. 验证结果（本机）

- 单连接 CRUD：通过
- 4 个独立 libpq 连接并发写入：通过
- 13 个 Agent、7 轮、事件/指标写入：通过
- 主节点 5432 写入，standby 5433 读取：通过
- standby 写入被拒绝：符合预期

## 6. Linux 一键运行

```bash
bash scripts/linux_oneclick.sh
```

脚本会：

1. 检查 CPU、内存、GPU、显存。
2. 安装 `cmake/ninja/libpq/libibverbs/librdmacm` 和 CUDA（如可用）。
3. 下载 `Spark-X2.5-4B-Q4_K_M.gguf`。
4. 根据显存选择 `CIV_N_CTX`：6 GB → 512，8 GB → 1024，更大 → 2048。
5. 构建并运行；设置 `CIV_PG_DSN` 时同时写入 PostgreSQL。

常用参数：

```bash
bash scripts/linux_oneclick.sh --skip-install
bash scripts/linux_oneclick.sh --cuda-test
bash scripts/linux_oneclick.sh --benchmark
bash scripts/linux_oneclick.sh --no-model --no-postgres
```

## 7. 虚拟机说明

本机 Ubuntu VM 已启动，IP 为 `192.168.30.133`，SSH 22 端口开放，但当前没有
guest 用户名/密码，无法自动登录安装 PostgreSQL。提供 guest 凭据后，可在 VM 内执行
同一 `linux_oneclick.sh`，并用虚拟机 IP 作为第二节点做远程写入/复制测试。
