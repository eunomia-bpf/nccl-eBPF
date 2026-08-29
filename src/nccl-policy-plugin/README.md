# NCCL Tuner+Profiler Policy Plugin

Combined NCCL Tuner v5 and Profiler v6 plugin that executes eBPF policy programs on every collective operation. The plugin loads a `.bpf.o` file via [bpftime](https://github.com/eunomia-bpf/bpftime), verifies it statically, JIT-compiles it with LLVM, and invokes it in `getCollInfo` to determine algorithm, protocol, and channel count.

## How It Works

1. On `init`, the plugin initializes the bpftime runtime (LLVM JIT, shared-memory maps) and loads the eBPF object specified by `NCCL_POLICY_BPF_PATH`. With no path set it uses a hardcoded noop program. If an explicitly requested object cannot be loaded, initialization fails.

2. On each `getCollInfo` call, the plugin constructs a `nccl_policy_ctx` struct containing:
   - Message size (`n_bytes`)
   - Collective type (AllReduce, AllGather, Broadcast, etc.)
   - Rank/node count
   - Validated NCCL tuner v5 NVL-domain counts when available
   - Profiler-fed telemetry (last latency, average latency, rolling p99, call count)
   - Current channel count

3. The eBPF program returns a packed 64-bit action word encoding: algorithm (RING/TREE), protocol (LL/LL128/SIMPLE), channel count, and aggressiveness. The plugin unpacks this and applies the requested overrides to NCCL's cost table.

4. The Profiler v6 adapter captures collective start/stop events. In `ebpf` mode it passes the measurement to a separately verified and JIT-compiled `SEC("profiler")` program; in `native` mode it uses the original C++ writer. Both update the same communicator-scoped `telemetry_map`, making telemetry available to the tuner policy on subsequent calls.

5. Hot-reload: the plugin supports swapping the active tuner eBPF program at runtime (via `ncclPolicyPluginDebugReloadPolicy`) without stopping NCCL. The shared telemetry map is owned by the communicator, so its state survives tuner reloads.

The `nvl72_size_aware` policy supports only a single-node communicator in one
NVL domain, with exact 4-rank or 8-rank inputs. The pinned NCCL tuner v5 ABI
does not provide a distinct MNNVL fabric signal, and its domain count follows
the node count, so this policy deliberately returns no override for every
multi-node communicator.

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
| `NCCL_POLICY_PROFILER_MODE` | `native` (default) or `ebpf` telemetry updates |
| `NCCL_POLICY_PROFILER_BPF_PATH` | Path to `profiler_latency.bpf.o`; required in `ebpf` mode |

## Exported Symbols

- `ncclTunerPlugin_v5` -- NCCL Tuner v5 interface (name: `eBPFPolicy`)
- `ncclProfiler_v6` -- NCCL Profiler v6 interface (name: `eBPFPolicyProfiler`)
- `ncclPolicyPluginDebugReloadPolicy` -- Hot-reload a new policy at runtime
- `ncclPolicyPluginDebugGetMapFd` -- Retrieve a bpftime map file descriptor by name
- `ncclPolicyPluginDebugSetSyntheticTelemetry` -- Inject synthetic telemetry for testing
- `ncclPolicyPluginDebugExecutePolicy` -- Execute a raw policy context for ABI regression testing

## Files

- `plugin.cpp` -- Main plugin implementation (~1400 lines)
- `native_baseline.cpp` -- Native C++ baseline for overhead comparison
- `CMakeLists.txt` -- Build configuration; also compiles all eBPF policies from `../ebpf-policies/`
- `test/` -- Integration tests (verifier correctness, hot-reload, crash isolation)
