add_executable(civ_tests test_core.cpp)
target_link_libraries(civ_tests PRIVATE civ_core)
add_test(NAME civ_tests COMMAND civ_tests)

add_executable(civ_agent_tests test_agents.cpp)
target_link_libraries(civ_agent_tests PRIVATE civ_ai)
add_test(NAME civ_agent_tests COMMAND civ_agent_tests)

if(TARGET civ_storage)
    add_executable(civ_storage_tests test_storage.cpp)
    target_link_libraries(civ_storage_tests PRIVATE civ_storage)
    add_test(NAME civ_storage_tests COMMAND civ_storage_tests)
endif()

if(CIV_HAS_CUDA)
    add_executable(civ_cuda_tests test_cuda.cu)
    target_link_libraries(civ_cuda_tests PRIVATE civ_ai)
    add_test(NAME civ_cuda_tests COMMAND civ_cuda_tests)
endif()
