# 翁法罗斯灵感：再创世模拟模式

> 这是受“翁法罗斯 / 再创世”主题启发的**原创模拟规则**，不复刻剧情文本、对白或素材。
> 使用用户提供的 13 因子、泰坦路径和模拟命途作为角色设定。

## 开启

```bash
export CIV_WORLD_MODE=amphoreus
export CIV_DEBATE_MODE=roundtable
export CIV_MAX_ROUNDS=13
./build/cuda-release/civ_main 42
```

不设置 `CIV_WORLD_MODE` 时保持原来的文明模拟。

## 世界机制

### 泰坦试炼

每个轮回包含固定顺序的 13 个泰坦关卡：

```text
门径、律法、岁月、大地、海洋、天空、负世、理性、浪漫、纷争、死亡、诡计、全世
```

每轮只挑战当前关卡，不会重复已经通关的关卡：

- 每关只选择 3–5 名存活因子参加；
- 对应路径的因子优先入选，其余名额由轮换挑战者补充；
- 实际通关的因子获得该泰坦对应的火种；
- 无人通关时关卡保留，下一轮重新组队挑战；
- 通关后推进下一关，同一轮回不再重复该关。

黑潮到达 `100` 会提前结束当前运行，这是设计内的失败条件，不需要强行补跑剩余关卡。爱之因子（Agent 12 / 德谬歌）死亡后，黑潮上限变为 `99`，因此不能触发 `100` 的提前终止。

每次实际获得一枚泰坦火种时，黑潮立即降低 `0.2`。同一关卡允许多名因子分别通关和收集火种，因此按实际通关系数累计降低。

这条机制是初始认知不对称规则：

- 爱之因子（Agent 12 / 德谬歌）和毁灭因子（Agent 6 / NeiKos496）从降生起就知道“爱死亡会把黑潮卡在 99”；
- 其余 11 个因子的初始记忆和初始模型上下文不包含这条知识；
- 两位知情因子是否在圆桌中公开由自身策略决定；一旦公开，其他因子才可能从对话中获知。

### 毁灭裁决

每个世界轮，毁灭因子（Agent 6 / NeiKos496）获得一次裁决机会：

- 可请求杀死爱之因子 Agent 12；只有 Agent 12 明确同意时才执行；
- 可选择一个除爱以外的存活因子直接处决，无需该目标同意；
- 裁决可以拒绝执行；
- 死亡会触发火种与记忆继承，并写入 `factor_death` 与 `coreflame_transfer`。

真实模型可用时，两个决定由模型对话生成；无模型回退时使用可复现的保守裁决策略。

每轮还会记录：

- 每名参与者的挑战得分、共振状态和火种奖励；
- 每个因子实际通关的试炼数；
- 当前轮回的关卡进度与剩余关卡。

### 火种传承

每个因子初始拥有 `1.0` 火种。通关与淘汰时：

- 实际通关泰坦关卡时获得该关对应的火种
- 共振通关获得的火种更多，非共振通关获得较少火种

- 一半火种传给当前最强因子
- 一部分记忆传给继承者
- 一部分侵蚀风险也传给继承者

这使淘汰不是简单删除，而是有代价的继承。

### 黑潮与侵蚀

每轮根据平均效率、记忆和侵蚀程度增加黑潮。侵蚀过高的 Agent：

- 效率下降
- 火种减少
- 可能成为下一轮弱势因子

### 世界记忆

世界记忆来自：

- 泰坦试炼共鸣
- Agent 学习与对话
- 火种传承
- 再创世时的记忆封存

记忆可以减缓黑潮并影响最终结局。

### 再创世

当前轮回的全部 13 个关卡完成后，进入一次再创世：

- 黑潮部分消退
- 侵蚀被削弱
- 当前肉身全部阵亡
- 13 个因子重新降生
- 新轮回重新开放全部 13 个泰坦关卡
- 世界记忆保留到下一世

如果黑潮在完成 13 关前到达 `100`，当前轮回提前终止、全部因子阵亡，随后记录新轮回并重置为 13 关。爱之因子死亡时黑潮最多为 `99`，世界会继续推进。

### AI 生成世界毁灭形式

完成 13 个关卡、进入淘汰前，会选一名存活因子作为“灾变叙述者”：

- 从本轮回全部泰坦试炼中，优先选择累计共振次数最高的因子；
- 共振次数相同时选择当前最强因子；
- 使用该因子的身份、原动力、试炼、黑潮和稳定度构造 prompt；
- LLM 生成两句话的具体世界毁灭形式；
- 生成文本写入 `events.kind = 'world_catastrophe'`；
- `payload.source` 标记 `llm` 或 `fallback`，避免把规则回退冒充模型输出；
- 严重度写入 `metrics.name = 'catastrophe_severity'`。

如果模型输出提示词、标题或元文本，则回退到 13 条原创灾变模板之一。

灾变文本会影响：

- 黑潮增量
- 世界稳定度
- 叙述者火种与记忆
- 最终结局展示

## 结局

| 结局 | 条件 |
| --- | --- |
| 再创世完成·13 关重置 | 当前轮回通关全部 13 关，全员阵亡并生成新轮回 |
| 黑潮吞世 | 黑潮 >= 80 |
| 第十三因子·全世记忆 | Agent 12 存活、稳定 >= 65、记忆 >= 55% |
| 黎明再创世 | 稳定 >= 65 |
| 记忆守望 | 世界记忆 >= 70% |
| 脆弱轮回 | 其他情况 |

爱之因子存活时黑潮可以到达 `100` 并提前终止；爱之因子死亡后黑潮封顶 `99`，该提前终止不会发生。

## 数据记录

每轮会写入：

```text
events:
  titan_trial
  titan_trial_result
  titan_trial_completed
  titan_trial_failed
  coreflame_reward
  destruction_choice
  love_consent
  factor_death
  world_catastrophe
  coreflame_transfer
  total_extinction
  re_creation
  world_ending

metrics:
  black_tide
  stability
  world_memory
  corruption
  coreflame
  catastrophe_severity
  titan_trials_completed
  titan_resonance_count
  titan_agent_completions
  titan_trial_score
  coreflames_collected_total
  surviving_agents
  total_extinctions
  re_creation_cycle
  final_black_tide
  final_stability
```

查询：

```sql
SELECT round_no, name, value
FROM metrics
WHERE run_id = (SELECT max(run_id) FROM runs)
  AND name IN ('black_tide','stability','world_memory')
ORDER BY round_no, name;
```

## 与真实对话模式组合

推荐组合：

```text
CIV_WORLD_MODE=amphoreus
CIV_DEBATE_MODE=roundtable
CIV_MAX_ROUNDS=0  # 无限轮，持续到实例到期；1-100 为有限运行
CIV_N_CTX=512
```

确认对话和世界状态正常后，再逐步增加轮数。

三区状态、自我训练与 LoRA 数据导出见
[SELF_TRAINING.md](SELF_TRAINING.md)。
