# NCCL Tuner+Profiler Policy Plugin

Combined NCCL Tuner v5 and Profiler v6 plugin that executes eBPF policy programs on every collective operation. The plugin loads a `.bpf.o` file via [bpftime](https://github.com/eunomia-bpf/bpftime), verifies it statically, JIT-compiles it with LLVM, and invokes it in `getCollInfo` to determine algorithm, protocol, and channel count.

## How It Works

1. On `init`, the plugin initializes the bpftime runtime (LLVM JIT, shared-memory maps) and loads the eBPF object specified by `NCCL_POLICY_BPF_PATH`. If no path is set or loading fails, it falls back to a hardcoded noop program.

2. On each `getCollInfo` call, the plugin constructs a `nccl_policy_ctx` struct containing:
   - Message size (`n_bytes`)
   - Collective type (AllReduce, AllGather, Broadcast, etc.)
   - Rank/node count
   - Profiler-fed telemetry (last latency, average latency, rolling p99, call count)
   - Current channel count

3. The eBPF program returns a packed 64-bit action word encoding: algorithm (RING/TREE), protocol (LL/LL128/SIMPLE), channel count, and aggressiveness. The plugin unpacks this and applies the requested overrides to NCCL's cost table.

4. The Profiler v6 adapter captures collective start/stop events and writes latency measurements into shared eBPF maps, making telemetry available to the policy on subsequent calls.

5. Hot-reload: the plugin supports swapping the active eBPF program at runtime (via `ncclPolicyPluginDebugReloadPolicy`) without stopping NCCL.

## Build

Requires a pre-built bpftime at `../../build-bpftime/` (override with `-DBPFTIME_BUILD_DIR=<path>`), NCCL headers at `../../nccl/build/include`, and CUDA toolkit.

```bash
cmake -S . -B build
cmake --build build -j$(nproc)
```

Produces:
- `build/libnccl-policy.so` -- the plugin shared library
- `build/ebpf-policies/*.bpf.o` -- compiled eBPF policy objects
- `build/test_ebpf_plugin` -- integration test harness

## Environment Variables

| Variable | Description |
|---|---|
| `NCCL_TUNER_PLUGIN` | Set to the path of `libnccl-policy.so` to activate |
| `NCCL_POLICY_BPF_PATH` | Path to the `.bpf.o` policy file to load |
| `NCCL_POLICY_VERIFY_MODE` | `strict` (default): reject unsafe programs; `warning`: log and allow; `none`: skip verification |

## Exported Symbols

- `ncclTunerPlugin_v5` -- NCCL Tuner v5 interface (name: `eBPFPolicy`)
- `ncclProfiler_v6` -- NCCL Profiler v6 interface (name: `eBPFPolicyProfiler`)
- `ncclPolicyPluginDebugReloadPolicy` -- Hot-reload a new policy at runtime
- `ncclPolicyPluginDebugGetMapFd` -- Retrieve a bpftime map file descriptor by name
- `ncclPolicyPluginDebugSetSyntheticTelemetry` -- Inject synthetic telemetry for testing

## Files

- `plugin.cpp` -- Main plugin implementation (~1400 lines)
- `native_baseline.cpp` -- Native C++ baseline for overhead comparison
- `CMakeLists.txt` -- Build configuration; also compiles all eBPF policies from `../ebpf-policies/`
- `test/` -- Integration tests (verifier correctness, hot-reload, crash isolation)
