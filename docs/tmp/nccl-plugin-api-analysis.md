# NCCL Plugin API Analysis: Tuner and Net Plugins

Date: March 9, 2026

## Executive Summary

The NCCL tuner plugin API is real, usable, and more capable than many papers assume, but it is still a narrow control surface. In current upstream NCCL, the active tuner ABI is `v5`: it gives a plugin communicator context, communicator size, NVLink-domain metadata, and mutable tuning constants at `init`, then a per-collective `getCollInfo` hook that can rewrite NCCL's cost table and override channel count. NCCL still loads older `v2`/`v3`/`v4` plugins through compatibility shims.

The net plugin API is much broader. A net plugin can enumerate NICs, report topology-relevant properties, request QoS, support DMA-BUF and GPUDirect-style registration, expose virtual NIC fusion, provide device-side networking handles, and optionally implement collective offload through `collNet`. In other words: the tuner plugin chooses among NCCL's existing communication plans; the net plugin determines what transport mechanisms and offloads are even available.

Public implementations of the stock NCCL tuner ABI are sparse. The strongest concrete example is AWS's `aws-ofi-nccl` tuner, which exports `ncclTunerPlugin_v1/v2/v3` and uses either region-based lookup or a simple cost model. `MSCCL` and `MSCCL++` are important prior art on programmable collectives, but they are not stock `NCCL_TUNER_PLUGIN` implementations: they fork or interpose NCCL. `AutoCCL` is the closest research system to "policy-driven tuning," but it also extends the ABI beyond upstream NCCL.

My assessment is that there is still room for a strong systems paper, but not if the claim is merely "we wrote a better NCCL tuner plugin." That would not be novel enough. The novelty window is narrower and stronger: a safe, verifiable, hot-swappable, cross-layer policy plane over NCCL's plugin stack, ideally with eBPF or bpftime-style isolation, runtime feedback, and coordination that the current tuner ABI does not expose.

## Scope and Fetch Notes

The two raw GitHub URLs in the task statement appear stale as of March 9, 2026:

- `https://raw.githubusercontent.com/NVIDIA/nccl/master/ext-tuner/example/nccl/tuner.h`
- `https://raw.githubusercontent.com/NVIDIA/nccl/master/ext-net/README.md`

Direct fetches returned `404` on March 9, 2026. The content has moved in current upstream NCCL. I verified the current upstream tree by cloning `https://github.com/NVIDIA/nccl` at commit:

`361915904b456d397e6e1578f8f65ea1a45bdd28`

Equivalent current paths:

- `plugins/tuner/example/nccl/tuner.h`
- `plugins/net/README.md`

I also cross-checked older ABI variants from:

- `src/include/plugin/tuner/tuner_v2.h`
- `src/include/plugin/tuner/tuner_v3.h`
- `src/include/plugin/tuner/tuner_v4.h`
- `src/include/plugin/tuner/tuner_v5.h`

and from AWS OFI's vendored NCCL headers for `v1`.

## 1. The Current Tuner Plugin API

### 1.1 Current upstream ABI: `ncclTunerPlugin_v5`

Current upstream exports `NCCL_TUNER_PLUGIN_SYMBOL "ncclTunerPlugin_v5"` from `plugins/tuner/example/nccl/tuner.h`.

Key enums and dimensions in the current header:

```c
#define NCCL_NUM_ALGORITHMS 7
#define NCCL_ALGO_TREE 0
#define NCCL_ALGO_RING 1
#define NCCL_ALGO_COLLNET_DIRECT 2
#define NCCL_ALGO_COLLNET_CHAIN 3
#define NCCL_ALGO_NVLS 4
#define NCCL_ALGO_NVLS_TREE 5
#define NCCL_ALGO_PAT 6

#define NCCL_NUM_PROTOCOLS 3
#define NCCL_PROTO_LL 0
#define NCCL_PROTO_LL128 1
#define NCCL_PROTO_SIMPLE 2
```

The exact `v5` tuner struct is:

```c
typedef struct {
  const char* name;
  ncclResult_t (*init)(void** ctx, uint64_t commId, size_t nRanks, size_t nNodes,
                       ncclDebugLogger_t logFunction,
                       ncclNvlDomainInfo_v5_t* nvlDomainInfo,
                       ncclTunerConstants_v5_t* constants);
  ncclResult_t (*getCollInfo)(void* context, ncclFunc_t collType, size_t nBytes,
                              int numPipeOps, float** collCostTable,
                              int numAlgo, int numProto,
                              int regBuff, int* nChannels);
  ncclResult_t (*finalize)(void* context);
} ncclTuner_v5_t;
```

The new `v5` inputs relative to older ABIs are important:

- `commId`: lets the plugin distinguish communicators.
- `nvlDomainInfo`: exposes NVLink-domain structure.
- `constants`: a mutable NCCL tuning-constant block; this is an input/output parameter.
- `regBuff`: tells `getCollInfo` whether user buffers are registerable in this call.

### 1.2 `v5` constants and metadata exposed at `init`

The plugin can inspect or modify:

```c
typedef struct {
  int nNvlDomains;
  int minRanksPerNvlDomain;
  int maxRanksPerNvlDomain;
} ncclNvlDomainInfo_v5_t;

typedef struct {
  double baseLatencies[NCCL_NUM_ALGORITHMS][NCCL_NUM_PROTOCOLS];
  double hwLatencies[NCCL_NUM_HW_LINKS][NCCL_NUM_ALGORITHMS][NCCL_NUM_PROTOCOLS];
  double llMaxBws[NCCL_NUM_COMPCAPS][NCCL_NUM_TUNING_SCALES];
  double perChMaxRingLL128Bws[NCCL_NUM_COMPCAPS][NCCL_NUM_TUNING_SCALES];
  double perChMaxTreeLL128Bws[NCCL_NUM_COMPCAPS][NCCL_NUM_TUNING_SCALES];
  double perChMaxTreeBws[NCCL_NUM_COMPCAPS][NCCL_NUM_TUNING_SCALES];
  double perChMaxNVLSTreeBws[NCCL_NUM_COMPCAPS][NCCL_NUM_TUNING_SCALES];
} ncclTunerConstants_v5_t;
```

This is more powerful than older public descriptions of the tuner API, because a plugin can now bias NCCL's own internal tuning model before per-collective selection runs.

### 1.3 Where NCCL calls the tuner

The hook points in current NCCL core are straightforward:

1. During communicator initialization:

```c
NCCLCHECKGOTO(ncclTopoInitTunerConstants(comm), ret, fail);
NCCLCHECKGOTO(ncclTunerPluginLoad(comm), ret, fail);
if (comm->tuner) {
  NCCLCHECK(comm->tuner->init(&comm->tunerContext, comm->commHash,
                              comm->nRanks, comm->nNodes, ncclDebugLog,
                              &comm->nvlDomainInfo, &comm->tunerConstants));
}
NCCLCHECKGOTO(ncclTopoTuneModel(comm, ...), ret, fail);
```

This ordering matters. `init` runs after NCCL initializes default tuning constants, but before NCCL finalizes the topology-based tuning model. So `v5` plugins can perturb internal NCCL defaults, not just individual collectives.

2. During collective planning in `ncclGetAlgoInfo`:

```c
NCCLCHECK(updateCollCostTable(comm, info, nBytes, collNetSupport,
                              nvlsSupport, numPipeOps, (float **)collCostTable));
...
NCCLCHECK(comm->tuner->getCollInfo(
      comm->tunerContext, info->func, nBytes,
      numPipeOps, (float **)collCostTable,
      NCCL_NUM_ALGORITHMS, NCCL_NUM_PROTOCOLS,
      regBuff, &nMaxChannels));
NCCLCHECK(topoGetAlgoInfo(comm, info, nBytes, (float **)collCostTable, simInfo));
```

