# Changelog

## v3.3.9 - 2026-10-30

### Added

- Bounded episodic memory with semantic recall, retrieval relevance, decay,
  consolidation, forgetting and inheritance through the coreflame.
- Dynamic cognition region with belief updates, confidence, coherence,
  contradiction, uncertainty, curiosity and strategy selection.
- Computational meta-cognition region with self-model confidence, goals,
  attention, prediction error and integrity.
- Online self-training loop that writes reflections and accepted JSONL samples
  for later LoRA fine-tuning.
- PostgreSQL tables: `agent_memory`, `agent_cognition`,
  `agent_consciousness`, `training_samples`, `self_training_runs`.
- `CIV_SELF_TRAIN`, `CIV_TRAINING_MIN_SCORE` and `CIV_TRAINING_EXPORT`.
- Optional `CIV_ENABLE_LLAMA=OFF` build for lightweight state-learning tests.
- 13 roundtable openings now use one GPU micro-batch while rebuttals remain
  causally ordered.
- `linux_gpu_acceptance.sh` samples `nvidia-smi` every 500 ms and enforces a
  configurable 95% average GPU-utilization target.

### Fixed

- Release-process shutdown could return `0xC0000409` after a completed
  simulation. Deterministic shutdown now releases agents and storage before
  process teardown.
- Windows builds now use shared ggml/llama DLLs with post-build copying and
  disable ggml OpenMP, avoiding the release static-runtime teardown conflict.
- CPU fallback replies are stored for audit but can no longer enter the
  accepted training dataset.
- World catastrophe events now record whether the text came from the LLM or a
  deterministic fallback.

## v3.3.8 - 2026-10-30

### Added

- Amphoreus-inspired re-creation world mode: `CIV_WORLD_MODE=amphoreus`.
- Rotating Titan trials with path resonance bonuses.
- Coreflame inheritance when an agent is eliminated.
- Black-tide pressure, corruption, stability, and world-memory mechanics.
- AI-generated world destruction form with a surviving factor as narrator,
  persisted as `world_catastrophe` with severity.
- Re-creation phases and multiple world endings.
- Metrics and events for trials, flame transfer, re-creation, and endings.

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
