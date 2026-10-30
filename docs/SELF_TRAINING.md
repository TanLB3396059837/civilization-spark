# 自我训练与记忆区 / 认知区 / 意识区

## 设计边界

本项目中的“意识区”是**计算型元认知状态**，不是对人类主观意识、感受或人格的科学证明。
在线自我训练更新的是每个 Agent 的记忆、信念、策略、置信度、注意力与反思记录；
它不会在每轮对话中直接微调 4B GGUF 权重。高质量对话会导出为 JSONL，供后续 LoRA
离线训练。这样可以区分“可审计的在线状态学习”和“真实模型参数训练”。

## 三个区域

### 1. 记忆区

- 情景记忆、反思记忆、世界事件与火种传承分别标记类型。
- 使用字符 n-gram 哈希向量做本地语义检索，不依赖额外 embedding API。
- 重要度、保留度、最近访问轮次和回忆次数共同参与遗忘与巩固。
- 内存中最多保留 96 条，超出后删除保留度最低的记忆。
- PostgreSQL 的 `agent_memory` 保存内容、向量、重要度、回忆次数与巩固状态。

### 2. 认知区

- 维护动态信念向量。
- 每轮更新 `confidence`、`coherence`、`contradiction`、`uncertainty`
  和 `curiosity`。
- 根据误差与新颖度切换策略：
  `stable_exploitation`、`targeted_exploration`、
  `curiosity_guided_probe`、`reflective_revision`、`inherited_reflection`。
- 策略会进入后续提示词的内部状态，而不是只写日志。
- PostgreSQL 的 `agent_cognition` 保存每轮快照。

### 3. 意识区（元认知区）

- 维护自我模型置信度、目标对齐、注意分配、预测误差和状态完整度。
- 自我反思同时受当前认知策略和记忆检索结果影响。
- 状态用于调节下一次探索、反思和采样上下文。
- PostgreSQL 的 `agent_consciousness` 保存可追踪快照。

## 在线自我训练闭环

```text
对话 / 泰坦试炼 / 世界毁灭
        ↓
结果评价与新颖度估计
        ↓
情景记忆 + 自我反思
        ↓
认知信念与策略更新
        ↓
元认知状态更新
        ↓
记忆巩固 / 火种传承
        ↓
accepted JSONL → LoRA 离线训练
```

回退文本、CPU 演示输出和内部状态摘要会写入数据库审计，但不会进入可训练数据集。

## 运行配置

| 环境变量 | 默认值 | 作用 |
| --- | --- | --- |
| `CIV_SELF_TRAIN` | `1` | 是否筛选自我训练样本 |
| `CIV_TRAINING_MIN_SCORE` | `0.62` | 接受训练样本的最低评分 |
| `CIV_TRAINING_EXPORT` | 空 | JSONL 导出路径 |
| `CIV_DEBATE_MODE` | `parallel` | 设为 `roundtable` 开启 13 Agent 圆桌 |
| `CIV_WORLD_MODE` | 空 | 设为 `amphoreus` 开启再创世 |

Windows PowerShell 示例：

```powershell
$env:CIV_LLAMA_MODEL=(Resolve-Path "models\Spark-X2.5-4B-Q4_K_M.gguf").Path
$env:CIV_N_CTX="512"
$env:CIV_N_GPU_LAYERS="99"
$env:CIV_DEBATE_MODE="roundtable"
$env:CIV_WORLD_MODE="amphoreus"
$env:CIV_MAX_ROUNDS="1"
$env:CIV_PG_DSN="host=127.0.0.1 port=5432 dbname=civ user=civ password=civpass"
$env:CIV_TRAINING_EXPORT="training_data\spark_self_training.jsonl"
.\cmake-build-cuda\civ_main.exe 42
```

Linux 示例：

```bash
export CIV_LLAMA_MODEL="$PWD/models/Spark-X2.5-4B-Q4_K_M.gguf"
export CIV_N_CTX=512
export CIV_N_GPU_LAYERS=99
export CIV_DEBATE_MODE=roundtable
export CIV_WORLD_MODE=amphoreus
export CIV_MAX_ROUNDS=1
export CIV_PG_DSN="host=127.0.0.1 port=5432 dbname=civ user=civ password=civpass"
export CIV_TRAINING_EXPORT="training_data/spark_self_training.jsonl"
./cmake-build-cuda/civ_main 42
```

