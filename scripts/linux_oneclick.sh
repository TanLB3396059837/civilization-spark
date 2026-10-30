#!/usr/bin/env bash
# Civilization Spark: Linux GPU/CPU one-click build and run.
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "$ROOT"

MODEL_DIR="$ROOT/models"
MODEL_NAME="Spark-X2.5-4B-Q4_K_M.gguf"
MODEL_PATH="${CIV_LLAMA_MODEL:-$MODEL_DIR/$MODEL_NAME}"
BUILD_DIR="$ROOT/build-linux"
INSTALL_DEPS=1
RUN_BENCH=0
RUN_CUDA_TEST=0
NO_MODEL=0
USE_POSTGRES=1
USE_ROUNDTABLE=0
USE_AMPHOREUS=0

for arg in "$@"; do
    case "$arg" in
        --skip-install) INSTALL_DEPS=0 ;;
        --benchmark)    RUN_BENCH=1 ;;
        --cuda-test)    RUN_CUDA_TEST=1 ;;
        --no-model)     NO_MODEL=1 ;;
        --no-postgres)  USE_POSTGRES=0 ;;
        --roundtable)   USE_ROUNDTABLE=1 ;;
        --amphoreus)    USE_AMPHOREUS=1 ;;
        -h|--help)
            echo "用法: $0 [--skip-install] [--no-model] [--no-postgres] [--roundtable] [--amphoreus] [--benchmark] [--cuda-test]"
            exit 0
            ;;
        *) echo "未知参数: $arg"; exit 2 ;;
    esac
done

SUDO=""
if [ "$(id -u)" -ne 0 ]; then SUDO="sudo"; fi

as_postgres() {
    if [ "$(id -u)" -eq 0 ]; then
        su postgres -s /bin/bash -c "$1"
    else
        sudo -u postgres bash -c "$1"
    fi
}

echo "========== Civilization Spark 环境检查 =========="
echo "OS:      $(. /etc/os-release 2>/dev/null && echo "$PRETTY_NAME" || uname -a)"
echo "CPU:     $(nproc) threads"
echo "RAM:     $(free -h 2>/dev/null | awk '/^Mem:/{print $2}' || echo unknown)"
if command -v nvidia-smi >/dev/null 2>&1; then
    nvidia-smi --query-gpu=name,memory.total,driver_version --format=csv,noheader || true
fi

if [ "$INSTALL_DEPS" -eq 1 ]; then
    echo "[1/5] 安装依赖"
    if command -v apt-get >/dev/null 2>&1; then
        $SUDO apt-get update
        $SUDO apt-get install -y build-essential cmake ninja-build git curl \
            libpq-dev librdmacm-dev libibverbs-dev rdma-core
        if ! command -v nvcc >/dev/null 2>&1; then
            $SUDO apt-get install -y nvidia-cuda-toolkit || \
                echo "警告: 自动安装 CUDA 失败，将构建 CPU 版本"
        fi
        if ! command -v psql >/dev/null 2>&1; then
            $SUDO apt-get install -y postgresql
        fi
    else
        echo "非 Debian/Ubuntu 系统，请手动安装 cmake、ninja、libpq、CUDA 工具链"
    fi
fi

if [ "$NO_MODEL" -eq 0 ] && [ ! -f "$MODEL_PATH" ]; then
    echo "[2/5] 下载模型 $MODEL_NAME"
    mkdir -p "$MODEL_DIR"
    curl -L --fail --retry 5 --retry-delay 3 -C - \
        -o "$MODEL_PATH.tmp" \
        "https://modelscope.cn/models/XHToken/Spark-X2.5-4B-GGUF/resolve/master/$MODEL_NAME" || \
    curl -L --fail --retry 5 --retry-delay 3 -C - \
        -o "$MODEL_PATH.tmp" \
        "https://hf-mirror.com/XHToken/Spark-X2.5-4B-GGUF/resolve/main/$MODEL_NAME"
    mv "$MODEL_PATH.tmp" "$MODEL_PATH"
fi

if [ "$NO_MODEL" -eq 0 ]; then
    size=$(stat -c%s "$MODEL_PATH" 2>/dev/null || echo 0)
    if [ "$size" -lt 2500000000 ]; then
        echo "模型文件不完整: $MODEL_PATH ($size bytes)"
        echo "请删除后重新运行本脚本下载。"
        exit 1
    fi
    export CIV_LLAMA_MODEL="$MODEL_PATH"
