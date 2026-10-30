#!/usr/bin/env bash
# Reproducible GPU acceptance run for RTX PRO 6000 / Blackwell and similar nodes.
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "$ROOT"

BUILD_DIR="${CIV_GPU_BUILD_DIR:-$ROOT/build-linux-gpu}"
MODEL_PATH="${CIV_LLAMA_MODEL:-$ROOT/models/Spark-X2.5-4B-Q4_K_M.gguf}"
ROUNDS="${CIV_ACCEPT_ROUNDS:-1}"
N_CTX="${CIV_N_CTX:-8192}"
N_GPU_LAYERS="${CIV_N_GPU_LAYERS:-99}"
TRAINING_EXPORT="${CIV_TRAINING_EXPORT:-$ROOT/training_data/acceptance_$(date +%Y%m%d_%H%M%S).jsonl}"
MIN_GPU_UTIL="${CIV_MIN_GPU_UTIL:-95}"

if [ ! -f "$MODEL_PATH" ]; then
    echo "模型不存在: $MODEL_PATH" >&2
    echo "先运行 scripts/linux_oneclick.sh 下载模型，或设置 CIV_LLAMA_MODEL。" >&2
    exit 1
fi
if ! command -v nvidia-smi >/dev/null 2>&1; then
    echo "未找到 nvidia-smi，无法执行 GPU 验收。" >&2
    exit 1
fi
if ! command -v nvcc >/dev/null 2>&1; then
    echo "未找到 nvcc，无法构建 CUDA 后端。" >&2
    exit 1
fi

echo "========== GPU acceptance environment =========="
uname -a
nproc
free -h
nvidia-smi
echo "CUDA compiler: $(nvcc --version | tail -1)"

CAP="$(nvidia-smi --query-gpu=compute_cap --format=csv,noheader | head -1 | tr -d ' .')"
if [ -z "$CAP" ]; then
    CAP="${CIV_CUDA_ARCH:-120}"
fi
if [ "$CAP" -ge 100 ] 2>/dev/null; then
    :
elif [ "$CAP" -lt 50 ] 2>/dev/null; then
    CAP="${CAP}0"
fi
echo "CUDA arch: $CAP"

PG_ROOT_ARG=()
if [ -n "${CIV_POSTGRES_ROOT:-}" ]; then
    PG_ROOT_ARG=(-DPostgreSQL_ROOT="$CIV_POSTGRES_ROOT")
fi
cmake -S . -B "$BUILD_DIR" -G Ninja \
    -DCMAKE_BUILD_TYPE=Release \
    -DCIV_ENABLE_LLAMA=ON \
    -DCIV_ENABLE_CUDA=ON \
    -DCMAKE_CUDA_ARCHITECTURES="$CAP" \
    -DCIV_ENABLE_POSTGRES=ON \
    "${PG_ROOT_ARG[@]}"
cmake --build "$BUILD_DIR" --parallel "$(nproc)" \
    --target civ_main civ_agent_tests civ_storage_tests civ_cuda_tests

ctest --test-dir "$BUILD_DIR" --output-on-failure

mkdir -p "$(dirname "$TRAINING_EXPORT")"
export CIV_LLAMA_MODEL="$MODEL_PATH"
export CIV_N_CTX="$N_CTX"
export CIV_N_GPU_LAYERS="$N_GPU_LAYERS"
export CIV_DEBATE_MODE=roundtable
export CIV_WORLD_MODE=amphoreus
export CIV_MAX_ROUNDS="$ROUNDS"
export CIV_SELF_TRAIN=1
export CIV_TRAINING_EXPORT="$TRAINING_EXPORT"

LOG="$ROOT/gpu_acceptance_$(date +%Y%m%d_%H%M%S).log"
GPU_CSV="$ROOT/gpu_utilization_$(date +%Y%m%d_%H%M%S).csv"
nvidia-smi --query-gpu=index,utilization.gpu,memory.used \
    --format=csv,noheader,nounits -lms 500 > "$GPU_CSV" &
MONITOR_PID=$!
set +e
"$BUILD_DIR/civ_main" 42 2>&1 | tee "$LOG"
RUN_STATUS=${PIPESTATUS[0]}
kill "$MONITOR_PID" 2>/dev/null || true
wait "$MONITOR_PID" 2>/dev/null || true
set -e

GPU_SUMMARY="$(awk -F',' '
    $2 + 0 > 5 {
        sum += $2 + 0;
        count++;
        if (($2 + 0) > peak) peak = $2 + 0;
    }
    END {
        if (count == 0) printf "0 0";
        else printf "%.2f %.2f", sum / count, peak;
    }
' "$GPU_CSV")"
GPU_AVG="$(printf '%s\n' "$GPU_SUMMARY" | awk '{print $1}')"
GPU_PEAK="$(printf '%s\n' "$GPU_SUMMARY" | awk '{print $2}')"
echo "GPU utilization: avg=${GPU_AVG}% peak=${GPU_PEAK}% target>=${MIN_GPU_UTIL}%"

if [ "$RUN_STATUS" -ne 0 ]; then
    echo "GPU run failed with status $RUN_STATUS" >&2
    exit "$RUN_STATUS"
fi
if awk -v avg="$GPU_AVG" -v min="$MIN_GPU_UTIL" \
    'BEGIN { exit !(avg < min) }'; then
    echo "GPU utilization target not met; inspect $GPU_CSV and $LOG" >&2
    exit 3
fi

if [ -n "${CIV_PG_DSN:-}" ] && command -v psql >/dev/null 2>&1; then
    RUN_ID="$(psql "$CIV_PG_DSN" -Atc "SELECT max(run_id) FROM runs")"
    echo "========== PostgreSQL acceptance (run_id=$RUN_ID) =========="
    psql "$CIV_PG_DSN" -v ON_ERROR_STOP=1 -c "
SELECT
  (SELECT count(*) FROM conversation_messages m
     JOIN conversations c USING (conversation_id)
    WHERE c.run_id = $RUN_ID) AS messages,
  (SELECT count(*) FROM agent_memory WHERE run_id = $RUN_ID) AS memories,
  (SELECT count(*) FROM agent_cognition WHERE run_id = $RUN_ID) AS cognition,
  (SELECT count(*) FROM agent_consciousness WHERE run_id = $RUN_ID) AS meta_cognition,
  (SELECT count(*) FROM training_samples WHERE run_id = $RUN_ID) AS samples,
  (SELECT count(*) FROM training_samples
    WHERE run_id = $RUN_ID AND accepted) AS accepted,
  (SELECT count(*) FROM self_training_runs
    WHERE run_id = $RUN_ID AND status = 'finished') AS finished_training_runs;"
fi

echo "Log: $LOG"
echo "GPU samples: $GPU_CSV"
echo "Training data: $TRAINING_EXPORT"
