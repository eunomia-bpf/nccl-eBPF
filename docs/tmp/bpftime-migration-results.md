# bpftime Migration Results

## Summary

The NCCL tuner plugin was migrated from raw `llvmbpf_vm` usage to bpftime runtime primitives. The resulting plugin keeps the NCCL tuner v5 ABI, loads `.bpf.o` objects from `NCCL_POLICY_BPF_PATH`, creates bpftime maps, applies ELF map relocations, verifies programs at load time, and executes policies through `bpftime::bpftime_prog`.

Implemented policies:

- `src/ebpf-policies/size_aware_v2.bpf.c`
- `src/ebpf-policies/adaptive_channels.bpf.c`
- `src/ebpf-policies/slo_enforcer.bpf.c`

Also added:

- native baseline: `src/nccl-policy-plugin/native_baseline.cpp`
- bad verifier test program: `src/ebpf-policies/bad_lookup.bpf.c`
- benchmark harness updates in `src/nccl-policy-plugin/test/test_ebpf_plugin.c`

## Step 2: bpftime Build

Build command used:

```bash
mkdir -p build-bpftime
cd build-bpftime
cmake ../bpftime -DCMAKE_BUILD_TYPE=Release -DBPFTIME_ENABLE_UNIT_TESTING=OFF
make -j24
cmake ../bpftime -DCMAKE_BUILD_TYPE=Release -DBPFTIME_ENABLE_UNIT_TESTING=OFF -DENABLE_EBPF_VERIFIER=ON
make -j24 bpftime-verifier runtime
```

Relevant library paths used by the plugin build:

- `/home/yunwei37/workspace/nccl-eBPF/build-bpftime/runtime/libruntime.a`
- `/home/yunwei37/workspace/nccl-eBPF/build-bpftime/vm/vm-core/libbpftime_vm.a`
- `/home/yunwei37/workspace/nccl-eBPF/build-bpftime/vm/compat/llvm-vm/libbpftime_llvm_vm.a`
- `/home/yunwei37/workspace/nccl-eBPF/build-bpftime/vm/compat/llvm-vm/libllvmbpf_vm.a`
- `/home/yunwei37/workspace/nccl-eBPF/build-bpftime/bpftime-verifier/libbpftime-verifier.a`
- `/home/yunwei37/workspace/nccl-eBPF/build-bpftime/bpftime-verifier/ebpf-verifier/libebpfverifier.a`
- `/home/yunwei37/workspace/nccl-eBPF/build-bpftime/bpftime-verifier/ebpf-verifier/external/libbtf/libbtf/liblibbtf.a`
- `/home/yunwei37/workspace/nccl-eBPF/build-bpftime/third_party/spdlog/libspdlog.a`
- `/home/yunwei37/workspace/nccl-eBPF/build-bpftime/FridaGum-prefix/src/FridaGum/libfrida-gum.a`
- `/home/yunwei37/workspace/nccl-eBPF/build-bpftime/libbpf/libbpf.a`

Plugin / harness build directory:

- `/home/yunwei37/workspace/nccl-eBPF/src/nccl-policy-plugin/build-bpftime-migration-prebuilt`

Built outputs:

- `/home/yunwei37/workspace/nccl-eBPF/src/nccl-policy-plugin/build-bpftime-migration-prebuilt/libnccl-policy.so`
- `/home/yunwei37/workspace/nccl-eBPF/src/nccl-policy-plugin/build-bpftime-migration-prebuilt/test_ebpf_plugin`

## Step 3: Plugin Migration Notes

What changed in `plugin.cpp`:

- replaced raw `llvmbpf_vm` setup with `bpftime::bpftime_prog`
- create bpftime maps via `bpftime_maps_create`
- parse BPF ELF with libbpf
- relocate `.reluprobe` `R_BPF_64_64` map references to `BPF_PSEUDO_MAP_FD`
- register helper groups:
  - kernel utils
  - shared-memory map helpers
- call bpftime verifier at load time
- seed `config_map` for SLO policy from env vars
- decode packed 64-bit policy actions into NCCL algo / proto / channels / aggressiveness decisions

