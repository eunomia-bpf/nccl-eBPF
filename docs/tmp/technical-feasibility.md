# Technical Feasibility of Injecting eBPF Policy Programs into NCCL

Source revisions used for this report:
- NCCL: `361915904b456d397e6e1578f8f65ea1a45bdd28`
- bpftime: `8a359fc86438b8b3b0b58e38cff0b190c7e68893`

## Executive Summary

- A stable control-plane integration for NCCL exists today through the NCCL plugin system, especially the tuner and net/collnet plugins.
- bpftime `LD_PRELOAD` is technically compatible with NCCL for host-side hooks. The low-risk path is to hook CPU functions only. Do not use bpftime's CUDA/PTX injection path in the first prototype.
- A single 100 ns policy hook is usually acceptable: about `1.0%` at a `10 us` collective and `0.01%` at a `1 ms` collective. Even a more conservative `190 ns` measured userspace-uprobe overhead stays below `2%` at `10 us`.
- The plugin APIs are good at choosing algorithms, protocols, channels, NICs, traffic class, and transport behavior. They are not a general "run arbitrary code on every collective" interface.
- Recommended approach: **hybrid**.
  - Use a mixed NCCL plugin for control decisions.
  - Use bpftime uprobes on exported NCCL APIs for observability, shadow-mode validation, and extra context capture.
  - Defer bpftime CUDA attach until after the host-side path is stable.

## Architecture Sketch

```text
                     +----------------------------------+
                     | External control plane           |
                     | - load/update eBPF policies      |
                     | - write policy maps              |
                     +----------------+-----------------+
                                      |
                         shared memory / bpftime maps
                                      |
+------------------------------------------------------------------------+
| Training worker process                                                |
|                                                                        |
|  LD_PRELOAD: libbpftime-agent.so (optional for uprobes/telemetry)      |
|      |                                                                 |
|      +-- uprobe on ncclCommInitRankConfig / ncclAllReduce / ...        |
|                                                                        |
|  libnccl.so                                                            |
|      |                                                                 |
|      +-- mixed policy plugin .so                                       |
|      |     - ncclTunerPlugin_v5      --> bpftime eval("tune", ...)     |
|      |     - ncclNetPlugin_v11       --> bpftime eval("net", ...)      |
|      |     - ncclCollNetPlugin_v11   --> bpftime eval("collnet", ...)  |
|      |     - ncclEnvPlugin_v1        --> optional coarse defaults      |
|      |                                                                 |
|      +-- NCCL core                                                     |
|             |                                                          |
|             +-- CUDA runtime / driver                                  |
|                    |                                                    |
|                    +-- GPU kernels                                      |
|                                                                        |
+------------------------------------------------------------------------+

MVP rule: keep bpftime on the host side only.
Do not enable bpftime CUDA/PTX injection in the first prototype.
```

## 1. NCCL Plugin Loading Mechanism

### Analysis

NCCL has a generic shared-library loader in `src/plugin/plugin_open.cc`. It tries `dlopen(..., RTLD_NOW | RTLD_LOCAL)` on:

- the exact string provided by the environment variable, if present
- otherwise the default basename for that plugin type, such as `libnccl-net.so`
- if the environment variable is a bare name like `foo`, NCCL also tries `libnccl-<type>-foo.so`

Important consequences:

- NCCL does not implement its own directory search. It relies on normal dynamic-loader resolution (`LD_LIBRARY_PATH`, RPATH/RUNPATH, default system paths).
- `NCCL_*_PLUGIN=none` disables external loading for env/tuner/profiler, and `NCCL_NET_PLUGIN=none` suppresses external net plugins.
- Tuner and profiler have a special fallback: if their own library cannot be opened, they try to reuse the already-open net plugin library handle. This is the mechanism that makes a "mixed" plugin feasible.

Net is the most complex loader:

- `NCCL_NET_PLUGIN` is read once and may contain a comma-separated list of external plugin names.
- If no external plugin is configured, NCCL still appends two internal transports: `ncclNetIb` and `ncclNetSocket`.
- For an external net plugin, NCCL probes symbol versions from newest to oldest:
  - `ncclNetPlugin_v11` down to `ncclNetPlugin_v6`
  - optional `ncclCollNetPlugin_v11` down to `ncclCollNetPlugin_v6`
- `ncclNet->init(...)` is called once per communicator to produce a per-communicator `netContext`, even though the shared object is process-global.

Tuner loading:

- `NCCL_TUNER_PLUGIN` chooses the library.
- NCCL probes `ncclTunerPlugin_v5`, then v4, v3, v2.
- The plugin is loaded late in communicator initialization, after `ncclTopoInitTunerConstants`.
- If present, NCCL calls `comm->tuner->init(&comm->tunerContext, comm->commHash, ...)`.

Profiler loading:

- `NCCL_PROFILER_PLUGIN` chooses the library.
- Symbol names are irregular: NCCL probes `ncclProfiler_v6` down to `ncclProfiler_v1`, not `ncclProfilerPlugin_vX`.
- The profiler is initialized before proxy-thread creation.
- It gets a per-communicator context plus `commHash`, `commName`, `nNodes`, `nRanks`, and `rank`.

Env loading:

- `NCCL_ENV_PLUGIN` is read via `std::getenv`, because the env plugin must be chosen before `ncclGetEnv()` exists.
- NCCL probes `ncclEnvPlugin_v1`.
- If that fails, it falls back to an internal built-in env plugin that just calls `std::getenv`.
- The env plugin is process-global and initialized once through `ncclInitEnv()`.

### Evidence

- Generic plugin loader and naming rules:
  - [NCCL `src/plugin/plugin_open.cc` lines 24-113](https://github.com/NVIDIA/nccl/blob/361915904b456d397e6e1578f8f65ea1a45bdd28/src/plugin/plugin_open.cc#L24-L113)
- Reusing the net plugin handle for other plugin types:
  - [NCCL `src/plugin/plugin_open.cc` lines 136-143](https://github.com/NVIDIA/nccl/blob/361915904b456d397e6e1578f8f65ea1a45bdd28/src/plugin/plugin_open.cc#L136-L143)
- Net plugin list, internal IB/socket fallback, per-comm init:
  - [NCCL `src/plugin/net.cc` lines 236-300](https://github.com/NVIDIA/nccl/blob/361915904b456d397e6e1578f8f65ea1a45bdd28/src/plugin/net.cc#L236-L300)
  - [NCCL `src/plugin/net.cc` lines 155-198](https://github.com/NVIDIA/nccl/blob/361915904b456d397e6e1578f8f65ea1a45bdd28/src/plugin/net.cc#L155-L198)
- Net and CollNet symbol names:
  - [NCCL `src/plugin/net/net_v11.cc` lines 15-30](https://github.com/NVIDIA/nccl/blob/361915904b456d397e6e1578f8f65ea1a45bdd28/src/plugin/net/net_v11.cc#L15-L30)
- Tuner loading and version probe order:
  - [NCCL `src/plugin/tuner.cc` lines 57-91](https://github.com/NVIDIA/nccl/blob/361915904b456d397e6e1578f8f65ea1a45bdd28/src/plugin/tuner.cc#L57-L91)
  - [NCCL `src/plugin/tuner/tuner_v5.cc` lines 15-20](https://github.com/NVIDIA/nccl/blob/361915904b456d397e6e1578f8f65ea1a45bdd28/src/plugin/tuner/tuner_v5.cc#L15-L20)
- Profiler loading and symbol names:
  - [NCCL `src/plugin/profiler.cc` lines 58-98](https://github.com/NVIDIA/nccl/blob/361915904b456d397e6e1578f8f65ea1a45bdd28/src/plugin/profiler.cc#L58-L98)
  - [NCCL `src/plugin/profiler/profiler_v6.cc` lines 16-22](https://github.com/NVIDIA/nccl/blob/361915904b456d397e6e1578f8f65ea1a45bdd28/src/plugin/profiler/profiler_v6.cc#L16-L22)
- Env plugin singleton load and built-in fallback:
  - [NCCL `src/plugin/env.cc` lines 37-95](https://github.com/NVIDIA/nccl/blob/361915904b456d397e6e1578f8f65ea1a45bdd28/src/plugin/env.cc#L37-L95)
  - [NCCL `src/plugin/env/env_v1.cc` lines 15-40](https://github.com/NVIDIA/nccl/blob/361915904b456d397e6e1578f8f65ea1a45bdd28/src/plugin/env/env_v1.cc#L15-L40)
- Where init happens in communicator setup:
  - [NCCL `src/init.cc` lines 155-162](https://github.com/NVIDIA/nccl/blob/361915904b456d397e6e1578f8f65ea1a45bdd28/src/init.cc#L155-L162)
  - [NCCL `src/init.cc` lines 430-449](https://github.com/NVIDIA/nccl/blob/361915904b456d397e6e1578f8f65ea1a45bdd28/src/init.cc#L430-L449)
  - [NCCL `src/init.cc` lines 1331-1448](https://github.com/NVIDIA/nccl/blob/361915904b456d397e6e1578f8f65ea1a45bdd28/src/init.cc#L1331-L1448)

### Conclusion

NCCL already provides a robust in-process injection point. The cleanest policy surface is a shared object that exports one or more NCCL plugin symbols. A single mixed plugin can plausibly carry net/collnet and tuner policy logic in one library.

## 2. Can bpftime `LD_PRELOAD` Work with NCCL?

### Analysis

Yes, with an important scope boundary:

- **Host-side bpftime hooks are feasible and low risk.**
  - bpftime's agent uses `LD_PRELOAD` and interposes `__libc_start_main`.
  - Userspace uprobes are implemented with Frida's function interceptor (`gum_interceptor_attach` / `gum_interceptor_replace`).
  - This patches host text pages and host function entry/exit. It does not rewrite CUDA device memory.

- **CUDA memory management by NCCL does not inherently conflict with host-side binary rewriting.**
  - NCCL's GPU buffers live in CUDA-managed device memory.
  - Frida-based host uprobes operate on the process's CPU-side code pages.
  - So there is no direct conflict between "rewrite a host function prologue" and "NCCL uses CUDA device memory."

- **The risky part is bpftime's optional CUDA attach path, not the basic preload path.**
  - If bpftime is built with `BPFTIME_ENABLE_CUDA_ATTACH`, the agent also intercepts `__cudaRegisterFatBinary`.
  - The CUDA attach module hooks `__cudaRegisterFatBinary`, `__cudaRegisterFunction`, `__cudaRegisterFatBinaryEnd`, and `cudaLaunchKernel`.
  - It extracts PTX, inserts instrumentation, recompiles PTX, and reinjects the modified kernels.
  - bpftime documents this path as limited to specific CUDA version formats and requiring a compatible NVCC toolchain.
  - That is much more invasive than ordinary host-side uprobes.

Practical challenges for NCCL:

1. **Preload ordering and early CUDA registration**
   - NCCL and user code may cause CUDA fatbin registration very early.
   - bpftime already special-cases `__cudaRegisterFatBinary` because it may fire before the normal LLVM VM registration path is ready.

2. **CUDA attach instability**
   - NCCL uses CUDA heavily, including internal kernels, streams, and possibly graphs.
   - bpftime's CUDA attach is explicitly limited and version-sensitive.
   - If enabled, it becomes a compatibility project with CUDA and NCCL release combinations, not just a policy hook.

3. **Optional GPU map / GDRCopy path**
   - bpftime can use GDRCopy for host-side GPU map reads, but falls back to `cuMemcpyDtoH` if unavailable.
   - This is not a correctness conflict with NCCL, but it adds another GPU/driver interaction surface that an MVP does not need.

4. **In-process safety**
   - bpftime verifies eBPF bytecode, but the wrapper plugin and Frida glue are native C/C++ in the NCCL process.
   - A bug in that host-side wrapper can still crash the process.

5. **Hook count matters**
   - Hooking one public NCCL API per collective is easy to budget.
   - Hooking many internal functions on every proxy step, transport progress loop, or CUDA launch could become noisy.

### Evidence

- bpftime `LD_PRELOAD` model and userspace loader:
  - [bpftime `README.md` lines 30-44](https://github.com/eunomia-bpf/bpftime/blob/8a359fc86438b8b3b0b58e38cff0b190c7e68893/README.md#L30-L44)
  - [bpftime `usage.md` lines 117-133](https://github.com/eunomia-bpf/bpftime/blob/8a359fc86438b8b3b0b58e38cff0b190c7e68893/usage.md#L117-L133)
- Agent preload entry and early CUDA registration hook:
  - [bpftime `runtime/agent/agent.cpp` lines 91-138](https://github.com/eunomia-bpf/bpftime/blob/8a359fc86438b8b3b0b58e38cff0b190c7e68893/runtime/agent/agent.cpp#L91-L138)
- Userspace uprobe implementation via Frida:
  - [bpftime `attach/frida_uprobe_attach_impl/src/frida_internal_attach_entry.cpp` lines 128-166](https://github.com/eunomia-bpf/bpftime/blob/8a359fc86438b8b3b0b58e38cff0b190c7e68893/attach/frida_uprobe_attach_impl/src/frida_internal_attach_entry.cpp#L128-L166)
- CUDA attach scope and mechanism:
  - [bpftime `attach/README.md` lines 101-119](https://github.com/eunomia-bpf/bpftime/blob/8a359fc86438b8b3b0b58e38cff0b190c7e68893/attach/README.md#L101-L119)
  - [bpftime `attach/nv_attach_impl/README.md` lines 10-19](https://github.com/eunomia-bpf/bpftime/blob/8a359fc86438b8b3b0b58e38cff0b190c7e68893/attach/nv_attach_impl/README.md#L10-L19)
  - [bpftime `attach/nv_attach_impl/README.md` lines 106-111](https://github.com/eunomia-bpf/bpftime/blob/8a359fc86438b8b3b0b58e38cff0b190c7e68893/attach/nv_attach_impl/README.md#L106-L111)
- Optional GDRCopy fast-path with fallback:
  - [bpftime `attach/nv_attach_impl/README.md` lines 67-69](https://github.com/eunomia-bpf/bpftime/blob/8a359fc86438b8b3b0b58e38cff0b190c7e68893/attach/nv_attach_impl/README.md#L67-L69)
  - [bpftime `runtime/src/bpf_map/gpu/nv_gpu_gdrcopy.cpp` lines 70-113](https://github.com/eunomia-bpf/bpftime/blob/8a359fc86438b8b3b0b58e38cff0b190c7e68893/runtime/src/bpf_map/gpu/nv_gpu_gdrcopy.cpp#L70-L113)

### Conclusion

bpftime `LD_PRELOAD` can work with NCCL if you keep the first implementation on the **host side only**:

- safe enough for uprobes on exported NCCL APIs
- safe enough to call bpftime from an NCCL plugin wrapper
- **not** low risk for immediate CUDA/PTX injection into NCCL kernels

Recommended boundary for the MVP: **preload bpftime agent, attach host uprobes, and embed or call bpftime from the NCCL plugin; leave CUDA attach disabled.**

## 3. Overhead Budget

### Analysis

Formula:

`overhead_pct = hook_latency / collective_latency * 100`

The user's target budget is about `100 ns` per policy hook. bpftime's repo currently also shows:

- an attach-system estimate of `~10-100 ns` per Frida uprobe
- a recent benchmark average of about `190 ns` for userspace uprobes
- an embedded VM average of about `106 ns`

So the right way to read the budget is:

- `100 ns` is achievable as an optimistic target
- `190 ns` is a reasonable conservative bound from the current repo's benchmark artifact

### Sensitivity Table

| Collective latency | 100 ns hook | 190 ns hook | 300 ns total (for 3 small hooks) |
| --- | ---: | ---: | ---: |
| 10 us | 1.000% | 1.900% | 3.000% |
| 20 us | 0.500% | 0.950% | 1.500% |
| 50 us | 0.200% | 0.380% | 0.600% |
| 100 us | 0.100% | 0.190% | 0.300% |
| 250 us | 0.040% | 0.076% | 0.120% |
| 1000 us | 0.010% | 0.019% | 0.030% |

### Representative NCCL Scenarios

These are inference-based buckets, not fixed NCCL guarantees. They use the user's stated `10-1000 us` real-workload range.

| Scenario | Representative size / type | Assumed latency | 100 ns overhead | Assessment |
| --- | --- | ---: | ---: | --- |
| Small latency-bound collective | small `AllReduce`, `AllGather`, `ReduceScatter` | 10-20 us | 0.5-1.0% | Acceptable if the policy path is a single fast decision and does not allocate/block |
| Medium collective | mid-size `AllReduce` / `ReduceScatter` | 50-100 us | 0.1-0.2% | Easily acceptable |
| Large bandwidth-bound collective | large `Broadcast` / `Reduce` / `AllGather` | 100-250 us | 0.04-0.10% | Negligible |
| Large multi-node collective | large `AllReduce` / `ReduceScatter` | 250-1000 us | 0.01-0.04% | Negligible |

Implications:

- A single policy hook per collective is easy to justify.
- A few host-side hooks are still acceptable for most workloads.
- The only sensitive zone is the smallest latency-bound collectives, where a stack of several hooks can reach the low single-digit percentages.

### Evidence

- bpftime attach-system estimate:
  - [bpftime `attach/README.md` lines 187-191](https://github.com/eunomia-bpf/bpftime/blob/8a359fc86438b8b3b0b58e38cff0b190c7e68893/attach/README.md#L187-L191)
- Recent benchmark numbers:
  - [bpftime `benchmark/uprobe/example_results.md` lines 21-27](https://github.com/eunomia-bpf/bpftime/blob/8a359fc86438b8b3b0b58e38cff0b190c7e68893/benchmark/uprobe/example_results.md#L21-L27)
  - [bpftime `benchmark/uprobe/example_results.md` lines 68-73](https://github.com/eunomia-bpf/bpftime/blob/8a359fc86438b8b3b0b58e38cff0b190c7e68893/benchmark/uprobe/example_results.md#L68-L73)

### Conclusion

The overhead budget is acceptable for NCCL if the policy path stays short:

- one hook: yes
- one hook plus a small map lookup: still likely yes
- many internal hooks per proxy step or per CUDA launch: not an MVP choice

## 4. Policy Action Space Via NCCL Plugins

### 4.1 Tuner Plugin

| API function | Concrete policy action | Limits |
| --- | --- | --- |
| `init(void** ctx, uint64_t commId, size_t nRanks, size_t nNodes, logFn, nvlDomainInfo, constants)` | Create a per-communicator policy context; load thresholds keyed by communicator size/topology; optionally modify NCCL tuning constants before use | No rank, `commName`, root, datatype, or payload pointer is provided |
| `getCollInfo(void* ctx, ncclFunc_t collType, size_t nBytes, int numPipeOps, float** collCostTable, int numAlgo, int numProto, int regBuff, int* nChannels)` | Choose preferred algo/proto indirectly by rewriting the cost table; choose `nChannels` directly | Cannot directly set `nThreads` / `nWarps`; cannot inspect buffers, root, datatype, or reduction op; policy must bias the cost table, then NCCL still runs `topoGetAlgoInfo` |
| `finalize(void* ctx)` | Tear down per-communicator policy state | Cleanup only |

How algo/proto control actually works:

- NCCL does **not** give the tuner explicit `algorithm` and `protocol` output arguments in v5.
- The plugin selects them by lowering or raising entries in `collCostTable`.
- NCCL then runs its own `topoGetAlgoInfo(...)` and fills anything the plugin left unspecified.

### 4.2 Net Plugin (`ncclNet_v11_t`)

| API function | Concrete policy action | Limits |
| --- | --- | --- |
| `init` | Create per-communicator transport context; apply QoS/traffic-class policy; register profiler callback | No rank argument in init |
| `devices` | Advertise how many NICs the policy exposes to NCCL | Cannot influence non-network transports like NVLink or SHM |
| `getProperties` | Hide or reveal devices; advertise speed/latency, pointer support, max bytes, offload support, device type/version | Wrong values can destabilize NCCL topology and transport selection |
| `listen` | Choose the receiving endpoint and device used for an incoming connection | Acts at transport level, not collective semantic level |
| `connect` | Choose the sending endpoint, route, and optional device-offload handle for a peer connection | Connection creation only |
| `accept` | Finalize the receiver side of connection establishment | Connection creation only |
| `regMr` | Approve/deny/register host or CUDA buffers for network use | Transport memory-registration policy only |
| `regMrDmaBuf` | Same as `regMr`, but for DMA-BUF paths | Linux / DMA-BUF specific path |
| `deregMr` | Reclaim transport registrations | Cleanup only |
| `isend` | Rate-limit, queue, or steer a send on the chosen transport | Cannot change NCCL collective semantics |
| `irecv` | Rate-limit, queue, or steer receives | Same |
| `iflush` | Control flush/fence behavior for CUDA-visible receives | Transport visibility only |
| `test` | Define request completion semantics and progress behavior | Completion path only |
| `closeSend` | Cleanup send endpoint | Cleanup only |
| `closeRecv` | Cleanup receive endpoint | Cleanup only |
| `closeListen` | Cleanup listening endpoint | Cleanup only |
| `getDeviceMr` | Export a memory handle in a device-usable form | Offload / transport detail only |
| `irecvConsumed` | Receive a signal that the device has consumed a recv | Feedback hook, not a scheduler |
| `makeVDevice` | Create virtual NICs and partition or aggregate real NICs | Device virtualization only |
| `finalize` | Tear down per-communicator network state | Cleanup only |
| `setNetAttr` | Adapt transport policy to NCCL-provided hints: operation mask, algo, proto, concurrent peers, flows-per-peer | Hints arrive after NCCL has already chosen the high-level collective algorithm and protocol |

### 4.3 CollNet Plugin (`ncclCollNet_v11_t`)

| API function | Concrete policy action | Limits |
| --- | --- | --- |
| `init` | Create per-communicator in-network-collective context | Only for CollNet-capable paths |
| `devices` | Advertise CollNet-capable devices | No effect if NCCL is not using CollNet |
| `getProperties` | Expose/withhold device capabilities for collective offload | Device-level only |
| `listen` | Prepare a collective endpoint | Setup only |
| `connect` | Form the collective network group and map handles to ranks | Setup only |
| `reduceSupport` | Permit or deny particular `(datatype, redOp)` combinations | Only on CollNet path |
| `regMr` | Register collective buffers | Transport memory-registration only |
| `regMrDmaBuf` | Register DMA-BUF collective buffers | Same |
| `deregMr` | Tear down collective registrations | Cleanup only |
| `iallreduce` | Implement inter-node allreduce offload policy | Only when NCCL selects CollNet |
| `iallgather` | Implement inter-node allgather offload policy | Same |
| `ireducescatter` | Implement inter-node reduce-scatter offload policy | Same |
| `iflush` | Control receive visibility semantics | Transport detail only |
| `test` | Define request progress/completion | Progress path only |
| `closeColl` | Cleanup collective endpoint | Cleanup only |
| `closeListen` | Cleanup listen endpoint | Cleanup only |
| `makeVDevice` | Virtualize CollNet devices | Device-level only |
| `finalize` | Tear down per-communicator CollNet state | Cleanup only |

### 4.4 Env Plugin

| API function | Concrete policy action | Limits |
| --- | --- | --- |
| `init(uint8_t major, minor, patch, const char* suffix)` | Initialize a process-global policy source for environment resolution | Global singleton, not per communicator |
| `finalize()` | Tear down process-global env policy state | Cleanup only |
| `getEnv(const char* name)` | Rewrite or synthesize process-level NCCL settings such as `NCCL_NET`, `NCCL_COLLNET_ENABLE`, `NCCL_ALGO`, `NCCL_PROTO`, `NCCL_MIN_CTAS`, `NCCL_MAX_CTAS`, `NCCL_NCHANNELS_PER_NET_PEER`, `NCCL_TUNER_PLUGIN`, `NCCL_PROFILER_PLUGIN`, and many others read through `ncclGetEnv()` / `NCCL_PARAM(...)` | No communicator context; many parameters are cached after first read; cannot control `NCCL_ENV_PLUGIN` or `NCCL_CONF_FILE` because those are read via `std::getenv` before the env plugin exists |

### 4.5 Profiler Plugin

Profiler exists, but it is an observation API, not a control API.

| API function | Concrete policy action | Limits |
| --- | --- | --- |
| `init` | Start a per-communicator telemetry context and choose the event mask | No direct control of NCCL behavior |
| `startEvent` | Observe the start of a NCCL event | Observe only |
| `stopEvent` | Observe the end of a NCCL event | Observe only |
| `recordEventState` | Observe state transitions and net-plugin updates | Observe only |
| `finalize` | Cleanup telemetry context | Observe only |

### What Is Not Possible Through the Plugin System?

- No plugin API gives a general "before every collective, run policy and optionally deny" callback.
- Tuner cannot directly set thread count / warps in v5; it can only bias algo/proto via the cost table and set `nChannels`.
- Tuner cannot see root, reduction op, datatype, send/recv pointers, or stream in `getCollInfo`.
- Net and CollNet only control transport behavior. They do not control intra-node SHM/NVLink/P2P transports outside the network path.
- Env is process-global and cached. It is poor for multi-tenant per-communicator policy.
- Profiler is observe-only.

### Evidence

- Tuner API surface:
  - [NCCL `src/include/plugin/tuner/tuner_v5.h` lines 38-86](https://github.com/NVIDIA/nccl/blob/361915904b456d397e6e1578f8f65ea1a45bdd28/src/include/plugin/tuner/tuner_v5.h#L38-L86)
- NCCL calling order for tuner:
  - [NCCL `src/enqueue.cc` lines 2026-2060](https://github.com/NVIDIA/nccl/blob/361915904b456d397e6e1578f8f65ea1a45bdd28/src/enqueue.cc#L2026-L2060)
- Example tuner preferring an algo/proto by rewriting the cost table:
  - [NCCL `plugins/tuner/example/plugin.c` lines 394-423](https://github.com/NVIDIA/nccl/blob/361915904b456d397e6e1578f8f65ea1a45bdd28/plugins/tuner/example/plugin.c#L394-L423)
- Net / CollNet API surface:
  - [NCCL `src/include/plugin/net/net_v11.h` lines 69-188](https://github.com/NVIDIA/nccl/blob/361915904b456d397e6e1578f8f65ea1a45bdd28/src/include/plugin/net/net_v11.h#L69-L188)
- NCCL passing op/algo/proto/concurrency hints into `setNetAttr`:
  - [NCCL `src/transport/net.cc` lines 215-276](https://github.com/NVIDIA/nccl/blob/361915904b456d397e6e1578f8f65ea1a45bdd28/src/transport/net.cc#L215-L276)
- Env API surface and caching path:
  - [NCCL `src/include/plugin/env/env_v1.h` lines 13-34](https://github.com/NVIDIA/nccl/blob/361915904b456d397e6e1578f8f65ea1a45bdd28/src/include/plugin/env/env_v1.h#L13-L34)
  - [NCCL `src/misc/param.cc` lines 106-140](https://github.com/NVIDIA/nccl/blob/361915904b456d397e6e1578f8f65ea1a45bdd28/src/misc/param.cc#L106-L140)
- Parameters read before env-plugin initialization:
  - [NCCL `src/misc/param.cc` lines 58-72](https://github.com/NVIDIA/nccl/blob/361915904b456d397e6e1578f8f65ea1a45bdd28/src/misc/param.cc#L58-L72)
  - [NCCL `src/plugin/env.cc` lines 41-48](https://github.com/NVIDIA/nccl/blob/361915904b456d397e6e1578f8f65ea1a45bdd28/src/plugin/env.cc#L41-L48)
- Profiler API surface:
  - [NCCL `src/include/plugin/profiler/profiler_v6.h` lines 128-145](https://github.com/NVIDIA/nccl/blob/361915904b456d397e6e1578f8f65ea1a45bdd28/src/include/plugin/profiler/profiler_v6.h#L128-L145)

### Conclusion

The plugin system exposes a meaningful but narrow action space:

- **tuner**: choose algo/proto/nChannels indirectly and per communicator
- **net/collnet**: choose devices, connections, registration, request progress, and transport hints
- **env**: rewrite process-level defaults
- **profiler**: observe only

For actual control, tuner plus net/collnet are the useful hooks.

## 5. Alternative Approach: Uprobe-Based Hooks

### Analysis

bpftime can attach uprobes to NCCL functions instead of using NCCL plugins. This is feasible, but the tradeoff depends on which symbols are targeted.

#### Stable uprobe targets

Public NCCL APIs such as:

- `ncclCommInitRank`
- `ncclCommInitRankConfig`
- `ncclAllReduce`
- `ncclBroadcast`
- `ncclReduceScatter`
- `ncclSend`
- `ncclRecv`

are good uprobe targets because they are declared with `NCCL_API(...)`, which gives them default visibility.

These hooks can observe:

- communicator pointer
- buffer pointers
- count
- datatype
- root / redop / peer
- CUDA stream

This is actually richer request context than the tuner plugin sees.

#### Weak uprobe targets

Internal functions such as:

- `ncclGetAlgoInfo`
- `topoGetAlgoInfo`
- `ncclTopoTuneModel`
- `ncclProxyConnect`

are not declared with `NCCL_API`. NCCL is built with `-fvisibility=hidden`, so these internals are not stable public symbols. They may be:

- hidden
- inlined
- moved across releases
- unavailable in stripped binaries

That makes internal uprobes fragile as a production control path.

#### Control capability

bpftime's Frida backend supports:

- entry probes
- return probes
- override probes
- full replacement (`ureplace`)

So in theory you could override a public NCCL API. In practice, that means you now own NCCL's front-end semantics, argument validation, grouping rules, async error model, and interaction with CUDA streams. That is much riskier than using NCCL's supported plugin ABI.

### Tradeoff Summary

| Approach | Strengths | Weaknesses |
| --- | --- | --- |
| Plugin-based | Supported ABI; can change tuner and transport decisions directly; per-communicator contexts exist | Narrow action surface; env is global; requires writing native plugin wrappers |
| Uprobe-based | Works with stock NCCL builds; best for observe/shadow mode; can see rich call arguments on public APIs | Internal decision points are unstable; direct control requires override/replace and is brittle |
| Hybrid | Stable control from plugins plus rich telemetry/context from public API uprobes | More moving parts, but the separation of concerns is clean |

### Evidence

- Public API visibility comes from `NCCL_API`:
  - [NCCL `src/include/core.h` lines 17-30](https://github.com/NVIDIA/nccl/blob/361915904b456d397e6e1578f8f65ea1a45bdd28/src/include/core.h#L17-L30)
- NCCL default build uses hidden visibility:
  - [NCCL `makefiles/common.mk` line 79](https://github.com/NVIDIA/nccl/blob/361915904b456d397e6e1578f8f65ea1a45bdd28/makefiles/common.mk#L79)
- Example public API wrapper:
  - [NCCL `src/collectives.cc` lines 111-122](https://github.com/NVIDIA/nccl/blob/361915904b456d397e6e1578f8f65ea1a45bdd28/src/collectives.cc#L111-L122)
- bpftime supports uprobe, uretprobe, override, and replace:
  - [bpftime `attach/README.md` lines 60-74](https://github.com/eunomia-bpf/bpftime/blob/8a359fc86438b8b3b0b58e38cff0b190c7e68893/attach/README.md#L60-L74)
  - [bpftime `attach/frida_uprobe_attach_impl/src/frida_internal_attach_entry.cpp` lines 134-156](https://github.com/eunomia-bpf/bpftime/blob/8a359fc86438b8b3b0b58e38cff0b190c7e68893/attach/frida_uprobe_attach_impl/src/frida_internal_attach_entry.cpp#L134-L156)

### Conclusion

Uprobes are excellent for **observation and shadow mode**, but they are a weaker primary control plane than NCCL plugins. Use uprobes to watch the public API and use plugins to make the actual tuning and transport decisions.

## 6. Multi-Tenant Scenario: Different Policies for Multiple Communicators

### Analysis

Short answer:

- **Tuner**: yes, per communicator
- **Net/CollNet**: yes, per communicator, with caveats around shared-resource child communicators
- **Profiler**: yes, per communicator
- **Env**: no, effectively process-global

#### Tuner isolation

NCCL calls `tuner->init(&comm->tunerContext, comm->commHash, comm->nRanks, comm->nNodes, ...)` for each communicator. That means the plugin can allocate a separate policy context per communicator.

But there are two caveats:

1. `commHash` is not globally unique by itself.
2. Tuner init does not receive rank or `commName`.

So for an external control plane, `commHash` alone is not a sufficient tenant key. The safest key inside the process is the plugin-allocated context pointer. For out-of-process control, add an explicit policy identifier.

#### Net/CollNet isolation

The net plugin receives a fresh per-communicator `netContext` and `collNetContext` through `ncclNet->init(...)` / `ncclCollNet->init(...)`. That is good for isolation.

However, child communicators can inherit the parent's net contexts through `ncclNetInitFromParent(...)` when NCCL shares resources. That means:

- two communicators in the same process can intentionally or accidentally share network policy state
- if strict isolation is required, resource sharing must be disabled for split/shrink/grow cases

#### Profiler isolation

Profiler is the easiest place to tag tenants, because `init(...)` includes:

- `commId`
- `commName`
- `nNodes`
- `nRanks`
- `rank`

That makes it a good observation path for validating tenant boundaries even if the control path uses tuner/net.

#### Env non-isolation

The env plugin is a singleton and `ncclGetEnv()` feeds many cached parameters. It cannot express different policies for two communicators in the same process.

### Policy-Isolation Design

Recommended design:

1. Add an explicit `policy_id` at communicator creation time.
   - Best option: carry it out of band from the application.
   - If available, `ncclConfig_t.commName` is a practical carrier for profiler-side labeling.

2. In the plugin:
   - allocate one bpftime-backed policy context per communicator
   - store the selected `policy_id` in that context
   - keep per-communicator fast-path state in normal process memory
   - keep mutable control-plane state in bpftime shared maps keyed by `policy_id`

3. For split/shrink/grow:
   - either intentionally inherit the parent policy
   - or disable NCCL shared resources so child communicators get independent net contexts

4. Do not key the control plane by `commHash` alone.
   - Use at least `{policy_id}`
   - or `{commHash, rank, process id}`
   - or an app-assigned unique tenant identifier

### Evidence

- Tuner per-communicator init:
  - [NCCL `src/init.cc` lines 1445-1448](https://github.com/NVIDIA/nccl/blob/361915904b456d397e6e1578f8f65ea1a45bdd28/src/init.cc#L1445-L1448)
- Profiler per-communicator init:
  - [NCCL `src/plugin/profiler.cc` lines 218-239](https://github.com/NVIDIA/nccl/blob/361915904b456d397e6e1578f8f65ea1a45bdd28/src/plugin/profiler.cc#L218-L239)
- Net per-communicator init:
  - [NCCL `src/plugin/net.cc` lines 155-176](https://github.com/NVIDIA/nccl/blob/361915904b456d397e6e1578f8f65ea1a45bdd28/src/plugin/net.cc#L155-L176)
- Parent-context reuse for child communicators:
  - [NCCL `src/plugin/net.cc` lines 330-344](https://github.com/NVIDIA/nccl/blob/361915904b456d397e6e1578f8f65ea1a45bdd28/src/plugin/net.cc#L330-L344)
- `commHash` is not guaranteed unique:
  - [NCCL `src/ras/ras_internal.h` lines 46-52](https://github.com/NVIDIA/nccl/blob/361915904b456d397e6e1578f8f65ea1a45bdd28/src/ras/ras_internal.h#L46-L52)
- Communicator config fields:
  - [NCCL `src/nccl.h.in` lines 83-105](https://github.com/NVIDIA/nccl/blob/361915904b456d397e6e1578f8f65ea1a45bdd28/src/nccl.h.in#L83-L105)

### Conclusion

Multiple communicators in one process can have different policies, but only if policy state is attached to the communicator-specific plugin context. Env-plugin-based isolation is not viable. Split/shrink/grow resource sharing is the main caveat.

## Risk Assessment

| Risk | Why it matters | Mitigation |
| --- | --- | --- |
| NCCL plugin ABI/version drift | Symbol versions and struct layouts may change across NCCL releases | Pin supported NCCL versions and export multiple plugin symbol versions where needed |
| bpftime CUDA attach instability | PTX rewriting is version-sensitive and more invasive than host uprobes | Keep CUDA attach disabled in the MVP |
| Wrapper crashes inside NCCL process | The NCCL plugin wrapper is native code, not verified eBPF | Keep wrapper logic tiny; call into bpftime VM for decisions; fuzz and stress test |
| Overhead on tiny collectives | Small latency-bound collectives have the tightest budget | One fast hook only on the hot path; cache results per communicator / size bucket |
| Reentrancy / deadlock | A policy path that calls back into NCCL/CUDA or blocks can deadlock | Make policy evaluation non-blocking, allocation-free, and side-effect minimal |
| Incorrect transport properties | Bad `getProperties` / `setNetAttr` outputs can mislead NCCL | Start in observe/shadow mode; gate enforcement behind allow-lists |
| Weak tenant keys | `commHash` is not unique enough | Use explicit `policy_id`; avoid `commHash`-only indexing |
| Shared child communicators | Split/shrink/grow may inherit parent net contexts | Disable resource sharing when strict policy isolation is required |
| Env plugin surprises | Process-global cached settings are hard to reason about | Use env plugin only for coarse process defaults, not tenant isolation |

## Recommended Approach

### Recommendation

Use **hybrid**:

1. **Primary control**: NCCL plugin-based.
   - Build a mixed plugin that exports:
     - `ncclTunerPlugin_v5`
     - `ncclNetPlugin_v11`
     - optional `ncclCollNetPlugin_v11`
   - The wrapper plugin calls into bpftime for policy evaluation.

2. **Primary observability**: bpftime uprobes on exported NCCL APIs.
   - Hook `ncclCommInitRankConfig`
   - Hook `ncclAllReduce`, `ncclBroadcast`, `ncclReduceScatter`, and optionally `ncclSend`/`ncclRecv`
   - Use these only for telemetry and shadow-mode validation in the first phase

3. **Avoid in MVP**
   - bpftime CUDA/PTX attach
   - env plugin for per-tenant behavior
   - internal-function uprobes as the main control path

### Why Hybrid Wins

- Plugin path gives stable and supported control over tuning and transport.
- Uprobe path gives richer request context and easier rollout on stock NCCL builds.
- Combining them lets you validate policy logic before enforcing it.

If forced to pick only one:

- for control: **plugin-based**
- for telemetry-only experimentation: **uprobe-based**

## Minimum Viable Prototype Plan

1. **Phase 0: Observe only**
   - Preload `libbpftime-agent.so` into an NCCL test workload.
   - Attach uprobes to exported APIs:
     - `ncclCommInitRankConfig`
     - `ncclAllReduce`
     - `ncclBroadcast`
     - `ncclReduceScatter`
   - Record `comm`, `count`, `datatype`, `op/root`, `stream`, and timestamps into bpftime maps.
   - Goal: verify hook stability and measure per-call overhead with no control changes.

2. **Phase 1: Tuner-only plugin**
   - Implement `libnccl-tuner-policy.so` exporting `ncclTunerPlugin_v5`.
   - In `getCollInfo`, call a tiny bpftime policy that:
     - rewrites `collCostTable` to prefer one algo/proto
     - sets `nChannels`
   - Start in shadow mode:
     - compute the policy decision
     - log it
     - optionally do not enforce yet

3. **Phase 2: Mixed tuner + net plugin**
   - Export `ncclNetPlugin_v11` and optionally `ncclCollNetPlugin_v11` from the same library.
   - Add per-communicator contexts.
   - Implement:
     - device filtering in `getProperties`
     - QoS in `init(trafficClass)`
     - transport adaptation in `setNetAttr`

4. **Phase 3: Tenant isolation**
   - Introduce explicit `policy_id`.
   - Store `policy_id -> rules` in bpftime shared maps.
   - Attach `policy_id` to communicator-local plugin context.
   - Test multiple communicators in the same process.
   - Test split/shrink/grow with shared resources both enabled and disabled.

5. **Phase 4: Enforcement**
   - Turn on actual algo/proto/channel overrides.
   - Turn on transport filtering/steering in net plugin.
   - Keep uprobes for post-decision telemetry and regression checks.

6. **Phase 5: Optional later work**
   - Evaluate env plugin for coarse process defaults.
   - Evaluate internal uprobes only if a missing context field is truly required.
   - Revisit bpftime CUDA attach only after the host-side path is production-stable.

## Bottom Line

Injecting eBPF policy into NCCL is technically feasible today, but the feasible boundary is not "arbitrary eBPF anywhere in NCCL." The viable production path is:

- NCCL plugins for supported control decisions
- bpftime host-side uprobes for low-overhead telemetry and shadow mode
- no CUDA/PTX rewriting in the first version

That path preserves NCCL's supported extension surface, keeps the hot-path overhead inside budget, and avoids taking ownership of NCCL's internal CUDA kernel pipeline too early.
