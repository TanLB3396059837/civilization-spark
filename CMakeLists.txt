cmake_minimum_required(VERSION 3.25)
project(civilization_spark VERSION 3.3.13 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# ============================================================
# ★ v3.3：用生成器表达式隔离 CXX 与 CUDA 编译选项
#   旧写法 add_compile_options(/Zc:__cplusplus) 会把 MSVC 选项传给 nvcc
#   导致 nvcc fatal error
# ============================================================
if(MSVC)
    add_compile_definitions(_CRT_SECURE_NO_WARNINGS)
    add_compile_options(
            $<$<COMPILE_LANGUAGE:CXX>:/W3>
            $<$<COMPILE_LANGUAGE:CXX>:/permissive->
            $<$<COMPILE_LANGUAGE:CXX>:/EHsc>
            $<$<COMPILE_LANGUAGE:CXX>:/utf-8>
            $<$<COMPILE_LANGUAGE:CXX>:/Zc:__cplusplus>
            $<$<COMPILE_LANGUAGE:CUDA>:-Xcompiler=/Zc:__cplusplus>
            $<$<COMPILE_LANGUAGE:CUDA>:-Xcompiler=/utf-8>
    )
    # NOMINMAX / WIN32_LEAN_AND_MEAN 已在 types.hpp 中带 #ifndef 保护
else()
    add_compile_options(-O2 -Wall -Wextra -Wno-unused-parameter)
endif()

find_package(Threads REQUIRED)

option(CIV_ENABLE_POSTGRES "Enable PostgreSQL persistence via libpq" ON)
if(CIV_ENABLE_POSTGRES)
    find_package(PostgreSQL QUIET)
    if(PostgreSQL_FOUND)
        message(STATUS "CIV: PostgreSQL persistence enabled")
    else()
        message(STATUS "CIV: PostgreSQL not found, persistence disabled")
    endif()
endif()

# ---- CUDA 可选 ----
option(CIV_ENABLE_CUDA "Enable CUDA kernels when a CUDA toolkit is available" ON)
option(CIV_ENABLE_LLAMA "Build and link the bundled llama.cpp backend" ON)
include(CheckLanguage)
if(CIV_ENABLE_CUDA)
    check_language(CUDA)
endif()
if(CIV_ENABLE_CUDA AND CMAKE_CUDA_COMPILER)
    enable_language(CUDA)
    set(CMAKE_CUDA_STANDARD 17)
    if(NOT DEFINED CMAKE_CUDA_ARCHITECTURES)
        set(CMAKE_CUDA_ARCHITECTURES 86)
    endif()
    set(CIV_HAS_CUDA ON)
    message(STATUS "CIV: CUDA enabled (arch=${CMAKE_CUDA_ARCHITECTURES})")
else()
    set(CIV_HAS_CUDA OFF)
    message(STATUS "CIV: CUDA not found, CPU-only")
endif()

# ---- RDMA ----
list(APPEND CMAKE_MODULE_PATH "${CMAKE_SOURCE_DIR}/cmake")
find_package(RDMA QUIET)

# ============================================================
# llama.cpp（GGML_CUDA，不再用 LLAMA_CUBLAS）
# ============================================================
if(CIV_ENABLE_LLAMA AND EXISTS ${CMAKE_SOURCE_DIR}/third_party/llama.cpp/CMakeLists.txt)
    set(GGML_CUDA   ${CIV_HAS_CUDA} CACHE BOOL "" FORCE)
    set(GGML_NATIVE OFF CACHE BOOL "" FORCE)
    set(GGML_CCACHE OFF CACHE BOOL "" FORCE)

    if(MSVC)
        set(GGML_OPENMP OFF CACHE BOOL "" FORCE)
    else()
        set(GGML_OPENMP ON CACHE BOOL "" FORCE)
    endif()

    set(LLAMA_BUILD_TESTS    OFF CACHE BOOL "" FORCE)
    set(LLAMA_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
    set(LLAMA_BUILD_TOOLS    OFF CACHE BOOL "" FORCE)
    set(LLAMA_CURL           OFF CACHE BOOL "" FORCE)

    if(WIN32)
        # Shared ggml/llama DLLs avoid Windows static-runtime teardown conflicts
        # with libpq while still supporting full CUDA offload.
        set(BUILD_SHARED_LIBS ON CACHE BOOL "" FORCE)
    else()
        set(BUILD_SHARED_LIBS ON  CACHE BOOL "" FORCE)
    endif()

    add_subdirectory(third_party/llama.cpp)
    set(CIV_HAS_LLAMA ON)
    message(STATUS "CIV: llama.cpp enabled (GGML_CUDA=${GGML_CUDA})")
else()
    set(CIV_HAS_LLAMA OFF)
    message(STATUS "CIV: llama.cpp not found, using built-in transformer")
endif()

function(civ_copy_runtime_deps target)
    if(WIN32 AND CIV_HAS_LLAMA)
        foreach(dep llama ggml ggml-base ggml-cpu)
            if(TARGET ${dep})
                add_custom_command(TARGET ${target} POST_BUILD
                        COMMAND ${CMAKE_COMMAND} -E copy_if_different
                        $<TARGET_FILE:${dep}>
                        $<TARGET_FILE_DIR:${target}>
                        VERBATIM)
            endif()
        endforeach()
    endif()
endfunction()

# ---- 核心库 ----
add_library(civ_core STATIC
        src/crc/crc8.cpp
        src/crc/crc32.cpp
        src/codec/frame_v4.cpp
        src/com/channel_factory.cpp
        src/channel/tcp_channel.cpp
        src/channel/shm_channel.cpp
        src/channel/multi_channel.cpp
        src/quantum/quantum_rng.cpp
)
target_include_directories(civ_core PUBLIC include)
target_link_libraries(civ_core PUBLIC Threads::Threads)

include(cmake/CompilerFeatures.cmake)

if(WIN32)
    target_link_libraries(civ_core PUBLIC ws2_32)
endif()

if(RDMA_FOUND)
    target_sources(civ_core PRIVATE src/channel/rocev2_channel.cpp)
    target_include_directories(civ_core PUBLIC ${RDMA_INCLUDE_DIRS})
    target_link_libraries(civ_core PUBLIC ${RDMA_LIBRARIES})
    target_compile_definitions(civ_core PUBLIC CIV_HAS_VERBS=1)
endif()

if(CIV_ENABLE_POSTGRES AND PostgreSQL_FOUND)
    add_library(civ_storage STATIC
            src/storage/postgres_store.cpp
    )
    target_link_libraries(civ_storage PUBLIC civ_core PostgreSQL::PostgreSQL)
    target_compile_definitions(civ_storage PUBLIC CIV_HAS_POSTGRES=1)
endif()

# ---- AI 库 ----
add_library(civ_ai STATIC
        src/ai/shared_model.cpp
        src/ai/batch_scheduler.cpp
        src/ai/llm_agent.cpp
)
target_link_libraries(civ_ai PUBLIC civ_core)

if(CIV_HAS_CUDA)
    target_sources(civ_ai PRIVATE
            cuda/transformer_kernels.cu
            cuda/attention_kernel.cu
    )
    set_target_properties(civ_ai PROPERTIES CUDA_SEPARABLE_COMPILATION ON)
    target_compile_definitions(civ_ai PUBLIC CIV_USE_CUDA=1)
endif()

if(CIV_HAS_LLAMA)
    if(TARGET llama)
        target_link_libraries(civ_ai PUBLIC llama)
    elseif(TARGET ggml)
        target_link_libraries(civ_ai PUBLIC ggml)
    endif()
    target_compile_definitions(civ_ai PUBLIC CIV_USE_LLAMA=1)
endif()

# ---- 文明模拟 ----
add_library(civ_sim STATIC
        src/civilization/simulation.cpp
        src/process/process_pool.cpp
)
target_link_libraries(civ_sim PUBLIC civ_ai)
if(TARGET civ_storage)
    target_link_libraries(civ_sim PUBLIC civ_storage)
endif()

# ---- 应用 ----
add_executable(civ_main apps/civilization_main.cpp)
target_link_libraries(civ_main PRIVATE civ_sim)
civ_copy_runtime_deps(civ_main)

add_executable(civ_distributed apps/distributed_main.cpp)
target_link_libraries(civ_distributed PRIVATE civ_sim)
civ_copy_runtime_deps(civ_distributed)

add_executable(civ_bench apps/benchmark_main.cpp)
target_link_libraries(civ_bench PRIVATE civ_sim)
civ_copy_runtime_deps(civ_bench)

# ---- 测试 ----
enable_testing()
add_subdirectory(tests)
