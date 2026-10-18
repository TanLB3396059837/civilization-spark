# Contributing

[English](CONTRIBUTING.md) · [简体中文](CONTRIBUTING.zh-CN.md) · [Русский](CONTRIBUTING.ru.md)

Thank you for contributing to Civilization Spark.

## Development environment

- C++23 compiler (MSVC 2022/2026, GCC, or Clang)
- CMake 3.25+
- Ninja
- Optional: CUDA 12.x, PostgreSQL 18 + libpq
- Model files are not committed; download them as described in the README

## Local checks

```bash
cmake --preset cpu-release
cmake --build --preset cpu-release
ctest --test-dir build/cpu-release --output-on-failure
```

With a GPU:

```bash
cmake --preset cuda-release
cmake --build --preset cuda-release
ctest --test-dir build/cuda-release --output-on-failure
```

## Pull request guidelines

- Keep one PR focused on one topic.
- Do not commit `models/*.gguf`, `build/`, `cmake-build-*`, or log files.
- Add tests for new behavior; describe reproduction steps for bug fixes.
- Run the CPU tests before submitting; run `civ_cuda_tests` when CUDA is affected.

## Code style

- 4-space indentation, 100-column line width.
- Public interfaces go in `include/civ/`; implementations go in `src/`.
- Prefer exceptions or explicit error objects over silent failures.
