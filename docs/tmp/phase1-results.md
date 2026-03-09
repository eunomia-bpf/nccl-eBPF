# Phase 1 Results: Minimal NCCL eBPF Tuner Plugin

## Status

Phase 1 completed.

Created:

- `src/nccl-policy-plugin/plugin.cpp`
- `src/nccl-policy-plugin/CMakeLists.txt`
- `src/nccl-policy-plugin/test/test_ebpf_plugin.c`
- `src/ebpf-policies/policy_context.h`
- `src/ebpf-policies/noop.bpf.c`
- `src/ebpf-policies/size_aware.bpf.c`

Important setup note:

- `bpftime` nested submodules were not initialized in this checkout at the start. `bpftime/vm/llvm-jit`, `bpftime/third_party/spdlog`, `bpftime/third_party/argparse`, `bpftime/third_party/ubpf`, and others were empty.
- Fix used:

```bash
git -C bpftime submodule update --init --recursive
```

## Step 1: Understand bpftime / llvmbpf Build System

Files inspected:

- `bpftime/CMakeLists.txt`
- `bpftime/vm/CMakeLists.txt`
- `bpftime/vm/compat/CMakeLists.txt`
- `bpftime/vm/compat/llvm-vm/CMakeLists.txt`
- `bpftime/vm/llvm-jit/CMakeLists.txt`
- `bpftime/vm/example/main.c`
- `bpftime/vm/llvm-jit/example/basic.cpp`
- `bpftime/vm/vm-core/include/ebpf-vm.h`

Findings:

- The actual LLVM JIT wrapper is in `bpftime/vm/compat/llvm-vm/`.
- The actual llvmbpf implementation is the nested submodule `bpftime/vm/llvm-jit/`.
- `bpftime/vm/compat/llvm-vm/CMakeLists.txt` builds:
  - `llvmbpf_vm`
  - `bpftime_llvm_vm`
- `bpftime/vm/vm-core/CMakeLists.txt` builds:
  - `bpftime_vm`
- There is a simple standalone VM example in:
  - `bpftime/vm/example/main.c` using `ebpf_create("llvm")`
  - `bpftime/vm/llvm-jit/example/basic.cpp` using `bpftime::llvmbpf_vm`

Minimal standalone llvmbpf build command:

```bash
cmake -S bpftime/vm/llvm-jit -B build-llvmbpf \
  -DCMAKE_BUILD_TYPE=Release \
  -DBPFTIME_LLVM_JIT=OFF \
  -DBPFTIME_ENABLE_UNIT_TESTING=OFF \
  -DBUILD_LLVM_AOT_CLI=OFF
cmake --build build-llvmbpf -j24 --target llvmbpf_vm
```

Result:

- Built successfully.
- Output library path: `/home/yunwei37/workspace/nccl-eBPF/libllvmbpf_vm.a`

Why the output lands there:

- `bpftime/vm/llvm-jit/CMakeLists.txt` sets `ARCHIVE_OUTPUT_DIRECTORY` to `${CMAKE_CURRENT_BINARY_DIR}/../`.
- With `build-llvmbpf` as the build dir, that resolves to the repo root.

## Step 2: Build llvmbpf

Requested command executed:

```bash
mkdir -p build-vm && cd build-vm
cmake ../bpftime/vm -DCMAKE_BUILD_TYPE=Release
make -j24
```

Result:

- Configure succeeded.
- Build succeeded.
- Key artifacts:
  - `/home/yunwei37/workspace/nccl-eBPF/build-vm/compat/llvm-vm/libllvmbpf_vm.a`
  - `/home/yunwei37/workspace/nccl-eBPF/build-vm/compat/llvm-vm/libbpftime_llvm_vm.a`
  - `/home/yunwei37/workspace/nccl-eBPF/build-vm/vm-core/libbpftime_vm.a`

Exact file sizes:

- `libllvmbpf_vm.a`: `449906` bytes
- `libbpftime_llvm_vm.a`: `734868` bytes
- `libbpftime_vm.a`: `37082` bytes

## Step 3: Create Minimal eBPF Tuner Plugin

Implementation summary:

- Exported `ncclTunerPlugin_v5`
- `init()`:
  - creates a `bpftime::llvmbpf_vm`
  - loads a hardcoded no-op eBPF bytecode fallback
  - optionally loads a `.bpf.o` policy from `NCCL_POLICY_BPF_PATH`
  - runs a one-shot warmup execution so real NCCL runs can prove eBPF execution even on a 1-GPU path