fi

if command -v nvidia-smi >/dev/null 2>&1 && command -v nvcc >/dev/null 2>&1; then
    ENABLE_CUDA=ON
    ARCH="$(nvidia-smi --query-gpu=compute_cap --format=csv,noheader | head -1 | tr -d ' .')"
    if [ -z "$ARCH" ]; then ARCH=86; fi
else
    ENABLE_CUDA=OFF
    ARCH=86
fi

echo "[3/5] 构建 (CUDA=$ENABLE_CUDA, arch=$ARCH)"
PG_ROOT_ARG=()
if [ -n "${PostgreSQL_ROOT:-}" ]; then
    PG_ROOT_ARG=(-DPostgreSQL_ROOT="$PostgreSQL_ROOT")
fi
cmake -S . -B "$BUILD_DIR" -G Ninja \
    -DCMAKE_BUILD_TYPE=Release \
    -DCIV_ENABLE_CUDA="$ENABLE_CUDA" \
    -DCMAKE_CUDA_ARCHITECTURES="$ARCH" \
    "${PG_ROOT_ARG[@]}"
cmake --build "$BUILD_DIR" --parallel "$(nproc)"

if [ "$USE_POSTGRES" -eq 1 ]; then
    echo "[4/5] 准备 PostgreSQL"
    if [ -z "${CIV_PG_DSN:-}" ] && command -v psql >/dev/null 2>&1; then
        if ! pg_isready -h 127.0.0.1 -p 5432 >/dev/null 2>&1; then
            $SUDO systemctl start postgresql 2>/dev/null || true
        fi
        if pg_isready -h 127.0.0.1 -p 5432 >/dev/null 2>&1; then
            as_postgres "psql -tc \"SELECT 1 FROM pg_roles WHERE rolname='civ'\"" \
                | grep -q 1 || as_postgres "psql -c \"CREATE ROLE civ LOGIN PASSWORD 'civpass';\""
            as_postgres "psql -tc \"SELECT 1 FROM pg_database WHERE datname='civ'\"" \
                | grep -q 1 || as_postgres "createdb -O civ civ"
            export CIV_PG_DSN="host=127.0.0.1 port=5432 dbname=civ user=civ password=civpass"
        fi
    fi
fi

VRAM="$(nvidia-smi --query-gpu=memory.total --format=csv,noheader,nounits 2>/dev/null | head -1 || echo 0)"
if [ "${VRAM:-0}" -le 6144 ] 2>/dev/null; then
    DEFAULT_CTX=512
elif [ "${VRAM:-0}" -le 8192 ] 2>/dev/null; then
    DEFAULT_CTX=1024
elif [ "${VRAM:-0}" -le 24576 ] 2>/dev/null; then
    DEFAULT_CTX=2048
elif [ "${VRAM:-0}" -le 49152 ] 2>/dev/null; then
    DEFAULT_CTX=4096
else
    DEFAULT_CTX=8192
fi
export CIV_N_CTX="${CIV_N_CTX:-$DEFAULT_CTX}"
export CIV_N_GPU_LAYERS="${CIV_N_GPU_LAYERS:-$([ "$ENABLE_CUDA" = ON ] && echo 99 || echo 0)}"
export CIV_SELF_TRAIN="${CIV_SELF_TRAIN:-1}"
mkdir -p "$ROOT/training_data"
export CIV_TRAINING_EXPORT="${CIV_TRAINING_EXPORT:-$ROOT/training_data/self_training_$(date +%Y%m%d_%H%M%S).jsonl}"
if [ "$USE_ROUNDTABLE" -eq 1 ]; then
    export CIV_DEBATE_MODE=roundtable
fi
if [ "$USE_AMPHOREUS" -eq 1 ]; then
    export CIV_WORLD_MODE=amphoreus
fi

echo "[5/5] 运行 (n_ctx/agent=$CIV_N_CTX, gpu_layers=$CIV_N_GPU_LAYERS)"
echo "训练样本导出: $CIV_TRAINING_EXPORT"
if [ "$RUN_CUDA_TEST" -eq 1 ] && [ -x "$BUILD_DIR/tests/civ_cuda_tests" ]; then
    "$BUILD_DIR/tests/civ_cuda_tests"
fi
if [ "$RUN_BENCH" -eq 1 ]; then
    "$BUILD_DIR/civ_bench"
else
    "$BUILD_DIR/civ_main" 42
fi
