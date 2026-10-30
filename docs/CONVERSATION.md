# 真正圆桌对话模式

## 模式

默认仍是并行辩论：

```text
CIV_DEBATE_MODE=parallel
```

开启真正对话：

```bash
export CIV_DEBATE_MODE=roundtable
export CIV_MAX_ROUNDS=1
./build/cuda-release/civ_main 42
```

## 每轮流程

```text
13 个 Agent 依次开场
        ↓
所有开场观点组成 shared transcript
        ↓
Agent 0 回应一个其他因子
        ↓
Agent 1 读取开场 + Agent 0 的回应后回答
        ↓
...
        ↓
每个 Agent 都能看到前面所有开场观点和最近回应
```

对话不是简单的 13 个并行独白：

- 开场阶段各自表达立场；
- 回应阶段每个 Agent 都读取其他 Agent 的发言；
- 回应会明确指定 `target_agent`；
- PostgreSQL 记录 `reply_to_message_id`，形成可追问的对话链。

## 运行配置

| 环境变量 | 默认值 | 说明 |
| --- | --- | --- |
| `CIV_DEBATE_MODE` | `parallel` | `roundtable` 开启对话 |
| `CIV_MAX_ROUNDS` | `7` | 对话/模拟轮数 |
| `CIV_N_CTX` | `2048` | 每个 Agent 的上下文 |
| `CIV_ROUNDTABLE_TOPIC` | 内置中文议题 | 自定义议题 |

Windows PowerShell 传入中文环境变量时可能使用本地代码页，建议直接使用默认议题，或保证
环境变量值为 UTF-8。

## 对话数据库

`conversations`：

```text
conversation_id, run_id, topic, mode, created_at, finished_at
```

`conversation_messages`：

```text
message_id, conversation_id, round_no, turn_no,
speaker_agent, target_agent, reply_to_message_id,
phase(opening/rebuttal), content, created_at
```

查询一场圆桌：

```sql
SELECT m.round_no, m.turn_no, m.speaker_agent, m.target_agent,
       m.phase, m.reply_to_message_id, m.content
FROM conversation_messages m
JOIN conversations c USING (conversation_id)
WHERE c.run_id = 47
ORDER BY m.message_id;
```

## 质量保护

模型偶尔会输出提示词、Markdown 标记或“要求：……”之类的元文本。圆桌模式会检测这类
不可用回复，并回退到该因子的第一因档案文本；正式对话链仍会完整记录。

## 上下文策略

每轮只携带：

- 所有开场观点的短摘要；
- 最近 4 条回应；
- 当前 Agent 的身份、原动力和 AI 路线。

这样 13 个 Agent 不会一次性把完整长对话塞满 512 token 上下文。需要更长对话时，
可以提高 `CIV_N_CTX`，或后续加入 LLM 摘要压缩。
