# 翁法罗斯灵感：再创世模拟模式

> 这是受“翁法罗斯 / 再创世”主题启发的**原创模拟规则**，不复刻剧情文本、对白或素材。
> 使用用户提供的 13 因子、泰坦路径和模拟命途作为角色设定。

## 开启

```bash
export CIV_WORLD_MODE=amphoreus
export CIV_DEBATE_MODE=roundtable
export CIV_MAX_ROUNDS=3
./build/cuda-release/civ_main 42
```

不设置 `CIV_WORLD_MODE` 时保持原来的文明模拟。

## 世界机制

### 泰坦试炼

每一轮让所有存活因子依次经历全部 13 个泰坦试炼，而不是只抽取一个：

```text
门径、律法、岁月、大地、海洋、天空、负世、理性、浪漫、纷争、死亡、诡计、全世
```

每个因子每轮都会完成 `13/13` 试炼。与自身路径共振的因子获得额外收益：

- 效率加成
- 火种增长
- 世界记忆增长

每轮还会记录：

- 每个因子完成的试炼数；
- 每个因子的共振数与共振路径；
- 每个泰坦的参与者数和共振人数。

### 火种传承

每个因子初始拥有 `1.0` 火种。被淘汰时：

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

每轮结束进入一次再创世：

- 黑潮部分消退
- 侵蚀被削弱
- 存活 Agent 获得少量火种和记忆
- 世界记忆保留到下一世

### AI 生成世界毁灭形式

每轮在淘汰前，会选一名存活因子作为“灾变叙述者”：

- 从本轮全部泰坦试炼中，优先选择累计共振次数最高的因子；
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
| 黑潮吞世 | 黑潮 >= 80 |
| 第十三因子·全世记忆 | Agent 12 存活、稳定 >= 65、记忆 >= 55% |
| 黎明再创世 | 稳定 >= 65 |
| 记忆守望 | 世界记忆 >= 70% |
| 脆弱轮回 | 其他情况 |

## 数据记录

每轮会写入：

```text
events:
  titan_trial
  titan_gauntlet_result
  titan_trial_completed
  world_catastrophe
  coreflame_transfer
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
CIV_MAX_ROUNDS=1   # 先验证
CIV_N_CTX=512
```

确认对话和世界状态正常后，再逐步增加轮数。

三区状态、自我训练与 LoRA 数据导出见
[SELF_TRAINING.md](SELF_TRAINING.md)。
