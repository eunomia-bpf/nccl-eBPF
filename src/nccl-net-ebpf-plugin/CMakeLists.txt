cmake_minimum_required(VERSION 3.20)

project(nccl_net_ebpf_plugin LANGUAGES C CXX)

set(CMAKE_C_STANDARD 11)
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)

find_package(CUDAToolkit REQUIRED)
find_path(LIBBPF_INCLUDE_DIR NAMES bpf/libbpf.h REQUIRED)
find_path(LIBELF_INCLUDE_DIR NAMES gelf.h REQUIRED)
find_program(LLVM_CONFIG_EXECUTABLE NAMES llvm-config-15 llvm-config REQUIRED)

set(BPFTIME_SOURCE_DIR ${CMAKE_CURRENT_SOURCE_DIR}/../../bpftime)
set(BPFTIME_BUILD_DIR ${CMAKE_CURRENT_SOURCE_DIR}/../../build-bpftime CACHE PATH
    "Path to the prebuilt bpftime build directory")

function(require_file file_path)
  if(NOT EXISTS "${file_path}")
    message(FATAL_ERROR "Required file not found: ${file_path}")
  endif()
endfunction()

execute_process(
    COMMAND ${LLVM_CONFIG_EXECUTABLE} --version
    OUTPUT_VARIABLE LLVM_VERSION
    OUTPUT_STRIP_TRAILING_WHITESPACE)
string(REGEX MATCH "^[0-9]+" LLVM_VERSION_MAJOR "${LLVM_VERSION}")

find_program(CLANG_EXECUTABLE NAMES clang-${LLVM_VERSION_MAJOR} clang REQUIRED)

execute_process(
    COMMAND ${LLVM_CONFIG_EXECUTABLE} --libdir
    OUTPUT_VARIABLE LLVM_LIBDIR
    OUTPUT_STRIP_TRAILING_WHITESPACE)
find_library(LLVM_LIBRARY NAMES LLVM-${LLVM_VERSION_MAJOR} LLVM
             PATHS ${LLVM_LIBDIR} REQUIRED)

set(BPFTIME_RUNTIME_LIBRARY ${BPFTIME_BUILD_DIR}/runtime/libruntime.a)
set(BPFTIME_VERIFIER_LIBRARY
    ${BPFTIME_BUILD_DIR}/bpftime-verifier/libbpftime-verifier.a)
set(BPFTIME_EBPF_VERIFIER_LIBRARY
    ${BPFTIME_BUILD_DIR}/bpftime-verifier/ebpf-verifier/libebpfverifier.a)
set(BPFTIME_BTF_LIBRARY
    ${BPFTIME_BUILD_DIR}/bpftime-verifier/ebpf-verifier/external/libbtf/libbtf/liblibbtf.a)
set(BPFTIME_VM_CORE_LIBRARY
    ${BPFTIME_BUILD_DIR}/vm/vm-core/libbpftime_vm.a)
set(BPFTIME_LLVM_COMPAT_LIBRARY
    ${BPFTIME_BUILD_DIR}/vm/compat/llvm-vm/libbpftime_llvm_vm.a)
set(BPFTIME_LLVM_JIT_LIBRARY
    ${BPFTIME_BUILD_DIR}/vm/compat/llvm-vm/libllvmbpf_vm.a)
set(BPFTIME_SPDLOG_LIBRARY
    ${BPFTIME_BUILD_DIR}/third_party/spdlog/libspdlog.a)
set(BPFTIME_FRIDA_GUM_LIBRARY
    ${BPFTIME_BUILD_DIR}/FridaGum-prefix/src/FridaGum/libfrida-gum.a)
set(BPFTIME_LIBBPF_LIBRARY ${BPFTIME_BUILD_DIR}/libbpf/libbpf.a)

require_file(${BPFTIME_RUNTIME_LIBRARY})
require_file(${BPFTIME_VERIFIER_LIBRARY})
require_file(${BPFTIME_EBPF_VERIFIER_LIBRARY})
require_file(${BPFTIME_BTF_LIBRARY})
require_file(${BPFTIME_VM_CORE_LIBRARY})
require_file(${BPFTIME_LLVM_COMPAT_LIBRARY})
require_file(${BPFTIME_LLVM_JIT_LIBRARY})
require_file(${BPFTIME_SPDLOG_LIBRARY})
require_file(${BPFTIME_FRIDA_GUM_LIBRARY})
require_file(${BPFTIME_LIBBPF_LIBRARY})