This is the main decision hook. The plugin sees NCCL's computed cost table and may modify it before NCCL chooses the final plan.

3. During communicator cleanup:

```c
if (comm->tuner != NULL) {
  NCCLCHECK(comm->tuner->finalize(comm->tunerContext));
  NCCLCHECK(ncclTunerPluginUnload(comm));
}
```

### 1.4 Version evolution

Current NCCL still loads `v5`, then falls back to `v4`, `v3`, and `v2` in that order.

The compatibility story is:

| Version | Signature style | What it can override |
| --- | --- | --- |
| `v2` | direct outputs | `algorithm`, `protocol`, `nChannels` |
| `v3` | cost-table rewrite | cost-table entries, `nChannels` |
| `v4` | cost-table rewrite + `regBuff` | cost-table entries, `nChannels`, can branch on registered-buffer capability |
| `v5` | `v4` + communicator ID + NVL metadata + mutable constants + `finalize` | same as `v4`, plus pre-tuning of NCCL internal constants |

Exact signatures:

```c
// v2
ncclResult_t (*getCollInfo)(void* context, ncclFunc_t collType, size_t nBytes,
                            int collNetSupport, int nvlsSupport, int numPipeOps,
                            int* algorithm, int* protocol, int* nChannels);

// v3
ncclResult_t (*getCollInfo)(void* context, ncclFunc_t collType, size_t nBytes,
                            int numPipeOps, float** collCostTable,
                            int numAlgo, int numProto, int* nChannels);

// v4
ncclResult_t (*getCollInfo)(void* context, ncclFunc_t collType, size_t nBytes,
                            int numPipeOps, float** collCostTable,
                            int numAlgo, int numProto,
                            int regBuff, int* nChannels);
```

Compatibility details from NCCL core:

- `v2` is wrapped into the new interface by inferring `collNetSupport` and `nvlsSupport` from the cost table, then setting the chosen table entry to `0.0`.
- `v3` is wrapped by ignoring `regBuff`.
- `v4` is wrapped directly, with `destroy` adapted to `finalize`.

Notably, current upstream NCCL does not appear to load `v1` anymore. `v1` survives in downstream implementations such as AWS OFI for compatibility with older NCCL releases.

## 2. What the Tuner Can and Cannot Override

### 2.1 What can be overridden

At `init`:

- Per-communicator plugin state.
- Policies keyed by `commId`, `nRanks`, `nNodes`.
- Policies keyed by NVLink-domain structure.
- NCCL's internal tuning constants in `v5`.

At `getCollInfo`:

- Relative preference among existing algorithm/protocol pairs by rewriting `collCostTable`.
- Hard preference by setting a legal entry to `0.0`.
- Soft disable by setting an entry to `NCCL_ALGO_PROTO_IGNORE`.
- Channel count through `nChannels`.
- Policy branches on `collType`, `nBytes`, `numPipeOps`, `regBuff`.

The reference example plugin explicitly demonstrates all of these except disable. It:

- reads CSV rules over collective type, size range, node/rank counts, `numPipeOps`, and `regBuff`,
- sets one table entry to `0.0`,
- optionally sets `nChannels`,
- and in `v5` `init`, mutates bandwidth/latency constants.

### 2.2 What the tuner cannot override

This is the key gap analysis for research novelty.

The stock NCCL tuner ABI cannot directly:

- install a new collective algorithm or schedule,
- emit custom kernels,
- alter ring/tree topology construction,
- choose actual peer/channel mappings,
- choose or fuse NICs,
- change transport registration logic,
- alter send/recv request handling,
- receive transport completion callbacks,
- observe queue depths, loss, retries, or per-NIC bandwidth directly,
- expose per-collective chunk size as an output,
- expose thread count as an output,
- expose P2P level, copy-engine choice, or proxy behavior,
- coordinate policy decisions across ranks through an API-level control plane,
- trigger mid-run replanning from profiler feedback.

There is also an important mismatch between public descriptions and the actual ABI:

- NVIDIA's July 22, 2025 NCCL tuning blog says NCCL internally chooses CTA count, protocol, algorithm, and chunk sizing, and that tuner plugins have a minimal interface focused on protocol, algorithm, and CTA override.
- The official public ABI exposed in `v2`-`v5` only gives explicit outputs for `algorithm`/`protocol` or cost-table rewrites plus `nChannels`.

So even if NCCL internally reasons about CTAs and chunking, the stock tuner ABI does not give a plugin a first-class `nThreads`, `chunkSize`, or `CTA` output. `nChannels` is the only explicit resource-control output in the stock interface.

This is exactly why systems like AutoCCL fork the interface and add their own hooks.

## 3. Net Plugin Capabilities

### 3.1 Current net ABI: `ncclNet_v11_t`

Current upstream net plugin support is much broader than the tuner API.

The main `v11` struct is:

```c
typedef struct {
  const char* name;
  ncclResult_t (*init)(void** ctx, uint64_t commId,
                       ncclNetCommConfig_v11_t* config,
                       ncclDebugLogger_t logFunction,
                       ncclProfilerCallback_t profFunction);
  ncclResult_t (*devices)(int* ndev);
  ncclResult_t (*getProperties)(int dev, ncclNetProperties_v11_t* props);
  ncclResult_t (*listen)(void* ctx, int dev, void* handle, void** listenComm);
  ncclResult_t (*connect)(void* ctx, int dev, void* handle,
                          void** sendComm, ncclNetDeviceHandle_v11_t** sendDevComm);
  ncclResult_t (*accept)(void* listenComm, void** recvComm,
                         ncclNetDeviceHandle_v11_t** recvDevComm);
  ...
  ncclResult_t (*makeVDevice)(int* d, ncclNetVDeviceProps_v11_t* props);
  ncclResult_t (*finalize)(void* ctx);
  ncclResult_t (*setNetAttr)(void* ctx, ncclNetAttr_v11_t* netAttr);
} ncclNet_v11_t;
```

### 3.2 Device selection and topology inputs

Per-device properties include:

```c
typedef struct {
  char* name;
  char* pciPath;
  uint64_t guid;
  int ptrSupport;
  int regIsGlobal;
  int forceFlush;
  int speed;
  int port;
  float latency;
  int maxComms;
  int maxRecvs;
  ncclNetDeviceType netDeviceType;
  int netDeviceVersion;
  ncclNetVDeviceProps_v11_t vProps;
  size_t maxP2pBytes;
  size_t maxCollBytes;
  int maxMultiRequestSize;
} ncclNetProperties_v11_t;
```

This means a net plugin can influence NCCL's network-device choice and transport behavior through:

- NIC identity and topology: `pciPath`, `guid`, `port`
- performance hints: `speed`, `latency`
- transport capability: `ptrSupport`, `regIsGlobal`, `forceFlush`
- scalability bounds: `maxComms`, `maxRecvs`, `maxP2pBytes`, `maxCollBytes`

There is also per-communicator QoS input:

```c
typedef struct {
  int trafficClass;
} ncclNetCommConfig_v11_t;
```

The README explicitly says `trafficClass` can be used by the plugin as a QoS selector.

### 3.3 Virtual NICs / NIC fusion

Virtual NIC support is explicit:

```c
typedef struct {
  int ndevs;
  int devs[NCCL_NET_MAX_DEVS_PER_NIC];
} ncclNetVDeviceProps_v11_t;
...
ncclResult_t (*makeVDevice)(int* d, ncclNetVDeviceProps_v11_t* props);
```

The README says NCCL may call `makeVDevice` to create a virtual NIC from multiple physical devices, currently used for NIC fusion. That is a net-plugin capability, not a tuner capability.

### 3.4 Device offload

Device-side networking support is also explicit:

```c
typedef enum {NCCL_NET_DEVICE_HOST=0, NCCL_NET_DEVICE_UNPACK=1} ncclNetDeviceType;

typedef struct {
  ncclNetDeviceType netDeviceType;
  int netDeviceVersion;
  void* handle;
  size_t size;
  int needsProxyProgress;
} ncclNetDeviceHandle_v11_t;
```

Offload-related hook points:

- `connect(..., ncclNetDeviceHandle_v11_t** sendDevComm)`
- `accept(..., ncclNetDeviceHandle_v11_t** recvDevComm)`
- `getDeviceMr(...)`
- `irecvConsumed(...)`

The README says these pointers are supplied when NCCL is requesting device offload for the connection.

### 3.5 Collective offload: `collNet`

The optional `collNet` interface is separate from point-to-point net transport and can implement:

- `iallreduce`
- `iallgather`
- `ireducescatter`

through `ncclCollNet_v11_t`.

This is how network plugins can expose in-network collective acceleration, such as SHARP-style offload.

### 3.6 What the net plugin can do that the tuner cannot

The net plugin can:

- control NIC enumeration and identity,
- present or hide transport capabilities,
- request QoS treatment,
- fuse NICs into vNICs,
- expose device offload,
- expose collective offload,
- change registration and flush behavior,
- publish telemetry through profiler coupling in some vendor stacks.

The tuner cannot do any of those directly.

## 4. Existing Implementations

### 4.1 Upstream reference implementations

Upstream NCCL now ships two reference tuner plugins:

- `plugins/tuner/basic`
- `plugins/tuner/example`

The basic plugin is intentionally trivial: it forces `RING/SIMPLE` when legal and sets `nChannels = 1`.

The example plugin is more representative:

- CSV-based rules over `collType`, byte range, `nNodes`, `nRanks`, `numPipeOps`, and `regBuff`
- `v5` `init` that modifies NCCL tuning constants
- `getCollInfo` that rewrites a single table entry to `0.0` and optionally overrides channels

These are useful baselines, but not serious adaptive systems.

### 4.2 AWS OFI NCCL tuner

This is the most substantial public implementation of the stock NCCL tuner ABI that I found.

Exports:

```c
extern const ncclTuner_v3_t ncclTunerPlugin_v3;
extern const ncclTuner_v2_t ncclTunerPlugin_v2;
extern const ncclTuner_v1_t ncclTunerPlugin_v1;
```

It implements two policy families:

1. Region-based tuner
2. Model-based tuner

Selection logic:

- detects AWS platform type,
- chooses among `p5.48xlarge`, `p5e.48xlarge`, `p5en.48xlarge`, `p6-b200.48xlarge`, `p6-b300.48xlarge`,
- decides whether to use region or model policy,
- otherwise falls back to NCCL's internal tuner.

Important behaviors:

- If the platform is not AWS, it does not engage.
- If `OFI_NCCL_FORCE_NUM_RAILS` is set, it falls back to internal NCCL to avoid heterogeneous-rail issues.
- For `v1` and `v2`, if `NCCL_ALGO` or `NCCL_PROTO` is set, it refuses to load and falls back.
- Region policy falls back for `<= 2` nodes.
- Region `v2` explicitly skips PAT because PAT is not supported in `v2`.
- Region `v3` can override `nChannels` on some P6 cases.

The region tuner is a hand-built lookup over polygons in `(log2(nBytes), log2(nRanks))` space. The model tuner computes Hockney-style cost estimates.

Limitations:

- strongly platform-specific,
- mostly open-loop,
- no cross-rank control plane,
- no transport telemetry input,
- model path effectively only models `AllReduce`,
- no public evidence of dynamic online adaptation.

One subtle implementation note: the model code only defines `nccl_ofi_tuner_compute_cost()` for `ncclFuncAllReduce`; unsupported collectives return `-1`. So the model path is much narrower than a generic "NCCL tuner" label suggests.

### 4.3 AutoCCL

AutoCCL is the closest published academic system to "policy-driven NCCL tuning," but it is not a stock upstream tuner plugin.

Its README uses:

- `NCCL_TUNER_PLUGIN=.../libnccl-plugin.so`

but its forked `nccl_tuner.h` adds custom hooks such as:

- `getCandidate`
- `startProfiling`
- `stopProfiling`
- `isNewWorkload`

and outputs beyond the stock ABI:

- `isCopyEngineNotSmCopy`
- `p2pLevel`
- `nThreads`
- `chunkSize`
- `iteration`
- `lastIterEffectiveChunksize`
- `native`

That is precisely the kind of control surface the upstream ABI does not expose.

Interpretation:

- AutoCCL is strong prior art for online and policy-driven collective tuning.
- It is not prior art for "the stock NCCL tuner API already supports all of this."
- It is evidence that researchers who want richer control quickly outgrow the upstream ABI.

### 4.4 MSCCL

MSCCL is strong prior art on programmable collectives, but not on the stock tuner plugin ABI.

MSCCL:

- forks the runtime,
- introduces `NCCL_ALGO_MSCCL`,
- loads XML/configured algorithms,
- selects them in internal tuning,
- routes execution to interpreter/specialized kernels.

So MSCCL is a programmable collective runtime, but not a `ncclTunerPlugin_vX` implementation.

This distinction matters for novelty claims:

- "first programmable collective runtime" is not tenable.
- "first safe policy plane layered over stock NCCL plugin hooks" is still possibly tenable.

### 4.5 MSCCL++

MSCCL++ is also important prior art, but it works through NCCL API interposition, not the stock tuner ABI.

It uses `LD_PRELOAD=libmscclpp_nccl.so`, intercepts NCCL APIs, selects a custom algorithm, and falls back to real NCCL when needed.

Again:

- strong prior art for programmable substitution and custom execution plans,
- not prior art that the stock tuner ABI is already sufficient.

### 4.6 eBPF-based NCCL tuner implementations

I did not find a public implementation of NCCL tuning via eBPF, bpftime, or a similar verified policy runtime.

That is an inference from:

- GitHub/repo searches for `NCCL`, `ncclTunerPlugin`, and `eBPF`
- web searches for `NCCL_TUNER_PLUGIN eBPF`
- searches for GPU/eBPF projects tied to collectives

This is not proof that none exists, but I found no public system comparable to AWS OFI or AutoCCL in this space.

## 5. Related Academic and Systems Work

### 5.1 Closest work on policy-driven collective tuning

#### AutoCCL (NSDI 2025)

Closest match to the spirit of "policy-driven collective communication" in the NCCL ecosystem.

Why it matters:

- real system,
- real tuning feedback,
- workload detection,
- richer knobs than upstream NCCL exposes.

Why it does not close the novelty gap completely:

- relies on a forked ABI,
- not safe or verifier-backed in the eBPF sense,
- not a stock NCCL plugin deployment model.

#### OCCL

OCCL is strong prior art on runtime collective scheduling and preemption. It is relevant because it shows that "runtime control of collectives" is already a serious research area, even if it is not phrased in NCCL-plugin terms.

### 5.2 Programmable collective runtimes and algorithm synthesis

These works significantly narrow any broad novelty claim:

- MSCCL / GC3 / MSCCLang
- MSCCL++
- TACCL
- TE-CCL
- ForestColl
- SyCCL
- HiCCL

Together, they establish prior art on:

- topology-aware collective synthesis,
- custom execution plans,
- programmable collective runtimes,
- compiler-driven collective specialization.

So a paper should avoid claims like:

- first programmable collective runtime,
- first topology-aware collective synthesis system,
- first customizable GPU collective system.

### 5.3 eBPF and GPU work

The closest eBPF/GPU works I found are:

- `eGPU`: eBPF/PTX-style instrumentation for live GPU kernels
- `eACGM`: eBPF-based full-stack observability for GPU/ML systems
- `Host-Side Telemetry for Performance Diagnosis in Cloud and HPC GPU Infrastructure`
- `gpu_ext`: eBPF-based policy runtime below the NCCL layer
- `bpftime` and `EIM`: verified or capability-scoped in-process eBPF execution infrastructure

