# Security Policy

## Supported Versions

当前主分支为安全更新目标。

## Reporting a Vulnerability

请不要在公开 issue 中披露可利用细节。请通过 GitHub Security Advisory
或维护者邮箱私下报告，并附上：

- 影响版本/commit
- 复现步骤
- 影响范围
- 建议修复方向（如有）

## 已知边界

- 本项目的“量子 RNG / QKD”是软件模拟，不是密码学量子设备。
- RoCEv2 通道需要受信任的 Linux/RDMA 环境。
- 默认示例 PostgreSQL 使用本地测试凭据，生产环境必须使用 `scram-sha-256`、
  最小权限账户和网络访问控制。
