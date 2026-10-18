# Участие в разработке

[English](CONTRIBUTING.md) · [简体中文](CONTRIBUTING.zh-CN.md) · [Русский](CONTRIBUTING.ru.md)

Спасибо за участие в Civilization Spark.

## Среда разработки

- Компилятор C++23 (MSVC 2022/2026, GCC или Clang)
- CMake 3.25+
- Ninja
- Опционально: CUDA 12.x, PostgreSQL 18 + libpq
- Файлы моделей не коммитятся; скачайте их согласно README

## Локальные проверки

```bash
cmake --preset cpu-release
cmake --build --preset cpu-release
ctest --test-dir build/cpu-release --output-on-failure
```

С GPU:

```bash
cmake --preset cuda-release
cmake --build --preset cuda-release
ctest --test-dir build/cuda-release --output-on-failure
```

## Правила pull request

- Один PR — одна тема.
- Не коммитьте `models/*.gguf`, `build/`, `cmake-build-*` и лог-файлы.
- Добавляйте тесты для нового поведения; для исправлений описывайте шаги воспроизведения.
- Перед отправкой запускайте CPU-тесты; при изменениях CUDA запускайте `civ_cuda_tests`.

## Стиль кода

- Отступ 4 пробела, ширина строки 100.
- Публичные интерфейсы — в `include/civ/`, реализации — в `src/`.
- Предпочитайте исключения или явные объекты ошибок молчаливым сбоям.