These are relevant because they suggest ingredients for a safe policy engine or observability substrate.

But none of them, from what I found, implement policy-driven NCCL collective control.

### 5.4 Network-side programmable/offloaded collectives

Important adjacent systems:

- SHARP / NCCL-RDMA-SHARP plugin
- SwitchML
- Spectrum-X NCCL plugin

These show that:

- collective behavior is already being pushed into the network,
- vendor plugins already expose resilience, load balancing, topology awareness, and telemetry,
- a "network-aware NCCL plugin" is not itself novel.

## 6. Gap Analysis: What Current Plugin APIs Still Cannot Do

This section is the practical answer to "where is the research room?"

### 6.1 Gaps in the tuner API

The current tuner API still lacks first-class support for:

- runtime feedback hooks from actual completed operations,
- safe mid-run policy reoptimization,
- transport-side telemetry as direct inputs,
- cross-rank coordination and consistency primitives,
- custom schedules or algorithm injection,
- explicit thread/CTA outputs,
- explicit chunk-size outputs,
- explicit proxy, copy-engine, or P2P-level outputs,
- per-collective state beyond what the plugin maintains itself,
- a verifier or capability model for third-party policy code.

### 6.2 Gaps across tuner and net together

Even the combined plugin stack still has a separation-of-concerns gap:

- the tuner decides among NCCL plans,
- the net plugin defines transport/offload capabilities,
- the profiler observes,
- but there is no standard cross-plugin policy bus.

NCCL can load tuner symbols from the net plugin library, and recent NCCL versions allow shared contexts across plugin types in one `.so`, but there is still no standard API for:

- policy exchange between tuner and net,
- policy sharing with profiler signals,
- dynamic transport-aware retuning after communicator init,
- safe third-party policy deployment.

### 6.3 Why AutoCCL had to fork

AutoCCL's forked ABI is the clearest empirical proof of the stock API boundary.

It added exactly the knobs that a research system wants:

- workload detection,
- profiling callbacks,
- thread count,
- chunk size,
- copy-engine mode,
- P2P level,
- iteration metadata.

That is a direct sign that the stock NCCL tuner API is not rich enough for ambitious adaptive runtimes.

## 7. Assessment for a Top-Venue Systems Paper

### 7.1 What is not novel enough

The following would probably not be enough for a top systems venue by themselves:

- a static NCCL tuner plugin with better heuristics,
- a vendor-specific retuning recipe,
- a cost-table override system without runtime feedback,
- a paper framed as "programmable collectives" without acknowledging MSCCL-family prior art,
- a paper framed as "network-aware NCCL" without acknowledging existing net plugins and SHARP/Spectrum-X.

### 7.2 What still looks novel enough

A paper could still be strong if it is framed as:

- a safe, verifier-backed policy plane over NCCL's existing extension stack,
- hot-swappable policy deployment without rebuilding NCCL or forking it,
- cross-layer control that fuses tuner, net, and profiler signals,
- eBPF or bpftime-style capability scoping for untrusted policy code,
- dynamic policy adaptation driven by online telemetry,
- deterministic or coordinated rank-consistent policy application,
- low overhead and strong fallback semantics.

That combination is the novelty window I do not see clearly occupied by existing work.

### 7.3 What the paper would need to show

For a top venue, I think the paper would need:

1. A clearly stronger control surface than stock `getCollInfo`, without requiring an NCCL fork.
2. A safety story: verifier, capability model, or otherwise compelling isolation.
3. A systems story: dynamic adaptation, not just static rules.
4. A cross-layer story: using signals unavailable to today's tuner ABI alone.
5. Strong baselines against:
   - stock NCCL
   - upstream example/basic tuner
   - AWS OFI tuner where applicable
   - AutoCCL-style adaptive tuning if reproducible
   - MSCCL or MSCCL++ where claims overlap
