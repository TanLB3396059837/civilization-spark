# Changelog

## v3.3.14 - 2026-10-30

### Added

- Native Moore Threads MUSA build support for the bundled llama.cpp backend,
  including automatic MTT S4000 (`MUSA_ARCHITECTURES=22`) detection.
- `CIV_MAX_ROUNDS=0` runs without a round limit and continues across
  re-creation cycles until the rented instance is terminated.
- CUDA and MUSA backends are mutually selected at configure time, while the
  existing NVIDIA custom-kernel path remains unchanged.

### Validated

- Local finite and multi-cycle tests remain compatible with the unlimited
  round mode.
- MUSA runtime validation is performed on an MTT S4000 48 GB instance.

## v3.3.13 - 2026-10-30

### Added

- Every cleared factor now collects one Titan coreflame and immediately reduces
  black tide by `0.2`. The reward event records the resulting black-tide value.
- A per-round destruction judgment lets Agent 6 request the death of love
  factor Agent 12. Agent 12 is killed only after an explicit consent decision.
- Agent 6 may directly select one living factor other than love each round;
  that target requires no consent.
- New events: `destruction_choice`, `love_consent`, and `factor_death`.
- New metrics: `coreflames_collected_total` and `surviving_agents`.

### Validated

- Local 100-round CPU/PostgreSQL run: seven complete 13-stage cycles, eight
  consented love-factor deaths, 100 completed stages, and 263 collected
  coreflames with 263 matching reward events.

## v3.3.12 - 2026-10-30

### Changed

- Separated the 13-stage Titan cycle length from the total simulation duration.
  `CIV_MAX_ROUNDS` now accepts 1–100 total rounds and defaults to one 13-stage
  cycle.
- A black-tide extinction ends the current cycle but no longer terminates a
  longer multi-cycle run. The next round starts the fresh cycle with all 13
  Titan stages reopened.
- Titan progress denominators and event payloads now use the shared
  `TITAN_STAGES` constant.

### Validated

- A local 100-round CPU/PostgreSQL run completes across repeated cycles with
  per-cycle extinction and re-creation records.

## v3.3.11 - 2026-10-30

### Changed

- Each re-creation cycle now advances through the fixed 13-Titan order one
  stage per round instead of rerunning every Titan in every round.
- A stage selects only 3–5 living factors. Path-resonant factors are selected
  first and rotation fills the remaining places.
- Cleared stages never repeat within the same cycle. Only factors that actually
  clear a stage receive that Titan's coreflame reward.
- Completing all 13 stages triggers total extinction and creates a new cycle
  with all 13 stages available again.
- Black tide reaching 100 remains an intentional early terminal condition.
  While the love factor is dead, black tide is capped at 99 and cannot trigger
  that early termination.
- Love and destruction are the only factors initialized with knowledge of that
  black-tide lock. The other 11 factors do not receive it in their initial
  memory or model context; disclosure can only happen through dialogue.
- New events include `titan_trial_result`, `titan_trial_failed`,
  `coreflame_reward`, `total_extinction`, and `trial_cycle_progress`.

### Fixed

- Re-creating agents now use cycle-namespaced local IDs, avoiding PostgreSQL
  uniqueness conflicts and preserving prior training exports.
- The Windows simulation lifetime test no longer touches the shared model pool
  after the simulation has released its agents.
- Re-creation state is reset explicitly to `0/13`, with
  `all_agents_died=true` and `next_trials=13` recorded in PostgreSQL.

## v3.3.10 - 2026-10-30

### Changed

- Every re-creation round now runs the complete Titan gauntlet instead of one
  rotating Titan path.
- Every surviving factor is tested by all 13 Titans and receives a complete
  `13/13` coverage record.
- Resonance results are aggregated per factor and per Titan, with narrator
  selection based on total resonance count.
- New events: `titan_gauntlet_result` and `titan_trial_completed`.
- New metrics: `titan_trials_completed` and `titan_resonance_count`.

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
