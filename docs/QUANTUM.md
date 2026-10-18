# 量子计算机测试指南

## 先明确现状

当前项目里的 `QuantumRNG` 是经典软件 PRNG，`generate_qkd_key()` 只是生成随机字节，
**不是真正的量子随机数，也不是 QKD**。要用量子计算机测试，需要先接入真实的量子熵源。

## 三个测试层级

| 层级 | 能验证什么 | 推荐入口 |
| --- | --- | --- |
| 本地量子模拟器 | 电路逻辑、比特分布、接口集成 | Qiskit Aer |
| 云量子计算机 | 真实量子测量随机性、硬件噪声 | IBM Quantum / AWS Braket / Azure Quantum |
| QKD 测试床 | 真正的量子密钥分发链路 | 光子 QKD 设备或 QKD 试验网 |

注意：门模型量子计算机是单方计算设备，**不能单独完成 BB84 这类双方 QKD**。QKD 需要
两端之间的量子信道和专用光子设备；云量子计算机只能提供量子随机数或量子算法实验。

## 云量子平台候选

| 平台 | 硬件/能力 | 接入方式 | 适合场景 |
| --- | --- | --- | --- |
| [本源量子云](https://qcloud.originqc.com.cn/) | “本源悟空”180 比特超导真机 + 4 类模拟器 | 中文网页、QPanda3、MCP | 国内访问、真机随机数、QPanda 生态 |
| [qBraid](https://www.qbraid.com/) | 20+ QPU、11 家提供商、云 IDE | 免费计划、统一 SDK、Qiskit/Braket/Cirq | 想快速比较多个真机，不想分别注册平台 |
| [IBM Quantum](https://quantum.ibm.com/) | IBM 超导真机 | Qiskit Runtime、Open Plan 免费额度 | 最成熟的 Qiskit 工作流、H+measure 随机数 |
| [Amazon Braket](https://aws.amazon.com/braket/) | IonQ、Rigetti、IQM、QuEra 等 | AWS SDK、按任务/按 shot 计费 | 需要多种硬件、AWS 计费体系 |
| [Azure Quantum](https://azure.microsoft.com/en-us/products/quantum) | IonQ、Quantinuum、Rigetti 等 | Azure 门户、Q#/Qiskit/Cirq | 企业账号、Azure credits、混合工作流 |
| [Quantum Inspire](https://www.quantum-inspire.com/) | QuTech 超导/量子点平台 | 网页、模拟器、硬件申请/额度 | 欧洲平台、教学和小电路 |
| [D-Wave Leap](https://cloud.dwavesys.com/leap/) | 量子退火机 | Leap 云服务、Ocean SDK | 组合优化；**不适合** H+measure QRNG/BB84 |

### 推荐尝试顺序

1. **qBraid 免费计划**：无需信用卡，先用免费模拟器和统一 SDK 跑通 H+measure；
   官方页面当前列出 100 CPU 小时/月、每模拟器 100 jobs/天、20+ QPU 的免费入口。
2. **本源量子云**：中文界面，先跑全振幅/含噪声模拟器，再申请“本源悟空”真机机时。
3. **IBM Quantum**：如果你主要使用 Qiskit，Open Plan 适合做小电路和随机数实验。
4. **AWS Braket / Azure Quantum**：需要 IonQ、Quantinuum、Rigetti 等硬件时再选。
5. **QKD**：不要在这类门模型云平台上找；使用 QKD 模拟器或真实 QKD 设备/试验网。

平台额度、可用硬件和地区限制会变化，注册前以各平台当前页面为准。

## 方案 A：Qiskit Aer 本地模拟

```bash
python -m pip install qiskit qiskit-aer
python scripts/quantum_entropy.py --shots 4096 --output quantum_entropy.bin
```

脚本执行 `H` + `measure` 电路，把测量结果打包成字节，并打印 0/1 比例和 64-bit seed。

## 方案 B：IBM Quantum 真机

1. 注册 IBM Quantum 账号并创建 API token。
2. 安装并设置 token：

```bash
python -m pip install qiskit qiskit-ibm-runtime
export IBM_QUANTUM_TOKEN="your-token"
```

3. 选择可用 backend 运行：

```bash
python scripts/quantum_entropy.py \
  --backend ibm \
  --backend-name ibm_brisbane \
  --shots 4096 \
  --output quantum_entropy.bin
```

4. 把生成的 64-bit seed 传给模拟器：

```bash
./build/cuda-release/civ_main <quantum-seed>
```

这是最小侵入的接入方式：量子硬件只负责产生熵，经典模拟仍然可复现。

## 接入 C++ 的两种方式

### 方式 1：只用量子 seed（推荐先做）

```text
量子电路/云后端 -> quantum_entropy.bin -> 64-bit seed -> civ_main <seed>
```

不需要改 C++，适合先验证量子硬件和统计质量。

### 方式 2：批量量子熵池

让 Python 生成一批量子随机字节，C++ 启动时读取并调用
`QuantumRNG::mix_entropy()`。不要在每次生成 token 时调用云量子接口，延迟会非常高。

```text
quantum_entropy.bin (4096 bytes)
        |
        v
QuantumRNG::mix_entropy(batch)
        |
        v
13 个 Agent 的 seed / sampling
```

## 统计验证

至少检查：

- 0/1 比例是否接近 50%；
- 相邻 bit 的相关性；
- 字节/块分布是否均匀；
- 多次运行的 seed 是否不同；
- 使用 NIST STS、Dieharder 或 PractRand 做更严格检验。

样本量建议从 1 MiB 以上开始。云真机排队和 shots 限制会显著影响吞吐。

## QKD 怎么测试

真正的 QKD 不能只靠 Qiskit 电路测试。可选路径：

1. **QKD 模拟器**：用 QuTiP/SimQKD 模拟 BB84 的基选择、误码率和窃听。
2. **QKD 设备/试验网**：需要两台端点、量子信道、经典认证信道和专用设备。
3. **项目内验证**：把 QKD 模拟器产生的密钥作为 `generate_qkd_key()` 的输入，
   验证后续帧加密/校验流程；不要把它宣传成“真实 QKD”。

## 成本与安全

- 云量子真机通常有免费额度、排队和 shots 限制。
- 不要把云 token 写入仓库或日志。
- 量子随机数不等于密码学安全的密钥；生产密钥仍需 CSPRNG/KDF 和密钥管理。

## 本机 Aer 验证结果

本项目已在本地用 `qiskit 2.6.0` + `qiskit-aer 0.17.2` 跑过：

```text
shots=4096
bytes=512
ones_ratio=0.4961
seed=1575472452266220787
```

使用该 seed 运行 `civ_main` 成功完成了 13 因子模拟。注意：这是量子电路**模拟器**结果，
不是云真机结果。

## 云平台尝试记录（2026-10-19）

- qBraid 的设备接口返回 `401 Unauthorized`，需要注册账号/API key 后才能提交任务。
- 本源量子云主页可以访问，但“本源悟空”真机和在线编程工作台需要登录账号。
- IBM Quantum 需要 IBM 账号和 API token。

其中 qBraid 和 IBM Quantum 仍需要账号/token；本源量子云已用 API key 完成真机测试，
结果见下一节。

## 本源量子云实测

拿到 API key 后，使用 `pyqpanda3.qcloud` 成功列出后端：

```text
{'HanYuan_01': False, 'PQPUMESH8': True, 'WK_C180': True,
 'WK_C180_2': True, 'WK_C180_3': True, 'full_amplitude': True,
 'partial_amplitude': True, 'single_amplitude': True}
```

测试命令：

```bash
export ORIGIN_API_KEY="你的 key"
python scripts/originqc_probe.py --list
python scripts/originqc_probe.py --backend full_amplitude --shots 1024
python scripts/originqc_probe.py --backend WK_C180 --shots 100
```

实测结果：

| 后端 | Job ID | Shots | 结果 |
| --- | --- | --- | --- |
| `full_amplitude` | `C2676C4E365927D098F20B3978151332` | 1024 | 0/1 概率约 `0.5049 / 0.4951` |
| `WK_C180`（本源悟空真机） | `F4D64E71FC7694621E21434BA301FAAA` | 100 | 0/1 概率约 `0.4568 / 0.5432`，QPU 运行 47 ms |

注意：当前接口返回的是概率；`get_counts()` 为空，`prob_count_raw` 未返回。
因此本次硬件结果用于验证真机链路和派生可复现 seed，不应直接当作密码学 QRNG。
用该次硬件结果派生 seed `1172716508096753925`，`civ_main` 已完成 13 因子 GPU 模拟。

## 批量单比特实测

批量任务的最小 shots 是 **10**，`shots=1` 会失败：

```text
field 'shot' has over the range: [10, 49600]
```

批量提交使用 `scripts/originqc_batch.py`。关闭读取误差缓解时，返回概率是
`1/shots` 的整数倍，可以还原为原始 counts：

```bash
export ORIGIN_API_KEY="你的 key"

python scripts/originqc_batch.py \
  --backend WK_C180 --programs 16 --shots 10 \
  --output originqc_batch_raw.json
```

本次成功任务：

```text
job_id: 6A1711B89D72896ABE4357FD048C41C4
programs: 16
shots/program: 10
total_counts: {"0": 79, "1": 81}
shot_ones_ratio: 0.50625
majority_bits: 1011011001100100
derived_seed: 7264055436326997056
```

该 seed 已用于 `civ_main`，PostgreSQL `run_id=31`，13 因子 GPU 模拟完成。

如果加 `--amend`，平台会做读取误差缓解，概率变成校准值（例如
`0.6479177`），不再是整数 counts；适合做误差缓解实验，不适合直接当原始 bit。