- `getCollInfo()`:
  - builds a small `nccl_policy_ctx`
  - calls the eBPF program
  - measures latency with `clock_gettime(CLOCK_MONOTONIC_RAW)`
  - prints timing to `stderr`
  - maps return values to simple algo/proto/channel choices
- `finalize()`:
  - destroys VM state
  - prints summary timing

Design choices:

- Plugin is C++ so it can use `bpftime::llvmbpf_vm` directly.
- The plugin loads `.bpf.o` files by using the same narrow libbpf API pattern that llvmbpf’s own CLI uses: open the ELF, read the first program’s instructions, then call `load_code()`.

Issues encountered and fixes:

- First build attempt failed because including `<bpf/libbpf.h>` conflicted with llvmbpf’s own `ebpf_inst.h` register enum names.
- Fix used:
  - removed the full `libbpf.h` include
  - used forward declarations plus `bpf_object__open`, `bpf_object__next_program`, `bpf_program__insns`, `bpf_program__insn_cnt`
- First runtime test failed because `ncclTunerPlugin_v5` was not exported from the shared object.
- Fix used:
  - explicit default visibility on the exported symbol

## Step 4: Write and Compile eBPF Programs

Source files:

- `src/ebpf-policies/noop.bpf.c`
- `src/ebpf-policies/size_aware.bpf.c`
- shared context header: `src/ebpf-policies/policy_context.h`

Behavior:

- `noop.bpf.c`: returns `0`
- `size_aware.bpf.c`:
  - `< 4096` bytes: returns `1`
  - `< 1 MiB`: returns `2`
  - otherwise: returns `3`

Commands executed:

```bash
cd /home/yunwei37/workspace/nccl-eBPF/src/ebpf-policies
clang -target bpf -O2 -c -I. -o noop.bpf.o noop.bpf.c
clang -target bpf -O2 -c -I. -o size_aware.bpf.o size_aware.bpf.c
```

Result:

- Both succeeded.
- Output files:
  - `/home/yunwei37/workspace/nccl-eBPF/src/ebpf-policies/noop.bpf.o`
  - `/home/yunwei37/workspace/nccl-eBPF/src/ebpf-policies/size_aware.bpf.o`

Initial failure and fix:

- First compile attempt used `<linux/bpf.h>` and `<bpf/bpf_helpers.h>`.
- That failed with:

```text
/usr/include/linux/types.h:5:10: fatal error: 'asm/types.h' file not found
```

- Fix used:
  - removed the kernel header dependency
  - replaced it with a minimal local `SEC(name)` macro

## Step 5: Build the Plugin

Build system added:

- `src/nccl-policy-plugin/CMakeLists.txt`

Build command executed:

```bash
cd /home/yunwei37/workspace/nccl-eBPF/src/nccl-policy-plugin
mkdir -p build && cd build
cmake .. -DCMAKE_BUILD_TYPE=Release
cmake --build . -j24
```

Result:

- Build succeeded.

Artifacts:

- Plugin shared library:
  - `/home/yunwei37/workspace/nccl-eBPF/src/nccl-policy-plugin/build/libnccl-policy.so`
- Benchmark harness:
  - `/home/yunwei37/workspace/nccl-eBPF/src/nccl-policy-plugin/build/test_ebpf_plugin`
- Build-tree BPF objects:
  - `/home/yunwei37/workspace/nccl-eBPF/src/nccl-policy-plugin/build/ebpf-policies/noop.bpf.o`
  - `/home/yunwei37/workspace/nccl-eBPF/src/nccl-policy-plugin/build/ebpf-policies/size_aware.bpf.o`

Notes:

- This CMake build uses the llvmbpf subdirectory directly and forces PIC so the shared plugin links cleanly.
- It uses NCCL headers from:
  - `nccl/build/include`
  - `nccl/src/include`
  - `nccl/src/include/plugin`

## Step 6: CPU-only Test Harness

Test harness:

- `src/nccl-policy-plugin/test/test_ebpf_plugin.c`

Behavior:

- `dlopen()` the plugin
- `dlsym("ncclTunerPlugin_v5")`
- `init()`
- `getCollInfo()` 1,000,000 times with varying sizes, collectives, `numPipeOps`, and `regBuff`
- compute P50 / P99 / max
- compare against a baseline empty call

Run command:

```bash
cd /home/yunwei37/workspace/nccl-eBPF/src/nccl-policy-plugin/build
./test_ebpf_plugin ./libnccl-policy.so ./ebpf-policies/size_aware.bpf.o
```

Observed plugin stderr:

```text
[nccl-policy-plugin] init warmup bytes=1024 decision=1 latency_ns=4342605
[nccl-policy-plugin] call=1 bytes=1024 decision=1 latency_ns=64
[nccl-policy-plugin] call=100000 bytes=524447 decision=2 latency_ns=15
[nccl-policy-plugin] call=1000000 bytes=1087 decision=1 latency_ns=14
[nccl-policy-plugin] finalize calls=1000000 avg_latency_ns=12 last_latency_ns=14 source=./ebpf-policies/size_aware.bpf.o
```

Benchmark results:

```text
policy path: ./ebpf-policies/size_aware.bpf.o
plugin latency ns: p50=33 p99=77 max=110372
baseline latency ns: p50=10 p99=23 max=1917
delta p50=23 delta p99=54 delta max=108455
```

Interpretation:

- Steady-state `llvmbpf_vm::exec()` overhead inside the tuner path is low tens of nanoseconds on this host.
- The one-time JIT/warmup cost is a few milliseconds and was moved into `init()`.

## Step 7: Test with Real NCCL

Command executed:

```bash
cd /home/yunwei37/workspace/nccl-eBPF
NCCL_DEBUG=INFO \
NCCL_POLICY_BPF_PATH=/home/yunwei37/workspace/nccl-eBPF/src/ebpf-policies/size_aware.bpf.o \
NCCL_TUNER_PLUGIN=/home/yunwei37/workspace/nccl-eBPF/src/nccl-policy-plugin/build/libnccl-policy.so \
LD_LIBRARY_PATH=/home/yunwei37/workspace/nccl-eBPF/nccl/build/lib \
./nccl-tests/build/all_reduce_perf -b 1M -e 1M -g 1 -n 5
```

Result:

- Command succeeded.
- NCCL loaded the tuner plugin successfully.
- The plugin’s init-time warmup executed the eBPF policy successfully.

Relevant NCCL debug lines:

```text
NCCL INFO NCCL_TUNER_PLUGIN set by environment to /home/yunwei37/workspace/nccl-eBPF/src/nccl-policy-plugin/build/libnccl-policy.so
NCCL INFO TUNER/Plugin: Using eBPFPolicy (v5)
NCCL INFO Successfully loaded external tuner plugin /home/yunwei37/workspace/nccl-eBPF/src/nccl-policy-plugin/build/libnccl-policy.so
```

Relevant plugin runtime lines:

```text
[nccl-policy-plugin] init warmup bytes=1024 decision=1 latency_ns=3435404
[nccl-policy-plugin] initialized for 1 ranks across 1 nodes using policy /home/yunwei37/workspace/nccl-eBPF/src/ebpf-policies/size_aware.bpf.o
[nccl-policy-plugin] finalize calls=0 avg_latency_ns=0 last_latency_ns=0 source=/home/yunwei37/workspace/nccl-eBPF/src/ebpf-policies/size_aware.bpf.o
```

Important caveat:

- On this exact `-g 1` run, NCCL did not call `getCollInfo()`.
- That is consistent with the plugin summary showing `calls=0`.
- The plugin still loaded and the eBPF VM still executed during `init()` because of the warmup.
- To validate real `getCollInfo()` traffic inside NCCL, the next step should be a multi-rank or multi-GPU run.

## Final Artifact Summary

- Standalone llvmbpf static library:
  - `/home/yunwei37/workspace/nccl-eBPF/libllvmbpf_vm.a`
- bpftime/vm llvmbpf static library:
  - `/home/yunwei37/workspace/nccl-eBPF/build-vm/compat/llvm-vm/libllvmbpf_vm.a`
- bpftime LLVM compat wrapper:
  - `/home/yunwei37/workspace/nccl-eBPF/build-vm/compat/llvm-vm/libbpftime_llvm_vm.a`
- bpftime VM C API wrapper:
  - `/home/yunwei37/workspace/nccl-eBPF/build-vm/vm-core/libbpftime_vm.a`
- NCCL tuner plugin:
  - `/home/yunwei37/workspace/nccl-eBPF/src/nccl-policy-plugin/build/libnccl-policy.so`
- CPU-only benchmark harness:
  - `/home/yunwei37/workspace/nccl-eBPF/src/nccl-policy-plugin/build/test_ebpf_plugin`

## Suggested Next Fixes / Phase 2

- Run NCCL with more than one rank so `getCollInfo()` is exercised by real collectives.
- Replace the current first-program-only `.bpf.o` loader with a section-name selector.
- Decide whether to keep init-time warmup or replace it with explicit JIT compilation in `init()`.
- Add a stable ABI for passing richer NCCL tuning context into the BPF program.
