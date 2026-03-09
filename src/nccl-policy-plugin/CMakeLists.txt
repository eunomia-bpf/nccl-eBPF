cmake_minimum_required(VERSION 3.20)

project(nccl_policy_plugin LANGUAGES C CXX)

set(CMAKE_C_STANDARD 11)
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)

set(BPFTIME_LLVM_JIT OFF CACHE BOOL "" FORCE)
set(BPFTIME_ENABLE_UNIT_TESTING OFF CACHE BOOL "" FORCE)
set(BUILD_LLVM_AOT_CLI OFF CACHE BOOL "" FORCE)

find_package(CUDAToolkit REQUIRED)
find_program(CLANG_EXECUTABLE NAMES clang REQUIRED)

add_subdirectory(${CMAKE_CURRENT_SOURCE_DIR}/../../bpftime/vm/llvm-jit
                 ${CMAKE_CURRENT_BINARY_DIR}/llvmbpf)

set(NCCL_PUBLIC_INCLUDE_DIRS
    ${CMAKE_CURRENT_SOURCE_DIR}/../../nccl/build/include
    ${CMAKE_CURRENT_SOURCE_DIR}/../../nccl/src/include
    ${CMAKE_CURRENT_SOURCE_DIR}/../../nccl/src/include/plugin
    ${CUDAToolkit_INCLUDE_DIRS})

set(POLICY_SOURCE_DIR ${CMAKE_CURRENT_SOURCE_DIR}/../ebpf-policies)
set(POLICY_BINARY_DIR ${CMAKE_CURRENT_BINARY_DIR}/ebpf-policies)
set(NOOP_BPF_O ${POLICY_BINARY_DIR}/noop.bpf.o)
set(SIZE_AWARE_BPF_O ${POLICY_BINARY_DIR}/size_aware.bpf.o)

add_custom_command(
    OUTPUT ${NOOP_BPF_O}
    COMMAND ${CMAKE_COMMAND} -E make_directory ${POLICY_BINARY_DIR}
    COMMAND ${CLANG_EXECUTABLE} -target bpf -O2 -g -I${POLICY_SOURCE_DIR}
            -c ${POLICY_SOURCE_DIR}/noop.bpf.c -o ${NOOP_BPF_O}
    DEPENDS ${POLICY_SOURCE_DIR}/noop.bpf.c
            ${POLICY_SOURCE_DIR}/policy_context.h
    VERBATIM)

add_custom_command(
    OUTPUT ${SIZE_AWARE_BPF_O}
    COMMAND ${CMAKE_COMMAND} -E make_directory ${POLICY_BINARY_DIR}
    COMMAND ${CLANG_EXECUTABLE} -target bpf -O2 -g -I${POLICY_SOURCE_DIR}
            -c ${POLICY_SOURCE_DIR}/size_aware.bpf.c -o ${SIZE_AWARE_BPF_O}
    DEPENDS ${POLICY_SOURCE_DIR}/size_aware.bpf.c
            ${POLICY_SOURCE_DIR}/policy_context.h
    VERBATIM)

add_custom_target(ebpf_policies ALL
                  DEPENDS ${NOOP_BPF_O} ${SIZE_AWARE_BPF_O})

add_library(nccl-policy SHARED plugin.cpp)
add_dependencies(nccl-policy ebpf_policies llvmbpf_vm)
target_include_directories(nccl-policy PRIVATE
    ${NCCL_PUBLIC_INCLUDE_DIRS}
    ${CMAKE_CURRENT_SOURCE_DIR}/../../bpftime/vm/llvm-jit/include
    ${POLICY_SOURCE_DIR})
target_link_libraries(nccl-policy PRIVATE llvmbpf_vm bpf dl)
set_target_properties(nccl-policy PROPERTIES
    OUTPUT_NAME "nccl-policy"
    PREFIX "lib")

add_executable(test_ebpf_plugin test/test_ebpf_plugin.c)
add_dependencies(test_ebpf_plugin nccl-policy ebpf_policies)
target_include_directories(test_ebpf_plugin PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/../../nccl/plugins/tuner/example/nccl)
target_link_libraries(test_ebpf_plugin PRIVATE dl rt)
target_compile_definitions(test_ebpf_plugin PRIVATE
    NCCL_POLICY_TEST_BPF_PATH=\"${SIZE_AWARE_BPF_O}\")
