# NCCLbpf -- eBPF-based Policy Execution for NCCL

NCCLbpf brings verified eBPF policy execution to [NCCL](https://github.com/NVIDIA/nccl) (NVIDIA Collective Communication Library). It uses [bpftime](https://github.com/eunomia-bpf/bpftime), a userspace eBPF runtime, to load and execute eBPF programs inside NCCL's plugin system. Policies run on every collective operation to govern algorithm/protocol selection, channel allocation, and transport-layer behavior -- all with static verification and process-level isolation.

This is a research prototype targeting the eBPF Workshop at SOSP 2026.

## Architecture

NCCLbpf consists of two NCCL plugins and a library of eBPF policy programs:

```
+------------------+     +---------------------------+
|  NCCL Runtime    |     |  eBPF Policy Programs     |
|                  |     |  (noop, size_aware,        |
|  Tuner v5 hook --+---->|   slo_enforcer, ...)      |
|  Profiler v6 hook+---->|                           |
|  Net v11 hook ---+---->+---------------------------+
|                  |              |
+------------------+     bpftime (LLVM JIT + verifier)
```

**Tuner+Profiler Plugin** (`src/nccl-policy-plugin/`) -- Implements NCCL's Tuner v5 and Profiler v6 interfaces in a single shared library. On each `getCollInfo` call, it executes an eBPF policy program that receives a context (message size, collective type, rank count, profiler-fed telemetry) and returns a packed action (algorithm, protocol, channel count). The profiler adapter writes runtime latency data into shared eBPF maps, closing the telemetry loop.

**Net Plugin** (`src/nccl-net-ebpf-plugin/`) -- Wraps NCCL's built-in Socket transport (Net v11 interface). Executes eBPF hooks on init, listen, connect, accept, isend, irecv, and finalize events. Designed for transport-layer observability and policy enforcement.

**eBPF Policies** (`src/ebpf-policies/`) -- Verified eBPF programs compiled with `clang -target bpf`. Includes:
- `noop.bpf.c` -- Passthrough (no override), used for overhead measurement
- `size_aware.bpf.c` / `size_aware_v2-v5` -- Size-based algorithm/protocol selection
- `adaptive_channels.bpf.c` -- Dynamic channel count adjustment using telemetry maps
- `slo_enforcer.bpf.c` -- SLO-driven policy using config maps and telemetry feedback
- `ring_simple_all.bpf.c` -- Forces RING/SIMPLE for all sizes
- `bad_*.bpf.c` -- Intentionally unsafe programs for verifier testing (div-by-zero, OOB access, stack overflow, infinite loop, etc.)

## Prerequisites

- NVIDIA GPU with CUDA toolkit
- clang/LLVM (for BPF compilation, tested with LLVM 15+)
- cmake (>= 3.20), pkg-config
- libelf-dev, zlib1g-dev
- Pre-built bpftime (at `build-bpftime/`; see the [bpftime repository](https://github.com/eunomia-bpf/bpftime) for build instructions)
- MPI implementation (for running nccl-tests)

## Build

### 1. Initialize submodules

```bash
git submodule update --init --recursive
```

### 2. Build NCCL

```bash
make -C nccl -j$(nproc) src.build BUILDDIR=$(pwd)/nccl/build
```

### 3. Build bpftime

Follow the instructions in the [bpftime repository](https://github.com/eunomia-bpf/bpftime). The build output is expected at `build-bpftime/`. You can override this with `-DBPFTIME_BUILD_DIR=<path>` when running cmake.

### 4. Build the tuner+profiler plugin

```bash
cmake -S src/nccl-policy-plugin -B src/nccl-policy-plugin/build
cmake --build src/nccl-policy-plugin/build -j$(nproc)
```

This produces `src/nccl-policy-plugin/build/libnccl-policy.so` and compiles all eBPF policy objects into `src/nccl-policy-plugin/build/ebpf-policies/`.

### 5. Build the net plugin

```bash
cmake -S src/nccl-net-ebpf-plugin -B src/nccl-net-ebpf-plugin/build
cmake --build src/nccl-net-ebpf-plugin/build -j$(nproc)
```

This produces `src/nccl-net-ebpf-plugin/build/libnccl-net-ebpf.so` and compiles `net_trace.bpf.o`.

## Usage

### Run with the tuner+profiler plugin

```bash
LD_LIBRARY_PATH=nccl/build/lib \
NCCL_TUNER_PLUGIN=src/nccl-policy-plugin/build/libnccl-policy.so \
NCCL_POLICY_BPF_PATH=src/ebpf-policies/size_aware_v5.bpf.o \
mpirun -np 2 nccl-tests/build/all_reduce_perf_mpi -b 1M -e 128M -g 1
```

### Run with the net plugin

```bash
LD_LIBRARY_PATH=nccl/build/lib \
NCCL_NET_PLUGIN=src/nccl-net-ebpf-plugin/build/libnccl-net-ebpf.so \
NCCL_NET_EBPF_BPF_PATH=src/nccl-net-ebpf-plugin/build/net_trace.bpf.o \
NCCL_NET=Socket NCCL_P2P_DISABLE=1 NCCL_SHM_DISABLE=1 \
mpirun -np 2 nccl-tests/build/all_reduce_perf_mpi -b 128M -e 128M -g 1
```

## Environment Variables

### Tuner+Profiler Plugin

| Variable | Description |
|---|---|
| `NCCL_TUNER_PLUGIN` | Path to `libnccl-policy.so` |
| `NCCL_POLICY_BPF_PATH` | Path to the eBPF policy `.bpf.o` file to load |
| `NCCL_POLICY_VERIFY_MODE` | Verifier behavior: `strict` (default, reject unsafe), `warning` (log but allow), `none` (skip verification) |

### Net Plugin

| Variable | Description |
|---|---|
| `NCCL_NET_PLUGIN` | Path to `libnccl-net-ebpf.so` |
| `NCCL_NET_EBPF_BPF_PATH` | Path to the net eBPF program `.bpf.o` file |
| `NCCL_NET_EBPF_VERIFY_MODE` | Verifier behavior: `strict` (default), `warning`/`warn`, `none` |

## Directory Structure

```
src/
  nccl-policy-plugin/    Tuner v5 + Profiler v6 plugin (C++)
  nccl-net-ebpf-plugin/  Net v11 plugin wrapping Socket transport (C++)
  ebpf-policies/         eBPF policy programs and shared headers
docs/
  paper/                 Workshop paper (LaTeX)
bpftime/                 Submodule: userspace eBPF runtime (bpftime)
nccl/                    Submodule: NVIDIA NCCL 2.29.7
build-bpftime/           Pre-built bpftime libraries (not checked in)
```

## Key Dependencies

- [bpftime](https://github.com/eunomia-bpf/bpftime) -- Userspace eBPF runtime with LLVM JIT compilation, static verification, and shared-memory map support. Statically linked into both plugins.
- [NCCL](https://github.com/NVIDIA/nccl) -- NVIDIA Collective Communication Library. Plugins use the Tuner v5, Profiler v6, and Net v11 plugin interfaces.

## License

MIT
