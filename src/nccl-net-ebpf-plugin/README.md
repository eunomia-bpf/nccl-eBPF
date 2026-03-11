# NCCL Net eBPF Plugin

NCCL Net v11 plugin that wraps the built-in Socket transport and executes eBPF hooks on transport-layer events. Uses [bpftime](https://github.com/eunomia-bpf/bpftime) for verified eBPF execution with LLVM JIT compilation.

## How It Works

The plugin resolves NCCL's internal `ncclNetSocket` symbol at runtime and delegates all transport operations to it. Before or after each operation, it invokes an eBPF program with a `nccl_net_ebpf_ctx` struct containing the hook type, device index, message size, tag, communicator ID, and timestamp.

Hooks are fired on: `init`, `listen`, `connect`, `accept`, `isend`, `irecv`, and `finalize`.

The default eBPF program (`net_trace.bpf.c`) maintains per-hook statistics (call count, total bytes, last tag) in a shared BPF array map. Custom programs can implement transport-layer policies such as rate limiting, anomaly detection, or selective logging.

## Build

Requires a pre-built bpftime at `../../build-bpftime/` (override with `-DBPFTIME_BUILD_DIR=<path>`), NCCL headers at `../../nccl/build/include`, and CUDA toolkit.

```bash
cmake -S . -B build
cmake --build build -j$(nproc)
```

Produces:
- `build/libnccl-net-ebpf.so` -- the plugin shared library
- `build/net_trace.bpf.o` -- default eBPF tracing program

## Environment Variables

| Variable | Description |
|---|---|
| `NCCL_NET_PLUGIN` | Set to the path of `libnccl-net-ebpf.so` to activate |
| `NCCL_NET_EBPF_BPF_PATH` | Path to the `.bpf.o` program to load (defaults to the built-in `net_trace.bpf.o`) |
| `NCCL_NET_EBPF_VERIFY_MODE` | `strict` (default): reject unsafe programs; `warning`/`warn`: log and allow; `none`: skip verification |

## Example

```bash
LD_LIBRARY_PATH=../../nccl/build/lib \
NCCL_NET_PLUGIN=build/libnccl-net-ebpf.so \
NCCL_NET_EBPF_BPF_PATH=build/net_trace.bpf.o \
NCCL_NET=Socket NCCL_P2P_DISABLE=1 NCCL_SHM_DISABLE=1 \
mpirun -np 2 nccl-tests/build/all_reduce_perf_mpi -b 128M -e 128M -g 1
```

## Files

- `plugin.cpp` -- Plugin implementation; wraps Socket transport with eBPF hook execution
- `net_ebpf_ctx.h` -- Context struct and hook type definitions shared between plugin and eBPF programs
- `net_trace.bpf.c` -- Default eBPF program that collects per-hook statistics
- `plugin_paths.h.in` -- CMake template for default BPF object path
- `CMakeLists.txt` -- Build configuration
