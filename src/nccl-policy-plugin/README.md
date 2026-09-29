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

5. Hot-reload: ordinary tuner policies can be swapped through `ncclPolicyPluginDebugReloadPolicy` while their shared telemetry map survives. Reload of an experimental distributed-map policy is rejected because a coordinated cross-rank version switch is not implemented.

The `nvl72_size_aware` policy supports only a single-node communicator in one
NVL domain, with exact 4-rank or 8-rank inputs. The pinned NCCL tuner v5 ABI
does not provide a distinct MNNVL fabric signal, and its domain count follows
the node count, so this policy deliberately returns no override for every
multi-node communicator.

## Spark TP4 protocol candidates

The `spark_tp4_ll_64k.bpf.o`, `spark_tp4_simple_64k.bpf.o`,
`spark_tp4_ll_1m.bpf.o`, and `spark_tp4_simple_64k_1m.bpf.o` objects are
separate A/B arms for a communicator with exactly four ranks on four nodes.
They override only AllReduce: the first two request Ring/LL versus
Ring/SIMPLE for 1 byte-64 KiB, the third requests Ring/LL for 1 byte-1 MiB,
and the fourth requests Ring/SIMPLE above 64 KiB through 1 MiB. The medium
window matched 443 of the first 1000 AllReduce calls in a four-Spark
DeepSeek pilot, giving it a real A/B target. Zero-byte calls, messages
above each arm's range, other collectives, and other topologies return no action. None sets a channel
count; NCCL retains its own channel limit. The plugin respects NCCL's
ignored algorithm/protocol entries. These are candidate policies, not
validated performance recommendations: compare each against both no plugin
and `noop.bpf.o` under the same live TP4 inference workload, and inspect
all ranks for plugin load, errors, request correctness, latency and
throughput before selecting an arm.

## Experimental agreed map across ranks

The `distributed_max_latency.bpf.o` example uses a verified `dist_merge`
program to take the maximum of rank-local latency slots. Set
`NCCL_POLICY_EXPERIMENTAL_DIST_MAP=1`, `NCCL_POLICY_BPF_PATH` to that object,
and both `NCCL_TUNER_PLUGIN` and `NCCL_PROFILER_PLUGIN` to this library.
Profiler v6 supplies the rank ID. The example supports 1-8 ranks, one rank per
process. Without `NCCL_POLICY_DIST_COORDINATOR`, ranks rendezvous by Unix
socket on one host with shared `/tmp`. For multiple hosts, set the same
`rank-0-host-or-IPv4:port` endpoint on every rank; rank 0 binds that specific
local interface. A multi-node communicator without an endpoint is rejected.

A round-0 exchange compares the BPF object and plugin-runtime hash before
the first BPF action. Each later 1024-call epoch exchanges measurements
asynchronously; activation occurs at the next 1024-call boundary after all
rank proposals agree. A missing rank, mismatch, or failed exchange returns an
error. The first-call handshake and activation may wait up to 30 seconds.
The TCP prepare/ACK/COMMIT protocol cannot make COMMIT delivery atomic:
a failure during COMMIT may leave peers with different outcomes. NCCL
call-count and host post-processing limitations also remain. CPU tests cover
four local processes using TCP loopback; multi-host Spark, GPU, and LLM
workloads have not been measured for this path.

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
| `NCCL_PROFILER_PLUGIN` | Set to the same library for the agreed-map example to supply the rank ID |
| `NCCL_POLICY_BPF_PATH` | Path to the `.bpf.o` policy file to load |
| `NCCL_POLICY_VERIFY_MODE` | `strict` (default): reject unsafe programs; `warning`: log and allow; `none`: skip verification |
| `NCCL_POLICY_PROFILER_MODE` | `native` (default) or `ebpf` telemetry updates |
| `NCCL_POLICY_PROFILER_BPF_PATH` | Path to `profiler_latency.bpf.o`; required in `ebpf` mode |
| `NCCL_POLICY_EXPERIMENTAL_DIST_MAP` | Set to `1` to load the agreed-map example |
| `NCCL_POLICY_DIST_COORDINATOR` | Shared `rank-0-host-or-IPv4:port` endpoint for multi-node TCP exchange; unset uses same-host Unix sockets |
| `NCCL_POLICY_BENCHMARK_READY` | Existing `1` opt-in prints the per-rank READY marker and compact cumulative tuner-call histograms; unset adds no counter updates |

With `NCCL_POLICY_BENCHMARK_READY=1`, the plugin summarizes AllReduce
(`AR`), AllGather (`AG`), and ReduceScatter (`RS`) calls in five
message-size buckets: <=16 KiB, <=64 KiB, <=1 MiB, <=16 MiB, and >16 MiB.
It prints cumulative counts at tuner calls 5, 100, 1000, each 10000
thereafter, and finalization. `hits` counts nonzero BPF action requests,
not confirmed NCCL algorithm/protocol selections; NCCL may ignore an
unavailable pair. CUDA Graph replay does not call the tuner again, so
these counts describe policy decisions during capture/uncaptured calls,
not the number of inference steps or requests.

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