6. Low overhead under no-op or disabled-policy mode.
7. Evidence that the approach generalizes beyond one cloud SKU or one collective.

### 7.4 Claim discipline

The strongest claim I would make is something like:

> a safe, dynamic, cross-layer policy plane for NCCL built over existing plugin hooks, with richer runtime feedback and lower deployment friction than fork-based systems.

The claims I would avoid:

- first programmable collective runtime
- first topology-aware collective tuning system
- first resilient NCCL plugin
- first observable NCCL stack

## Bottom Line

There is enough novelty for a strong systems paper only if the work goes beyond "better `getCollInfo` heuristics."

The official NCCL tuner API is powerful enough to be useful, but not powerful enough to subsume systems like AutoCCL or MSCCL. That is the opportunity:

- the stock ABI is intentionally narrow,
- public stock-ABI implementations are few,
- no public eBPF-based NCCL policy runtime was found,
- and the combined tuner/net/profiler stack still lacks a standard safe policy plane.

That is a publishable gap if the system delivers safety, dynamic feedback, and cross-layer control without forking NCCL.

## Sources

### Upstream NCCL source

- NVIDIA NCCL upstream repo: https://github.com/NVIDIA/nccl
- Current upstream commit used for code inspection: `361915904b456d397e6e1578f8f65ea1a45bdd28`
- Current tuner example header path: https://github.com/NVIDIA/nccl/blob/361915904b456d397e6e1578f8f65ea1a45bdd28/plugins/tuner/example/nccl/tuner.h
- Current net README path: https://github.com/NVIDIA/nccl/blob/361915904b456d397e6e1578f8f65ea1a45bdd28/plugins/net/README.md

### NCCL docs and blogs

- NVIDIA NCCL tuning blog, July 22, 2025: https://developer.nvidia.com/blog/understanding-nccl-tuning-to-accelerate-gpu-to-gpu-communication/
- NCCL environment variable docs (`NCCL_TUNER_PLUGIN` load order): https://docs.nvidia.com/deeplearning/nccl/user-guide/docs/env.html
- NCCL 2.29.2 release notes: https://docs.nvidia.com/deeplearning/nccl/release-notes/rel_2-29-2.html

### Net plugin examples and vendor plugin docs

- Spectrum-X NCCL plugin docs: https://docs.nvidia.com/networking/display/hpcxv226/Spectrum-X-NCCL-Plugin
- NCCL-RDMA-SHARP plugins docs: https://docs.nvidia.com/networking/display/hpcvx225/NCCL-RDMA-SHARP-Plugins

### Existing implementations

- AWS OFI NCCL: https://github.com/aws/aws-ofi-nccl
- MSCCL: https://github.com/microsoft/msccl
- MSCCL++: https://github.com/microsoft/mscclpp
- AutoCCL: https://github.com/gbxu/autoccl

### Related research

- AutoCCL (NSDI 2025): https://www.usenix.org/conference/nsdi25/presentation/xu-guanbin
- MSCCL / GC3 paper: https://arxiv.org/abs/2201.11840
- MSCCL++ paper: https://arxiv.org/abs/2504.09014
- TACCL (NSDI 2023): https://www.usenix.org/system/files/nsdi23-shah.pdf
- ForestColl: https://arxiv.org/abs/2402.06787
- SyCCL: https://ennanzhai.github.io/pub/sigcomm25-syccl.pdf
- HiCCL: https://arxiv.org/abs/2408.05962
- OCCL: https://arxiv.org/abs/2303.06324
- eGPU: https://hcds-workshop.github.io/edition/2025/resources/hcds25-12.pdf
- eACGM: https://arxiv.org/abs/2506.02007
- Host-side telemetry for GPU infrastructure: https://arxiv.org/abs/2411.11895
- bpftime: https://arxiv.org/abs/2311.07923
- EIM capability model: https://eunomia.dev/bpftime/EIM/spec/
- gpu_ext: https://arxiv.org/abs/2512.12615
