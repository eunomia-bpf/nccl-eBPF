# NCCL Net eBPF Plugin

NCCL Net v11 plugin that wraps the built-in Socket transport and executes eBPF hooks on transport-layer events. Uses [bpftime](https://github.com/eunomia-bpf/bpftime) for verified eBPF execution with LLVM JIT compilation.

## How It Works

The plugin resolves NCCL's internal `ncclNetSocket` symbol at runtime and delegates all transport operations to it. Before or after each operation, it invokes an eBPF program with a `nccl_net_ebpf_ctx` struct containing the hook type, device index, message size, tag, communicator ID, and timestamp.

### Compatibility limit

The Net v11 plugin interface itself does not require an NCCL source change,
but this implementation depends on the *internal* `ncclNetSocket` object being
exported from `libnccl.so`. The pinned NCCL source is built with hidden symbols
and does not mark that object for export. The recorded Socket-wrapper
experiment used a one-line visibility change to NCCL and a rebuild (see
[`docs/tmp/net-plugin-experiment.md`](../../docs/tmp/net-plugin-experiment.md)).
Without an exported symbol, all three runtime lookup strategies in
`plugin.cpp` fail and the wrapper cannot forward to the built-in backend.
Before trying this plugin, check the exact NCCL library you will load:

```sh
nm -D nccl/build/lib/libnccl.so | grep -w ncclNetSocket
```

An empty result means this wrapper is not usable with that library. A
different Net v11 implementation could use its own transport or a separately
exported backend without changing NCCL, but that alternative is not included
or benchmarked here. This constraint does not apply to the tuner/profiler
plugin.

Hooks are fired on: `init`, `listen`, `connect`, `accept`, `isend`, `irecv`, and `finalize`.

The default eBPF program (`net_trace.bpf.c`) maintains per-hook statistics (call count, total bytes, last tag) in a shared BPF array map. The current wrapper executes hooks for observation; it does not consume a BPF return value to alter or reject transport operations. Rate limiting or admission policy would require additional implementation and validation.

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
