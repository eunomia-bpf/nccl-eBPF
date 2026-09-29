# NCCLbpf -- eBPF-based Policy Execution for NCCL

NCCLbpf brings verified eBPF policy execution to [NCCL](https://github.com/NVIDIA/nccl) (NVIDIA Collective Communication Library). It uses [bpftime](https://github.com/eunomia-bpf/bpftime), a userspace eBPF runtime, to load and execute eBPF programs inside NCCL's plugin system. Tuner policies can govern algorithm, protocol, and channel selection; the current net plugin only traces transport events. Tuner policy bytecode receives a load-time check for rank-local dependencies in collective-wide actions.

This is a research prototype targeting the eBPF Workshop at SOSP 2026.

The paper artifact entry point, including frozen submission files, build
checks, benchmark commands, and an evidence index, is
[`docs/artifact/README.md`](docs/artifact/README.md).

## Architecture

NCCLbpf consists of two NCCL plugins and a library of eBPF policy programs:

```
+------------------+     +---------------------------+
|  NCCL Runtime    |     |  eBPF Policy Programs     |
|                  |     |  (noop, size_aware,        |
|  Tuner v5 hook --+---->|   agreed-map example)      |
|  Profiler v6 hook+---->|                           |
|  Net v11 hook ---+---->+---------------------------+
|                  |              |
+------------------+     bpftime (LLVM JIT + verifier)
```

**Tuner+Profiler Plugin** (`src/nccl-policy-plugin/`) -- Implements NCCL's Tuner v5 and Profiler v6 interfaces in a single shared library. On each `getCollInfo` call, it executes an eBPF policy program that receives a context (message size, collective type, rank count, profiler-fed telemetry) and returns a packed action (algorithm, protocol, channel count). The profiler adapter can execute a separate eBPF program that writes runtime latency data into the tuner program's communicator-scoped telemetry map, closing the telemetry loop.

**Net Plugin** (`src/nccl-net-ebpf-plugin/`) -- Prototype wrapping NCCL's built-in Socket transport (Net v11 interface). Executes eBPF hooks on init, listen, connect, accept, isend, irecv, and finalize events. The current program records transport statistics; it does not enforce transport decisions.

**eBPF Policies** (`src/ebpf-policies/`) -- Verified eBPF programs compiled with `clang -target bpf`. Includes:
- `noop.bpf.c` -- Passthrough (no override), used for overhead measurement
- `size_aware.bpf.c` / `size_aware_v2-v5` -- Size-based algorithm/protocol selection
- `adaptive_channels.bpf.c` / `slo_enforcer.bpf.c` -- Historical local-telemetry examples; the cross-rank checker now rejects their tuner actions because local measurements can diverge across ranks
- `ring_simple_all.bpf.c` -- Forces RING/SIMPLE for all sizes
- `nvl72_size_aware.bpf.c` -- Single-node, one-NVL-domain AllReduce overrides
  for exact 4-rank and 8-rank communicators; it deliberately leaves all
  multi-node communicators unchanged
- `bad_*.bpf.c` -- Intentionally unsafe programs for verifier testing (div-by-zero, OOB access, stack overflow, infinite loop, etc.)
- `distributed_max_latency.bpf.c` -- Experimental agreed-map example: a verified BPF merge takes the maximum of rank-local latency slots, and the tuner selects channels from the shared result
- `spark_tp4_ll_64k.bpf.c`, `spark_tp4_simple_64k.bpf.c`, and
  `spark_tp4_ll_1m.bpf.c` -- Four-node, four-rank AllReduce protocol
  comparison: Ring/LL or Ring/SIMPLE through 64 KiB, and Ring/LL through
  1 MiB. They leave other collectives, topologies, and channel counts to
  NCCL; no Spark inference speedup has been established by a hardware A/B test.

### Cross-rank agreement prototype

The tuner loader now checks both data and control dependencies of the returned
action. It rejects a policy that lets ordinary local maps, timers, local
telemetry, or rank-local context fields choose a collective-wide action. A
second check verifies that a `dist_merge` BPF program reads only exchanged
rank slots and writes the agreed result. The profiler may still record local
measurements. See [the design and NCCL source analysis](docs/cross-rank-agreement.md).

`distributed_max_latency` demonstrates the experimental agreed-map subset.
Set `NCCL_POLICY_EXPERIMENTAL_DIST_MAP=1`, load its object with
`NCCL_POLICY_BPF_PATH`, and load the same library as both tuner and profiler
(`NCCL_TUNER_PLUGIN` and `NCCL_PROFILER_PLUGIN`). The profiler supplies the
rank ID, which tuner v5 does not expose; a missing or conflicting rank is
rejected. The example supports 1-8 ranks, one rank per process. With
`NCCL_POLICY_DIST_COORDINATOR` unset, ranks on one host exchange over a Unix
socket and must share `/tmp`. For separate hosts, every rank must set it to
the same `rank-0-host-or-IPv4:port`; rank 0 binds only that named local
interface and peers connect to it. An unspecified endpoint on a multi-node
communicator is rejected.

Before its first policy action, each participating distributed-policy rank
exchanges the hash of the BPF object and plugin runtime in round 0. A mismatch
or transport failure returns an error. Thereafter each rank writes its own
`local_latency` value; an asynchronous TCP or Unix-socket exchange starts
every 1024 tuner calls. At the following 1024-call boundary, each rank runs
the verified `SEC("dist_merge")` program on the same rank-indexed slots and
checks the maximum before publishing `agreed_map`. The initial handshake and
activation can wait up to the exchange deadline (30 seconds by default).
Distributed-policy hot reload is disabled.

This remains an experimental path. NCCL tuner call counts can differ on
special paths, host processing can depend on rank-local state, and a transport
failure partway through COMMIT can leave some ranks with a committed snapshot
while others fail. The current CPU tests do not establish safety for arbitrary
NCCL jobs. The original paper's single-node B300 results predate this feature.

The hardware-free tests run under `make test`: local two- and eight-process
Unix-socket exchanges, a four-process TCP loopback plugin run, version
mismatch and missing-coordinator rejection, and focused TCP transport cases.
There is no measured multi-host Spark, GPU, or LLM result for this path yet.

### NCCL source compatibility

The tuner and profiler implementation uses NCCL's existing plugin ABIs. It
does not require changes to NCCL source or a custom NCCL build. The net plugin
is a narrower prototype: it forwards to NCCL's **internal** Socket backend by
looking up `ncclNetSocket` at runtime. With the pinned NCCL source and its
default hidden-symbol build, that symbol is not exported, so this particular
Socket wrapper cannot be run against an unmodified build. The recorded net
plugin experiment used a one-line NCCL visibility change; see
[`docs/tmp/net-plugin-experiment.md`](docs/tmp/net-plugin-experiment.md).
Exporting the symbol is not required for the tuner/profiler, nor is it a
change to the eBPF policy programs. A net plugin that implements its own
transport or wraps a separately exported backend could avoid changing NCCL,
but that is not the implementation measured here and has not been validated
by this artifact.

## Prerequisites

- NVIDIA GPU with CUDA toolkit
- clang/LLVM (for BPF compilation, tested with LLVM 15+)
- cmake (>= 3.20), pkg-config
- libelf-dev, zlib1g-dev, libzstd-dev, libboost-dev
- Pre-built bpftime (at `build-bpftime/`; see the [bpftime repository](https://github.com/eunomia-bpf/bpftime) for build instructions)
- MPI implementation (for running nccl-tests)
- [NVIDIA nccl-tests](https://github.com/NVIDIA/nccl-tests), installed and
  built separately; it is not included or pinned as a submodule here. The
  paper's GPU measurements used nccl-tests 2.18.0.

## Build

On Ubuntu, `make install` installs the host build dependencies used by the
Makefile, including CMake and the distribution LLVM packages. CUDA, NCCL,
bpftime, MPI, and nccl-tests remain separate prerequisites.
The example benchmark paths below assume a separately built `nccl-tests/`
checkout next to `nccl/`; see the upstream build instructions for CUDA,
`NCCL_HOME`, and optional `MPI=1` settings. A missing `nccl-tests/` directory
is an unmet external prerequisite, not a missing paper source file.

### 1. Initialize submodules

```bash
git submodule update --init --recursive
```

### 2. Build NCCL

```bash
make -C nccl -j$(nproc) src.build BUILDDIR=$(pwd)/nccl/build
```

### Prepare nccl-tests for GPU benchmarks

NVIDIA maintains `nccl-tests` separately from NCCL. To obtain the version
used for the paper and produce the single-process binaries referenced below:

```bash
git clone --branch v2.18.0 --depth 1 https://github.com/NVIDIA/nccl-tests.git nccl-tests
make -C nccl-tests -j$(nproc) NCCL_HOME="$PWD/nccl/build"
```

MPI examples additionally need MPI-enabled binaries. Build those from the
same checkout with `MPI=1 NAME_SUFFIX=_mpi` and an `MPI_HOME` appropriate for
your installation, as described in the
[upstream build instructions](https://github.com/NVIDIA/nccl-tests#build).
The `nccl-tests/` checkout is a separate local dependency, not part of this
repository; avoid adding it to a paper-artifact commit.

### 3. Build bpftime

Follow the instructions in the [bpftime repository](https://github.com/eunomia-bpf/bpftime). The build output is expected at `build-bpftime/`. You can override this with `-DBPFTIME_BUILD_DIR=<path>` when running cmake.

### 4. Build the tuner+profiler plugin

Once NCCL and bpftime are available, `make build` configures and builds both
plugins using the build directories shown below. The equivalent individual
commands are:

```bash
cmake -S src/nccl-policy-plugin -B src/nccl-policy-plugin/build
cmake --build src/nccl-policy-plugin/build -j$(nproc)
```

This produces `src/nccl-policy-plugin/build/libnccl-policy.so` and compiles all eBPF policy objects into `src/nccl-policy-plugin/build/ebpf-policies/`.

### 5. Build the net plugin

Building the library alone does not make its Socket backend available. Check
whether your NCCL library exports `ncclNetSocket` before attempting the net
plugin example below. The pinned NCCL source does not export it in a default
build; the experiment documented a visibility change. The tuner/profiler build
and GPU policy benchmarks do not need this symbol.

```bash
cmake -S src/nccl-net-ebpf-plugin -B src/nccl-net-ebpf-plugin/build
cmake --build src/nccl-net-ebpf-plugin/build -j$(nproc)
```

This produces `src/nccl-net-ebpf-plugin/build/libnccl-net-ebpf.so` and compiles `net_trace.bpf.o`.

Run the hardware-free CTest and benchmark-runner regressions with:

```bash
make test
```

### 6. Check benchmark arm configuration

`scripts/nccl_bench.sh` provides generic baseline, policy, and environment-forced
nccl-tests arms without assuming a cluster topology or transport configuration.
Use `selftest` to inspect the generated command before running it:

```bash
NPROC=8 ARM=policy:nvl72_size_aware \
  scripts/nccl_bench.sh selftest
```

`nvl72_size_aware` is intentionally limited to a single-node communicator in
one NVL domain. With the pinned NCCL tuner v5 implementation,
`nNvlDomains` tracks `nNodes`; the ABI has no separate MNNVL capability signal.
The policy therefore returns no override whenever `nNodes > 1`, including for
multi-host jobs. Other runner arms may still use `HOSTLIST` supplied by the
caller.

Policy and noop arms preflight the plugin and BPF objects. Completed output is
given a policy-labelled filename only after the log contains a successful
tuner-policy initialization marker for every Open MPI rank. The runner enables
nccl-tests data checking (`CHECK=1`) by default; set `CHECK=0` explicitly to
disable it. Run `scripts/test_nccl_bench.sh` for the hardware-free arm and
load-marker regression tests. For a short live policy trial,
`NCCL_POLICY_BENCHMARK_READY=1` also prints compact per-communicator
AllReduce/AllGather/ReduceScatter size and action-request summaries; see
[the plugin README](src/nccl-policy-plugin/README.md). CUDA Graph replay
does not add tuner calls to these summaries.

## Usage

### Run with the tuner+profiler plugin

```bash
LD_LIBRARY_PATH=nccl/build/lib \
NCCL_TUNER_PLUGIN=src/nccl-policy-plugin/build/libnccl-policy.so \
NCCL_POLICY_BPF_PATH=src/nccl-policy-plugin/build/ebpf-policies/size_aware_v5.bpf.o \
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
| `NCCL_PROFILER_PLUGIN` | Set to the same library for the agreed-map example so profiler v6 supplies the rank ID |
| `NCCL_POLICY_BPF_PATH` | Path to the eBPF policy `.bpf.o` file to load |
| `NCCL_POLICY_VERIFY_MODE` | Verifier behavior: `strict` (default, reject unsafe), `warning` (log but allow), `none` (skip verification) |
| `NCCL_POLICY_PROFILER_MODE` | Telemetry writer: `native` (default) or `ebpf` |
| `NCCL_POLICY_PROFILER_BPF_PATH` | Path to `profiler_latency.bpf.o`; required when profiler mode is `ebpf` |
| `NCCL_POLICY_EXPERIMENTAL_DIST_MAP` | Set to `1` to load the experimental agreed-map example |
| `NCCL_POLICY_DIST_COORDINATOR` | For multi-node agreed-map jobs, set the same rank-0 host or IPv4 address and TCP port on every rank (for example `rank0.example:47000`); rank 0 must own that interface. Unset selects same-host Unix sockets |

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