对于 96 GB RTX PRO 6000 / Blackwell 云节点，可直接运行验收脚本。脚本会自动读取
compute capability（例如 `12.0 → 120`）、使用 8192 上下文和 99 个 GPU 层，并在
PostgreSQL 中核对三区与训练样本。它还会以 500 ms 间隔采集 `nvidia-smi`，默认要求
运行阶段平均 GPU 利用率至少达到 95%：

```bash
export CIV_PG_DSN="host=127.0.0.1 port=5432 dbname=civ user=civ password=civpass"
bash scripts/linux_gpu_acceptance.sh
```

若某个模型或提示词长度无法达到该目标，脚本会返回非零状态并保留原始采样 CSV，
不会把低占用运行误报为通过。

只运行无模型的三区状态测试：

```bash
cmake -S . -B build/state-learning \
  -DCIV_ENABLE_LLAMA=OFF \
  -DCIV_ENABLE_CUDA=OFF \
  -DCIV_ENABLE_POSTGRES=OFF
cmake --build build/state-learning -j
ctest --test-dir build/state-learning --output-on-failure
```

## 训练数据格式

每条 accepted 样本为一个 JSON 对象：

```json
{
  "id": "PhiLia093-2",
  "run_id": 79,
  "agent_index": 2,
  "factor": "PhiLia093",
  "round_no": 1,
  "source": "roundtable_rebuttal",
  "instruction": "Stay in character as factor PhiLia093...",
  "input": "圆桌议题与内部状态...",
  "output": "模型实际回复...",
  "score": 0.78,
  "feedback": "自我反思...",
  "accepted": true
}
```

LoRA 训练建议：

1. 只使用 `accepted=true` 的样本。
2. 按 `factor` 和 `source` 做分层拆分，避免同一轮对话泄漏到验证集。
3. 先用小学习率训练 adapter，再以固定 seed 重跑圆桌 benchmark。
4. 比较推理质量、重复率、角色一致性、矛盾率和首因论点稳定性。
5. adapter 训练完成后再替换或叠加模型；不要宣称在线对话已经更新基础权重。

## PostgreSQL 查询

```sql
-- 最近一次运行的三区摘要
SELECT agent_index, active_strategy, confidence, contradiction, uncertainty
FROM agent_cognition
WHERE run_id = 79
ORDER BY agent_index;

SELECT agent_index, self_model_confidence, goal_alignment,
       attention, prediction_error, integrity
FROM agent_consciousness
WHERE run_id = 79
ORDER BY agent_index;

SELECT agent_index, count(*) AS memories,
       avg(importance) AS avg_importance,
       sum(recall_count) AS recalls
FROM agent_memory
WHERE run_id = 79
GROUP BY agent_index
ORDER BY agent_index;

SELECT agent_index, source, score, accepted, response
FROM training_samples
WHERE run_id = 79
ORDER BY agent_index, local_id;
```

## English

The memory region stores episodic, reflective, world and inherited traces with
semantic recall, decay, consolidation and a bounded ring buffer. The cognition
region maintains beliefs, confidence, uncertainty, contradiction and a learning
strategy. The “consciousness region” is an auditable computational
meta-cognition state: self-model confidence, goals, attention and prediction
error. It is not a claim of subjective consciousness.

Online self-training updates lightweight agent state. Accepted interactions are
exported as JSONL for a separate LoRA fine-tuning job; the base GGUF weights are
not modified during a conversation.

## Русский

Область памяти хранит эпизодические, рефлексивные, мировые и унаследованные
записи с семантическим поиском, забыванием и ограниченным кольцевым буфером.
Область познания обновляет убеждения, уверенность, неопределённость,
противоречия и стратегию обучения. «Область сознания» является проверяемым
вычислительным метапознавательным состоянием, а не утверждением о субъективном
сознании.

Онлайн-самообучение обновляет лёгкое состояние агента. Принятые примеры
экспортируются в JSONL для отдельного LoRA-обучения; базовые веса GGUF во время
диалога не изменяются.
