# Benchmarks

## 环境

| 项目 | 配置 |
| --- | --- |
| CPU | AMD Ryzen 7 5800H, 8C/16T |
| RAM | 32 GB |
| GPU | NVIDIA GeForce RTX 3060 Laptop, 6 GB |
| 模型 | Spark-X2.5-4B-Q4_K_M |
| 上下文 | 512 tokens / agent, total 8192 |
| GPU layers | 99 |
| 轮数 | 7 |
| Agent 数 | 13 |

## 单次 GPU 模拟

```text
seed  wall_ms  rounds  survivors
42    14354.2  7       6
43    12498.1  7       6
44    13520.2  7       6
```

平均约 13.46 秒/次。

## 单轮批量生成

每轮 13 个 Agent、约 16 tokens/Agent，耗时约 1.4–2.0 秒。

## CUDA 数值验证

| Kernel | 最大绝对误差 |
| --- | --- |
| elementwise | 0 |
| attention softmax | 7.45e-09 |

## 复现

```bash
export CIV_LLAMA_MODEL=/path/to/Spark-X2.5-4B-Q4_K_M.gguf
export CIV_N_CTX=512
export CIV_N_GPU_LAYERS=99
export CIV_PG_DSN="host=127.0.0.1 port=5432 dbname=civ user=civ password=civpass"
export CIV_BENCH_SEEDS=42,43,44
export CIV_BENCH_REPEAT=1
./build/cuda-release/civ_bench
```