set(BPFTIME_STATIC_ARCHIVES
    ${BPFTIME_RUNTIME_LIBRARY}
    ${BPFTIME_VERIFIER_LIBRARY}
    ${BPFTIME_EBPF_VERIFIER_LIBRARY}
    ${BPFTIME_BTF_LIBRARY}
    ${BPFTIME_VM_CORE_LIBRARY}
    ${BPFTIME_LLVM_COMPAT_LIBRARY}
    ${BPFTIME_LLVM_JIT_LIBRARY}
    ${BPFTIME_SPDLOG_LIBRARY}
    ${BPFTIME_FRIDA_GUM_LIBRARY}
    ${BPFTIME_LIBBPF_LIBRARY})

set(NCCL_PUBLIC_INCLUDE_DIRS
    ${CMAKE_CURRENT_SOURCE_DIR}/../../nccl/build/include
    ${CMAKE_CURRENT_SOURCE_DIR}/../../nccl/src/include
    ${CMAKE_CURRENT_SOURCE_DIR}/../../nccl/src/include/plugin
    ${CUDAToolkit_INCLUDE_DIRS})

set(BPF_COMPAT_DIR ${CMAKE_CURRENT_SOURCE_DIR}/../ebpf-policies)
set(BPF_SOURCE ${CMAKE_CURRENT_SOURCE_DIR}/net_trace.bpf.c)
set(BPF_OBJECT ${CMAKE_CURRENT_BINARY_DIR}/net_trace.bpf.o)

add_custom_command(
    OUTPUT ${BPF_OBJECT}
    COMMAND ${CLANG_EXECUTABLE} -target bpf -O2 -g
            -I${CMAKE_CURRENT_SOURCE_DIR}
            -I${BPF_COMPAT_DIR}
            -c ${BPF_SOURCE} -o ${BPF_OBJECT}
    DEPENDS ${BPF_SOURCE}
            ${CMAKE_CURRENT_SOURCE_DIR}/net_ebpf_ctx.h
            ${BPF_COMPAT_DIR}/bpf_compat.h
    VERBATIM)
add_custom_target(nccl_net_ebpf_bpf ALL DEPENDS ${BPF_OBJECT})

configure_file(${CMAKE_CURRENT_SOURCE_DIR}/plugin_paths.h.in
               ${CMAKE_CURRENT_BINARY_DIR}/plugin_paths.h @ONLY)

add_library(nccl-net-ebpf SHARED plugin.cpp)
add_dependencies(nccl-net-ebpf nccl_net_ebpf_bpf)
target_include_directories(nccl-net-ebpf PRIVATE
    ${NCCL_PUBLIC_INCLUDE_DIRS}
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_CURRENT_BINARY_DIR}
    ${BPF_COMPAT_DIR}
    ${LIBBPF_INCLUDE_DIR}
    ${LIBELF_INCLUDE_DIR}
    ${BPFTIME_SOURCE_DIR}/runtime/include
    ${BPFTIME_SOURCE_DIR}/runtime
    ${BPFTIME_SOURCE_DIR}/vm/compat/include
    ${BPFTIME_SOURCE_DIR}/bpftime-verifier/include
    ${BPFTIME_SOURCE_DIR}/bpftime-verifier/ebpf-verifier/src
    ${BPFTIME_SOURCE_DIR}/vm/include
    ${BPFTIME_SOURCE_DIR}/vm/vm-core/include
    ${BPFTIME_SOURCE_DIR}/third_party/spdlog/include)
target_link_libraries(nccl-net-ebpf PRIVATE
    "-Wl,--start-group"
    ${BPFTIME_STATIC_ARCHIVES}
    "-Wl,--end-group"
    ${LLVM_LIBRARY}
    dl
    elf
    m
    pthread
    rt
    z)
set_target_properties(nccl-net-ebpf PROPERTIES
    OUTPUT_NAME "nccl-net-ebpf"
    PREFIX "lib")