Practical runtime workaround added:

- When `BPFTIME_GLOBAL_SHM_NAME` is not set, the plugin now assigns a per-process shared-memory name (`nccl_policy_bpftime_<uid>_<pid>`). This avoids collisions with an existing root-owned `/dev/shm/bpftime_maps_shm` on this host.

## Step 4: Policy Status

### `size_aware_v2.bpf.c`

- strict verifier load: works
- runtime execution: works
- uses context fields only, no maps

### `adaptive_channels.bpf.c`

- strict verifier load: rejected by bpftime userspace verifier
- warning verifier load: works
- runtime execution with `telemetry_map`: works

### `slo_enforcer.bpf.c`

- strict verifier load: rejected by bpftime userspace verifier
- warning verifier load: works
- runtime execution with `config_map` + `telemetry_map`: works

Inference:

- The map-backed policies appear to hit a current bpftime verifier limitation for these `uprobe` programs with custom context access patterns, rather than a relocation/runtime failure.
- Reason for that inference: both objects load and execute correctly in `warning` mode, maps are created, map updates persist, and policy outputs change over time as state evolves.

## Step 5: Native Baseline

Native baseline file:

- `/home/yunwei37/workspace/nccl-eBPF/src/nccl-policy-plugin/native_baseline.cpp`

This implements the same decision logic as `size_aware_v2` without eBPF.

## Step 6: Build Result

Plugin build command:

```bash
cmake -S . -B build-bpftime-migration-prebuilt -DCMAKE_BUILD_TYPE=Release
cmake --build build-bpftime-migration-prebuilt -j24
```

Result:

- build succeeded
- plugin shared object built successfully
- harness built successfully

Notable warning:

- The final link reports ODR warnings from mixing bpftime’s vendored libbpf Linux headers with the system `/usr/include/linux/bpf.h`.
- This did not block the build or runtime tests on this machine.

## Step 7: Benchmarks

Benchmark command:

```bash
cd /home/yunwei37/workspace/nccl-eBPF/src/nccl-policy-plugin/build-bpftime-migration-prebuilt
./test_ebpf_plugin ./libnccl-policy.so
```

Iterations per policy:

- 1,000,000 `getCollInfo()` calls

Results:

| Policy | Verify Mode | P50 ns | P99 ns | Max ns | Delta P50 vs native | Delta P99 vs native |
|---|---:|---:|---:|---:|---:|---:|
| native baseline | n/a | 14 | 21 | 60089 | 0 | 0 |
| noop | strict | 45 | 53 | 5906 | +31 | +32 |
| size_aware | strict | 45 | 54 | 3986 | +31 | +33 |
| size_aware_v2 | strict | 45 | 54 | 8110 | +31 | +33 |
| adaptive_channels | warning | 67 | 77 | 10885 | +53 | +56 |
| slo_enforcer | warning | 80 | 91 | 5083 | +66 | +70 |

Observed behavior:

- `size_aware` and `size_aware_v2` stayed close to the no-op policy overhead.
- `adaptive_channels` adds measurable cost from hash-map lookup/update and telemetry maintenance.
- `slo_enforcer` is the heaviest of the tested policies due to multiple map touches and more control logic.

## Verifier Test

Bad-program verifier test:

- object: `/home/yunwei37/workspace/nccl-eBPF/src/nccl-policy-plugin/build-bpftime-migration-prebuilt/ebpf-policies/bad_lookup.bpf.o`
- mode: `strict`
- result: rejected as expected

Harness output:

```text
verifier rejection: PASS (/home/yunwei37/workspace/nccl-eBPF/src/nccl-policy-plugin/build-bpftime-migration-prebuilt/ebpf-policies/bad_lookup.bpf.o)
```

## Step 8: Real NCCL Test

### Attempt 1: existing `nccl-tests` build

Command:

```bash
mpirun --oversubscribe -np 2 \
  -x LD_LIBRARY_PATH=/home/yunwei37/workspace/nccl-eBPF/nccl/build/lib \
  -x NCCL_DEBUG=INFO \
  -x NCCL_TUNER_PLUGIN=/home/yunwei37/workspace/nccl-eBPF/src/nccl-policy-plugin/build-bpftime-migration-prebuilt/libnccl-policy.so \
  -x NCCL_POLICY_BPF_PATH=/home/yunwei37/workspace/nccl-eBPF/src/nccl-policy-plugin/build-bpftime-migration-prebuilt/ebpf-policies/size_aware_v2.bpf.o \
  -x NCCL_POLICY_VERIFY_MODE=strict \
  /home/yunwei37/workspace/nccl-eBPF/nccl-tests/build/all_reduce_perf -b 1M -e 1M -f 2 -g 1 -n 5
```

Result:

- plugin library loaded in each process
- but this `nccl-tests` binary was built with `MPI=0`
- both processes initialized as `nranks=1`
- `getCollInfo()` was not called

Evidence:

- `nccl-tests/src/Makefile` defaults to `MPI ?= 0`
- runtime log showed `rank 0 nranks 1`
- plugin `finalize` reported `calls=0`

### Attempt 2: rebuilt MPI-enabled `nccl-tests`

MPI-enabled build command:

```bash
cd /home/yunwei37/workspace/nccl-eBPF/nccl-tests
make -C src MPI=1 NAME_SUFFIX=_mpi BUILDDIR=../build-mpi \
  MPI_HOME=/usr/lib/x86_64-linux-gnu/openmpi \
  NCCL_HOME=/home/yunwei37/workspace/nccl-eBPF/nccl/build -j24
```

MPI-enabled binary:

- `/home/yunwei37/workspace/nccl-eBPF/nccl-tests/build-mpi/all_reduce_perf_mpi`

2-rank command:

```bash
mpirun --oversubscribe -np 2 \
  -x LD_LIBRARY_PATH=/home/yunwei37/workspace/nccl-eBPF/nccl/build/lib \
  -x NCCL_DEBUG=INFO \
  -x NCCL_TESTS_DEVICE=0 \
  -x NCCL_TUNER_PLUGIN=/home/yunwei37/workspace/nccl-eBPF/src/nccl-policy-plugin/build-bpftime-migration-prebuilt/libnccl-policy.so \
  -x NCCL_POLICY_BPF_PATH=/home/yunwei37/workspace/nccl-eBPF/src/nccl-policy-plugin/build-bpftime-migration-prebuilt/ebpf-policies/size_aware_v2.bpf.o \
  -x NCCL_POLICY_VERIFY_MODE=strict \
  /home/yunwei37/workspace/nccl-eBPF/nccl-tests/build-mpi/all_reduce_perf_mpi -b 1M -e 1M -f 2 -g 1 -n 5
```

Result:

- communicator reached real multi-rank init:
  - rank 0 / nranks 2
  - rank 1 / nranks 2
- run then failed before collective selection because this machine has only one visible GPU and both ranks had to bind to device 0
- NCCL aborted with:
  - `Duplicate GPU detected : rank 1 and rank 0 both on CUDA device 2000`

Host GPU inventory:

```text
GPU 0: NVIDIA GeForce RTX 5090
```

### Final Step 8 status

Could not confirm real `getCollInfo()` traffic inside a successful 2-rank NCCL collective on this host.

What was confirmed:

- NCCL can load `libnccl-policy.so` in real runs
- a real `nranks=2` communicator is reachable with the MPI-enabled test binary

What blocked the final confirmation:

- only one visible GPU is available on this machine
- NCCL aborts the true 2-rank communicator before collective execution due duplicate-GPU detection

## Final Status

Completed:

- bpftime API research
- bpftime runtime build
- plugin migration to bpftime runtime primitives
- three policy implementations
- native C baseline
- build-system update
- microbenchmark + verifier rejection test

Partially completed:

- real NCCL 2-rank confirmation of `getCollInfo()`

Reason for the remaining gap:

- host has one visible GPU, so NCCL aborts a true 2-rank communicator before `getCollInfo()` can be observed.
