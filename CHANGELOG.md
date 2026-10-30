# Changelog

## v3.3.6 - 2026-10-30

### Added

- True 13-agent roundtable mode: `CIV_DEBATE_MODE=roundtable`.
- Opening statements plus a rebuttal pass where every agent reads the room.
- PostgreSQL `conversations` and `conversation_messages` tables with
  `speaker_agent`, `target_agent`, and `reply_to_message_id`.
- Chat-template based generation with UTF-8-safe output and meta-reply fallback.
- `CIV_MAX_ROUNDS` for short conversation validation.

## v3.3.0 - 2026-10-19

### Added

- 13 因子档案：黄金裔、泰坦路径、原动力、模拟命途、第一因。
- 13 种现代 AI 学习路线与独有进化方式。
- PostgreSQL 18 / libpq 持久化层：runs、agents、rounds、events、metrics、benchmarks。
- 主从流复制验证与多连接并发写入测试。
- `civ_bench` 多 seed benchmark。
- 自定义 CUDA elementwise / softmax attention 正确性测试。
- Linux GPU 一键运行脚本与 Windows 模型下载脚本。

### Fixed

- llama.cpp vocab API 兼容、KV 上下文按序列分配。
- QuantumRNG `next_range` 溢出。
- KFL-M 会话号截断、CRC 失败字段清零。
- Windows TCP socket 有效性判断、共享内存容量与所有权。
- CUDA attention 缺少 softmax。
- 同种子不可复现、批解码超过 `n_batch` 等问题。
