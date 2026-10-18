FROM nvidia/cuda:12.4-devel-ubuntu22.04

RUN apt-get update && apt-get install -y \
    build-essential cmake git \
    libpq-dev postgresql-client \
    libibverbs-dev librdmacm-dev rdma-core \
    && rm -rf /var/lib/apt/lists/*

WORKDIR /workspace
COPY . .

RUN bash scripts/build.sh

ENV CIV_LLAMA_MODEL=/workspace/models/Spark-X2.5-4B-Q4_K_M.gguf
ENV CIV_N_CTX=512
ENV CIV_N_GPU_LAYERS=99

CMD ["./build/civ_main", "42"]
