# Contributing

感谢你参与 Civilization Spark。

## 开发环境

- C++23 编译器（MSVC 2022/2026 或 GCC/Clang）
- CMake 3.25+
- Ninja
- 可选：CUDA 12.x、PostgreSQL 18 + libpq
- 模型文件不进入仓库，请按 README 下载

## 本地检查

```bash
cmake --preset cpu-release
cmake --build --preset cpu-release
ctest --test-dir build/cpu-release --output-on-failure
```

有 GPU 时：

```bash
cmake --preset cuda-release
cmake --build --preset cuda-release
ctest --test-dir build/cuda-release --output-on-failure
```

## 提交规范

- 一个 PR 聚焦一个主题。
- 不要提交 `models/*.gguf`、`build/`、`cmake-build-*` 或日志文件。
- 新功能请补测试；修复 bug 请说明复现步骤。
- 提交前至少跑一遍 CPU 测试；涉及 CUDA 时同时跑 `civ_cuda_tests`。

## 代码风格

- 4 空格缩进，列宽 100。
- 公共接口放在 `include/civ/`，实现放在 `src/`。
- 错误处理优先使用异常或返回错误对象，避免静默失败。
