cmake_minimum_required(VERSION 3.20)

project(nccl_policy_plugin LANGUAGES C CXX)

set(CMAKE_C_STANDARD 11)
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)

find_package(CUDAToolkit REQUIRED)
find_program(CLANG_EXECUTABLE NAMES clang REQUIRED)
find_program(LLVM_CONFIG_15_EXECUTABLE NAMES llvm-config-15 REQUIRED)

set(BPFTIME_SOURCE_DIR ${CMAKE_CURRENT_SOURCE_DIR}/../../bpftime)
set(BPFTIME_BUILD_DIR ${CMAKE_CURRENT_SOURCE_DIR}/../../build-bpftime CACHE PATH
    "Path to the prebuilt bpftime build directory")

function(require_file file_path)
  if(NOT EXISTS "${file_path}")
    message(FATAL_ERROR "Required file not found: ${file_path}")
  endif()
endfunction()

execute_process(
    COMMAND ${LLVM_CONFIG_15_EXECUTABLE} --libdir
    OUTPUT_VARIABLE LLVM_15_LIBDIR
    OUTPUT_STRIP_TRAILING_WHITESPACE)
find_library(LLVM_15_LIBRARY NAMES LLVM-15 PATHS ${LLVM_15_LIBDIR} REQUIRED)

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

set(POLICY_SOURCE_DIR ${CMAKE_CURRENT_SOURCE_DIR}/../ebpf-policies)
set(POLICY_BINARY_DIR ${CMAKE_CURRENT_BINARY_DIR}/ebpf-policies)

function(add_policy_object target_name source_name output_var)
  set(output_path ${POLICY_BINARY_DIR}/${source_name}.bpf.o)
  add_custom_command(
      OUTPUT ${output_path}
      COMMAND ${CMAKE_COMMAND} -E make_directory ${POLICY_BINARY_DIR}
      COMMAND ${CLANG_EXECUTABLE} -target bpf -O2 -g -I${POLICY_SOURCE_DIR}
              -c ${POLICY_SOURCE_DIR}/${source_name}.bpf.c -o ${output_path}
      DEPENDS ${POLICY_SOURCE_DIR}/${source_name}.bpf.c
              ${POLICY_SOURCE_DIR}/bpf_compat.h
              ${POLICY_SOURCE_DIR}/policy_action.h
              ${POLICY_SOURCE_DIR}/policy_context.h
              ${POLICY_SOURCE_DIR}/policy_maps.h
      VERBATIM)
  set(${output_var} ${output_path} PARENT_SCOPE)
endfunction()

add_policy_object(noop_policy noop NOOP_BPF_O)
add_policy_object(size_aware_policy size_aware SIZE_AWARE_BPF_O)
add_policy_object(size_aware_v2_policy size_aware_v2 SIZE_AWARE_V2_BPF_O)
add_policy_object(adaptive_channels_policy adaptive_channels
                  ADAPTIVE_CHANNELS_BPF_O)
add_policy_object(slo_enforcer_policy slo_enforcer SLO_ENFORCER_BPF_O)
add_policy_object(bad_lookup_policy bad_lookup BAD_LOOKUP_BPF_O)

set(POLICY_OBJECTS
    ${NOOP_BPF_O}
    ${SIZE_AWARE_BPF_O}
    ${SIZE_AWARE_V2_BPF_O}
    ${ADAPTIVE_CHANNELS_BPF_O}
    ${SLO_ENFORCER_BPF_O}
    ${BAD_LOOKUP_BPF_O})

add_custom_target(ebpf_policies ALL DEPENDS ${POLICY_OBJECTS})
configure_file(${CMAKE_CURRENT_SOURCE_DIR}/test/policy_test_paths.h.in
               ${CMAKE_CURRENT_BINARY_DIR}/policy_test_paths.h @ONLY)

add_library(native_policy_baseline STATIC native_baseline.cpp)
target_include_directories(native_policy_baseline PRIVATE ${POLICY_SOURCE_DIR})

add_library(nccl-policy SHARED plugin.cpp)
add_dependencies(nccl-policy ebpf_policies)
target_include_directories(nccl-policy PRIVATE
    ${NCCL_PUBLIC_INCLUDE_DIRS}
    ${POLICY_SOURCE_DIR}
    ${BPFTIME_SOURCE_DIR}/runtime/include
    ${BPFTIME_SOURCE_DIR}/runtime
    ${BPFTIME_SOURCE_DIR}/vm/compat/include
    ${BPFTIME_SOURCE_DIR}/bpftime-verifier/include
    ${BPFTIME_SOURCE_DIR}/bpftime-verifier/ebpf-verifier/src
    ${BPFTIME_SOURCE_DIR}/vm/include
    ${BPFTIME_SOURCE_DIR}/vm/vm-core/include
    ${BPFTIME_SOURCE_DIR}/third_party/spdlog/include)
target_link_libraries(nccl-policy PRIVATE
    "-Wl,--start-group"
    ${BPFTIME_STATIC_ARCHIVES}
    "-Wl,--end-group"
    ${LLVM_15_LIBRARY}
    dl
    elf
    m
    pthread
    rt
    z)
set_target_properties(nccl-policy PROPERTIES
    OUTPUT_NAME "nccl-policy"
    PREFIX "lib")

add_executable(test_ebpf_plugin test/test_ebpf_plugin.c)
add_dependencies(test_ebpf_plugin nccl-policy ebpf_policies
                 native_policy_baseline)
set_source_files_properties(test/test_ebpf_plugin.c PROPERTIES LANGUAGE CXX)
target_include_directories(test_ebpf_plugin PRIVATE
    ${NCCL_PUBLIC_INCLUDE_DIRS}
    ${POLICY_SOURCE_DIR}
    ${CMAKE_CURRENT_BINARY_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(test_ebpf_plugin PRIVATE
    native_policy_baseline
    dl
    rt)
set_target_properties(test_ebpf_plugin PROPERTIES LINKER_LANGUAGE CXX)
