#!/bin/bash
set -e

if [ ! -d "third_party/llama.cpp" ]; then
    git clone --depth 1 https://github.com/ggerganov/llama.cpp.git third_party/llama.cpp
fi

mkdir -p build && cd build
cmake .. -DCMAKE_BUILD_TYPE=Release
cmake --build . --parallel $(nproc)
echo "构建完成：./build/civ_main"