# NCCL Plugin Exploration

## Scope

I read the plugin examples under `nccl/plugins/` and the internal loader/ABI code under `nccl/src/plugin/` and `nccl/src/include/plugin/`, then traced the public elastic and one-sided APIs through NCCL core.

Files explicitly reviewed:

- `nccl/plugins/tuner/example/plugin.c`
- `nccl/plugins/tuner/example/nccl/tuner.h`
- `nccl/plugins/tuner/basic/plugin.c`
- `nccl/plugins/tuner/basic/nccl/tuner.h`
- `nccl/plugins/net/example/plugin.c`
- `nccl/plugins/env/example/plugin.c`
- `nccl/plugins/profiler/example/plugin.cc`
- `nccl/plugins/profiler/example/plugin.h`
- `nccl/plugins/profiler/inspector/inspector_plugin.cc`
- `nccl/plugins/mixed/example/plugin.c`
- `nccl/src/plugin/plugin_open.cc`
- `nccl/src/plugin/tuner.cc`
- `nccl/src/plugin/tuner/tuner_v2.cc`
- `nccl/src/plugin/tuner/tuner_v3.cc`
- `nccl/src/plugin/tuner/tuner_v4.cc`
- `nccl/src/plugin/tuner/tuner_v5.cc`
- `nccl/src/include/plugin/plugin.h`
- `nccl/src/include/plugin/tuner/tuner_v2.h`
- `nccl/src/include/plugin/tuner/tuner_v3.h`
- `nccl/src/include/plugin/tuner/tuner_v4.h`
- `nccl/src/include/plugin/tuner/tuner_v5.h`

Additional internal files that were necessary to connect the dots:

- `nccl/src/include/plugin/nccl_tuner.h`
- `nccl/src/include/plugin/nccl_net.h`
- `nccl/src/include/plugin/net/net_v11.h`
- `nccl/src/include/plugin/nccl_env.h`
- `nccl/src/include/plugin/env/env_v1.h`
- `nccl/src/include/plugin/nccl_profiler.h`
- `nccl/src/include/plugin/profiler/profiler_v1.h` through `profiler_v6.h`
- `nccl/src/plugin/net.cc`
- `nccl/src/plugin/gin.cc`
- `nccl/src/plugin/env.cc`
- `nccl/src/plugin/profiler.cc`
- `nccl/src/plugin/net/net_v11.cc`
- `nccl/src/plugin/gin/gin_v11.cc`
- `nccl/src/plugin/gin/gin_v12.cc`
- `nccl/src/plugin/profiler/profiler_v1.cc` through `profiler_v6.cc`
- `nccl/src/init.cc`
- `nccl/src/enqueue.cc`
- `nccl/src/collectives.cc`
- `nccl/src/nccl.h.in`
- `nccl/src/misc/param.cc`
- `nccl/src/include/plugin/nccl_gin.h`
- `nccl/src/include/plugin/gin/gin_v11.h`
- `nccl/src/include/plugin/gin/gin_v12.h`
- `nccl/src/rma/rma.cc`
- `nccl/src/rma/rma_ce.cc`
- `nccl/src/rma/rma_proxy.cc`
- `nccl/src/bootstrap.cc`

## Executive Summary

NCCL currently exposes five plugin families in core loader code: `NET`, `GIN`, `TUNER`, `PROFILER`, and `ENV`. Of those, the most usable hooks for an eBPF policy plane are:

- `TUNER` for control decisions on algorithm/protocol/channel selection.
- `PROFILER` for event export and feedback.
- `ENV` for bootstrap/config injection.

`NET` and `GIN` are much more invasive. They are transport implementations, not lightweight policy hooks. They are appropriate only if the policy plane must directly own transport behavior, memory registration semantics, or one-sided host-RMA progress.

The one-sided host API (`ncclPutSignal`, `ncclSignal`, `ncclWaitSignal`) is real NCCL core functionality. It is not implemented through the tuner/profiler path; it is built on symmetric windows plus GIN/RMA support.

Elastic APIs (`ncclCommRevoke`, `ncclCommShrink`, `ncclCommGrow`) are also real public APIs. They create/transition communicators in NCCL core, but they do not currently introduce a dedicated plugin callback surface.

## Plugin ABI Surfaces

### Tuner

Current alias:

- `nccl/src/include/plugin/nccl_tuner.h` aliases `ncclTuner_t` to `ncclTuner_v5_t`.
- Current symbol macro: `NCCL_TUNER_PLUGIN_SYMBOL` = `"ncclTunerPlugin_v5"`.

Supported versions are v2 through v5. NCCL wraps older versions in `src/plugin/tuner/tuner_v2.cc` through `tuner_v5.cc` so the rest of the code can call a v5-shaped interface.

#### Tuner v2

```c
typedef struct {
  const char* name;
  ncclResult_t (*init)(size_t nRanks, size_t nNodes,
                       ncclDebugLogger_t logFunction, void **context);
  ncclResult_t (*getCollInfo)(void* context, ncclFunc_t collType,
                              size_t nBytes, int collNetSupport,
                              int nvlsSupport, int numPipeOps,
                              int* algorithm, int* protocol,
                              int* nChannels);
  ncclResult_t (*destroy)(void* context);
} ncclTuner_v2_t;
```

Notes:

- v2 returns explicit `algorithm` and `protocol`.
- NCCL's wrapper translates that into cost-table form by marking the selected algo/proto as preferred and leaving fallback behavior to core.

#### Tuner v3

```c
typedef struct {
  const char* name;
  ncclResult_t (*init)(size_t nRanks, size_t nNodes,
                       ncclDebugLogger_t logFunction, void **context);
  ncclResult_t (*getCollInfo)(void* context, ncclFunc_t collType,
                              size_t nBytes, int numPipeOps,
                              float** collCostTable,
                              int numAlgo, int numProto,
                              int* nChannels);
  ncclResult_t (*destroy)(void* context);
} ncclTuner_v3_t;
```

Notes:

- v3 moves to cost-table editing.
- No `regBuff` parameter yet.

#### Tuner v4

```c
typedef struct {
  const char* name;
  ncclResult_t (*init)(size_t nRanks, size_t nNodes,
                       ncclDebugLogger_t logFunction, void **context);
  ncclResult_t (*getCollInfo)(void* context, ncclFunc_t collType,
                              size_t nBytes, int numPipeOps,
                              float** collCostTable,
                              int numAlgo, int numProto,
                              int regBuff, int* nChannels);
  ncclResult_t (*destroy)(void* context);
} ncclTuner_v4_t;
```

Notes:

- Adds `regBuff`, allowing the tuner to see whether user buffers are registered.

#### Tuner v5

```c
typedef struct {
  const char* name;
  ncclResult_t (*init)(void** ctx, uint64_t commId,
                       size_t nRanks, size_t nNodes,
                       ncclDebugLogger_t logFunction,
                       ncclNvlDomainInfo_v5_t* nvlDomainInfo,
                       ncclTunerConstants_v5_t* constants);
  ncclResult_t (*getCollInfo)(void* context, ncclFunc_t collType,
                              size_t nBytes, int numPipeOps,
                              float** collCostTable,
                              int numAlgo, int numProto,
                              int regBuff, int* nChannels);
  ncclResult_t (*finalize)(void* context);
} ncclTuner_v5_t;
```

Notes:

- Adds `commId`, NVLink domain info, and mutable tuning constants at init time.
- Renames `destroy` to `finalize`.
- NCCL calls `ncclTopoInitTunerConstants()`, then `ncclTunerPluginLoad()`, then `comm->tuner->init(...)` during communicator init.
- NCCL calls `comm->tuner->getCollInfo(...)` inside collective selection in `enqueue.cc`.
- NCCL calls `comm->tuner->finalize(...)` during communicator cleanup.

Important compatibility risk:

- The example plugin vendors its own `plugins/tuner/example/nccl/tuner.h`.
- That vendored header appears slightly stale relative to the in-tree `src/include/plugin/tuner/tuner_v5.h` and demonstrates that out-of-tree tuner plugins are version-coupled to the exact NCCL release they were built against.

### Net / CollNet

Current alias:

- `nccl/src/include/plugin/nccl_net.h` aliases `ncclNet_t` to `ncclNet_v11_t`.
- `ncclCollNet_t` aliases to `ncclCollNet_v11_t`.
- Current symbols are `ncclNetPlugin_v11` and `ncclCollNetPlugin_v11`.
- Loader compatibility extends back to v6.

#### NET v11

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
                          void** sendComm,
                          ncclNetDeviceHandle_v11_t** sendDevComm);
  ncclResult_t (*accept)(void* listenComm, void** recvComm,
                         ncclNetDeviceHandle_v11_t** recvDevComm);
  ncclResult_t (*regMr)(void* comm, void* data, size_t size,
                        int type, void** mhandle);
  ncclResult_t (*regMrDmaBuf)(void* comm, void* data, size_t size,
                              int type, uint64_t offset, int fd,
                              void** mhandle);
  ncclResult_t (*deregMr)(void* comm, void* mhandle);
  ncclResult_t (*isend)(void* sendComm, void* data, size_t size,
                        int tag, void* mhandle, void* phandle,
                        void** request);
  ncclResult_t (*irecv)(void* recvComm, int n, void** data,
                        size_t* sizes, int* tags, void** mhandles,
                        void** phandles, void** request);
  ncclResult_t (*iflush)(void* recvComm, int n, void** data,
                         int* sizes, void** mhandles, void** request);
  ncclResult_t (*test)(void* request, int* done, int* sizes);
  ncclResult_t (*closeSend)(void* sendComm);
  ncclResult_t (*closeRecv)(void* recvComm);
  ncclResult_t (*closeListen)(void* listenComm);
  ncclResult_t (*getDeviceMr)(void* comm, void* mhandle, void** dptr_mhandle);
  ncclResult_t (*irecvConsumed)(void* recvComm, int n, void* request);
  ncclResult_t (*makeVDevice)(int* d, ncclNetVDeviceProps_v11_t* props);
  ncclResult_t (*finalize)(void* ctx);
  ncclResult_t (*setNetAttr)(void* ctx, ncclNetAttr_v11_t* netAttr);
} ncclNet_v11_t;
```

#### CollNet v11

```c
typedef struct {
  const char* name;
  ncclResult_t (*init)(void** ctx, uint64_t commId,
                       ncclDebugLogger_t logFunction);
  ncclResult_t (*devices)(int* ndev);
  ncclResult_t (*getProperties)(int dev, ncclNetProperties_v11_t* props);
  ncclResult_t (*listen)(void* ctx, int dev, void* handle, void** listenComm);
  ncclResult_t (*connect)(void* handles[], int nranks, int rank,
                          void* listenComm, void** collComm);
  ncclResult_t (*reduceSupport)(ncclDataType_t dataType,
                                ncclRedOp_t redOp, int* supported);
  ncclResult_t (*regMr)(void* collComm, void* data, size_t size,
                        int type, void** mhandle);
  ncclResult_t (*regMrDmaBuf)(void* collComm, void* data, size_t size,
                              int type, uint64_t offset, int fd,
                              void** mhandle);
  ncclResult_t (*deregMr)(void* collComm, void* mhandle);
  ncclResult_t (*iallreduce)(void* collComm, void* sendData, void* recvData,
                             size_t count, ncclDataType_t dataType,
                             ncclRedOp_t redOp, void* sendMhandle,
                             void* recvMhandle, void** request);
  ncclResult_t (*iallgather)(void* collComm, void* sendData, int nRecvParts,
                             ncclNetSGE_v11_t* recvParts,
                             size_t bytesPerRank, size_t windowOffset,
                             size_t windowBytes, void* sendMhandle,
                             void** request);
  ncclResult_t (*ireducescatter)(void* collComm, int nSendParts,
                                 ncclNetSGE_v11_t* sendParts, void* recvData,
                                 size_t bytesPerRank, size_t windowOffset,
                                 size_t windowBytes, ncclDataType_t dataType,
                                 ncclRedOp_t redOp, void* recvMhandle,
                                 void** request);
  ncclResult_t (*iflush)(void* collComm, void* data, int size,
                         void* mhandle, void** request);
  ncclResult_t (*test)(void* request, int* done, int* size);
  ncclResult_t (*closeColl)(void* collComm);
  ncclResult_t (*closeListen)(void* listenComm);
  ncclResult_t (*makeVDevice)(int* d, ncclNetVDeviceProps_v11_t* props);
  ncclResult_t (*finalize)(void* ctx);
} ncclCollNet_v11_t;
```

Notes:

- Example net plugin exports `ncclNetPlugin_v11`, `v10`, `v9`, `v8`, `v7`, `v6`, plus older compatibility objects. It is a stub transport example, not a functional backend.
- Current loader probes `ncclNetPlugin_v11` down to `v6` and `ncclCollNetPlugin_v11` down to `v6`.

### Env

Current alias:

- `nccl/src/include/plugin/nccl_env.h` aliases `ncclEnv_t` to `ncclEnv_v1_t`.
- Current symbol macro: `NCCL_ENV_PLUGIN_SYMBOL` = `ncclEnvPlugin_v1`.

```c
typedef struct {
  const char* name;
  ncclResult_t (*init)(uint8_t ncclMajor, uint8_t ncclMinor,
                       uint8_t ncclPatch, const char* suffix);
  ncclResult_t (*finalize)(void);
  const char* (*getEnv)(const char* name);
} ncclEnv_v1_t;
```

Notes:

- The plugin must keep returned strings valid until `finalize()` or until the next `getEnv()` for the same variable.
- ENV is process-global and bootstraps `ncclGetEnv()`, so the loader uses `std::getenv("NCCL_ENV_PLUGIN")` directly instead of recursing through `ncclGetEnv()`.

### Profiler

Current alias:

- `nccl/src/include/plugin/nccl_profiler.h` aliases `ncclProfiler_t` to `ncclProfiler_v6_t`.
- Current symbol is `ncclProfiler_v6`.
- Loader compatibility extends back to `ncclProfiler_v1`.

#### Profiler v6

```c
typedef struct {
  const char* name;
  ncclResult_t (*init)(void** context, uint64_t commId,
                       int* eActivationMask, const char* commName,
                       int nNodes, int nranks, int rank,
                       ncclDebugLogger_t logfn);
  ncclResult_t (*startEvent)(void* context, void** eHandle,
                             ncclProfilerEventDescr_v6_t* eDescr);
  ncclResult_t (*stopEvent)(void* eHandle);
  ncclResult_t (*recordEventState)(void* eHandle,
                                   ncclProfilerEventState_v6_t eState,
                                   ncclProfilerEventStateArgs_v6_t* eStateArgs);
  ncclResult_t (*finalize)(void* context);
} ncclProfiler_v6_t;
```

Version evolution that matters:

- v1, v2, v3:
  - `init(void** context, int* eActivationMask)`
  - event descriptor and event-state argument structs are older/smaller.
- v4:
  - adds communicator metadata and logger:
    `init(void** context, int* eActivationMask, const char* commName, uint64_t commHash, int nNodes, int nranks, int rank, ncclDebugLogger_t logfn)`
- v5:
  - moves to `commId` terminology and richer event descriptors.
- v6:
  - adds CE event support (`ncclProfileCeColl`, `ncclProfileCeSync`, `ncclProfileCeBatch`).

Compatibility behavior:

- NCCL tries `v6`, then `v5`, `v4`, `v3`, `v2`, `v1`.
- The v5 wrapper masks out CE event bits because v5 has no CE support.
- Older wrappers downgrade descriptors and ignore unsupported event/state types.

### GIN (not requested, but important for one-sided APIs)

GIN is not part of the user request's ABI list, but it matters because NCCL one-sided host-RMA is implemented on top of it.

Key current operations in `ncclGin_v12_t`:

- `init`, `devices`, `getProperties`, `listen`, `connect`
- `createContext`
- `regMrSym`, `regMrSymDmaBuf`, `deregMrSym`
- `iput`
- `iputSignal`
- `test`
- `ginProgress`
- `queryLastError`
- `finalize`

If the policy plane eventually wants to intercept or replace one-sided remote progress, GIN is the relevant plugin family, not NET or TUNER.

## Plugin Discovery and Loading

### Generic Loader Rules

`nccl/src/plugin/plugin_open.cc` centralizes the common loader logic for `NET`, `GIN`, `TUNER`, `PROFILER`, and `ENV`.

Fixed mappings:

- `NET` -> prefix `libnccl-net`
- `GIN` -> prefix `libnccl-gin`
- `TUNER` -> prefix `libnccl-tuner`
- `PROFILER` -> prefix `libnccl-profiler`
- `ENV` -> prefix `libnccl-env`

The loader algorithm is:

1. If a plugin name/path is provided, try `dlopen(name, RTLD_NOW | RTLD_LOCAL)` exactly as given.
2. If that fails and the provided string looks like a short suffix rather than a path or full SONAME, try `<prefix>-<name>.so`.
3. If no name/path is provided, try `<prefix>.so`.
4. If the string is `STATIC_PLUGIN`, the loader special-cases it and does not attempt a real `dlopen`.

Implications:

- `NCCL_TUNER_PLUGIN=example` resolves to `libnccl-tuner-example.so`.
- `NCCL_TUNER_PLUGIN=libnccl-tuner-example.so` is used directly.
- `NCCL_TUNER_PLUGIN=/abs/path/libnccl-tuner-example.so` is also used directly.

### Per-plugin Environment Variables, Defaults, and Fallbacks

| Plugin family | Load env var | Default external candidate | Symbol probe order | Fallback behavior |
| --- | --- | --- | --- | --- |
| ENV | `NCCL_ENV_PLUGIN` | `libnccl-env.so` | `ncclEnvPlugin_v1` | if load fails or env is `none`, NCCL uses its internal default env plugin |
| TUNER | `NCCL_TUNER_PLUGIN` | `libnccl-tuner.so` | `ncclTunerPlugin_v5`, `v4`, `v3`, `v2` | if standalone load fails, NCCL tries to reuse the already-opened NET plugin library |
| PROFILER | `NCCL_PROFILER_PLUGIN` | `libnccl-profiler.so` | `ncclProfiler_v6`, `v5`, `v4`, `v3`, `v2`, `v1` | if standalone load fails, NCCL tries to reuse the already-opened NET plugin library |
| NET | `NCCL_NET_PLUGIN` | `libnccl-net.so` when unset | `ncclNetPlugin_v11`..`v6` and optional `ncclCollNetPlugin_v11`..`v6` | internal `ib` and `socket` plugins are always added |
| GIN | `NCCL_GIN_PLUGIN` | `libnccl-gin.so` when unset | `ncclGinPlugin_v12`, `v11` | also probes the already-opened NET plugin library; internal IB GIN plugin is always added |

Important special cases:

- `NCCL_TUNER_PLUGIN=none` disables external tuner loading.
- `NCCL_PROFILER_PLUGIN=none` disables external profiler loading.
- `NCCL_ENV_PLUGIN=none` forces fallback to the internal env plugin.
- `NCCL_NET_PLUGIN=none` disables external NET candidates, but internal IB and socket remain.
- `NCCL_GIN_PLUGIN=none` disables named external GIN candidates, but NET piggyback probing and internal IB GIN still remain.

### NET-library Piggybacking

`ncclGetNetPluginLib()` is a key detail. If a NET library is already open, NCCL re-opens that same library path for:

- `TUNER`
- `PROFILER`
- `GIN`

This is why the mixed plugin example works cleanly.

What it means in practice:

- One `.so` can export `ncclNetPlugin_v11`, `ncclTunerPlugin_v5`, and/or `ncclProfiler_v6`.
- If NCCL has already opened that `.so` as the NET plugin, the tuner/profiler/gin loaders can find their own symbols in the same shared object even when their standalone env vars are unset.

Limitation:

- ENV does not have this piggyback path. A multi-purpose `.so` can still export `ncclEnvPlugin_v1`, but it must be opened explicitly through `NCCL_ENV_PLUGIN`.

### Load-time vs Selection-time Variables

There are two separate mechanisms for networking:

- `NCCL_NET_PLUGIN` chooses external plugin library candidates.
- `NCCL_NET` chooses the network by plugin `name` field at communicator configuration time.

That is an important distinction. `NCCL_NET` does not pick a filename; it picks the plugin instance whose `name` matches `comm->config.netName`.

### Process-wide vs Per-communicator Semantics

- ENV is effectively process-global.
- TUNER and PROFILER libraries are process-global with reference counting, but each communicator gets its own plugin context.
- NET and GIN can keep multiple candidates alive, then choose a specific implementation per communicator.
- Once one external NET plugin is assigned to a communicator, NCCL disables the other external NET candidates.

## Plugin Init / Use / Finalize Sequence

### ENV

1. `ncclGetEnv()` calls `ncclInitEnv()`.
2. `ncclInitEnv()` runs `ncclEnvPluginInit()` once.
3. NCCL loads either the external env plugin or its internal default env plugin.
4. NCCL calls `envPlugin->init(NCCL_MAJOR, NCCL_MINOR, NCCL_PATCH, NCCL_SUFFIX)`.
5. Later env reads go through `ncclEnvPluginGetEnv()`.
6. Finalization is registered with `atexit`.

### PROFILER

1. `ncclProfilerPluginInit(comm)` runs during communicator init.
2. NCCL explicitly initializes profiler context before creating proxy threads.
3. If a profiler plugin is present, NCCL calls:

```c
ncclProfiler->init(&comm->profilerContext, comm->commHash,
                   &ncclProfilerEventMask, comm->config.commName,
                   comm->nNodes, comm->nRanks, comm->rank, ncclDebugLog);
```

4. NCCL then uses `startEvent`, `recordEventState`, and `stopEvent` across API, proxy, kernel, and NET-plugin events.
5. On cleanup, NCCL calls `ncclProfiler->finalize(comm->profilerContext)` and unloads the plugin.

### TUNER

1. NCCL computes topology-derived tuning constants.
2. NCCL loads the tuner plugin.
3. If present, NCCL calls:

```c
comm->tuner->init(&comm->tunerContext, comm->commHash,
                  comm->nRanks, comm->nNodes, ncclDebugLog,
                  &comm->nvlDomainInfo, &comm->tunerConstants);
```

4. On every collective selection path, NCCL prepares a cost table and then calls:

```c
comm->tuner->getCollInfo(comm->tunerContext, info->func, nBytes,
                         numPipeOps, (float**)collCostTable,
                         NCCL_NUM_ALGORITHMS, NCCL_NUM_PROTOCOLS,
                         regBuff, &nMaxChannels);
```

5. On communicator cleanup, NCCL calls `comm->tuner->finalize(comm->tunerContext)` and unloads the plugin.

### NET / GIN

1. NCCL builds candidate plugin lists from `NCCL_NET_PLUGIN` and `NCCL_GIN_PLUGIN`.
2. It appends internal implementations.
3. During communicator init, it loads external libraries on demand, initializes contexts, and attempts assignment.
4. GIN also probes the NET library for GIN symbols.
5. Finalization happens during communicator teardown, with library unload when refcounts reach zero.

## What the Example Plugins Show

### `plugins/tuner/example/plugin.c`

This is the most realistic tuner example.

Behavior:

- Exports `ncclTunerPlugin_v5`.
- Reads configuration from `NCCL_TUNER_CONFIG_FILE`, defaulting to `nccl_tuner.conf`.
- Parses a CSV-like rule file.
- Matches on collective type, message size, algorithm, protocol, channels, node count, rank count, and optionally `numPipeOps` and `regBuff`.
- Edits the cost table and can adjust `nChannels`.
- Mutates `ncclTunerConstants_v5_t` in `init()`.

This is a good example of a pure control-plane hook: it changes NCCL decisions without replacing the transport.

### `plugins/tuner/basic/plugin.c`

Behavior:

- Exports `ncclTunerPlugin_v4`.
- Implements the minimum v4 tuner.
- Biases the cost table toward ring/simple when that entry is available.

This is intentionally small and demonstrates the older v4 ABI still being loadable through the current NCCL wrappers.

### `plugins/net/example/plugin.c`

Behavior:

- Exports `ncclNetPlugin_v11` plus multiple compatibility versions.
- Stubs most operations.
- Reports zero devices, so it is not a real transport backend.

Use it as an ABI example only.

### `plugins/env/example/plugin.c`

Behavior:

- Exports `ncclEnvPlugin_v1`.
- Mostly forwards to regular environment lookup.

This demonstrates the minimal env-plugin shape.

### `plugins/profiler/example/plugin.cc`

Behavior:

- Exports both `ncclProfiler_v5` and `ncclProfiler_v6`.
- Implements a full reference profiler with internal event pools and dump/output logic.
- Uses `NCCL_PROFILE_EVENT_MASK` to control activation.
- Supports CE events in the v6 export.
- Exposes extra helper symbols `exampleProfilerStart()` and `exampleProfilerStop()` that are not part of NCCL's required ABI.

This is the best example if the goal is to export structured runtime telemetry to an external controller.

### `plugins/profiler/inspector/inspector_plugin.cc`

Behavior:

- Exports `ncclProfiler_v5`.
- Enables `ncclProfileColl | ncclProfileKernelCh`.
- Tracks per-collective execution and summarizes performance.

This is a narrower, analysis-oriented profiler example.

### `plugins/mixed/example/plugin.c`

Behavior:

- Exports both `ncclNetPlugin_v11` and `ncclTunerPlugin_v5` from the same shared object.

This is the key demonstration that NCCL's mixed-plugin deployment model is symbol-based, not one-plugin-per-`.so`.

## Mixed Plugin Pattern

The mixed example proves two separate facts:

1. A single shared object can export multiple NCCL plugin symbols.
2. NCCL's loader explicitly supports reusing the NET plugin library for GIN, TUNER, and PROFILER symbol lookup.

Practical deployment patterns:

- Set `NCCL_NET_PLUGIN` to a mixed library and leave `NCCL_TUNER_PLUGIN` / `NCCL_PROFILER_PLUGIN` unset.
  - NCCL loads NET first.
  - Later tuner/profiler loading can find their symbols in the same library through `ncclGetNetPluginLib()`.
- Point multiple env vars at the same exact path:
  - `NCCL_NET_PLUGIN=/path/libfoo.so`
  - `NCCL_TUNER_PLUGIN=/path/libfoo.so`
  - `NCCL_PROFILER_PLUGIN=/path/libfoo.so`
  - `NCCL_ENV_PLUGIN=/path/libfoo.so`

Operational limitation:

- The automatic piggyback mechanism does not exist for ENV.
- If you want ENV in the same `.so`, you must point `NCCL_ENV_PLUGIN` to it explicitly.

## Elastic APIs: Revoke / Shrink / Grow

### `ncclCommRevoke`

Source behavior:

- Public API comment says revoke stops in-flight work and makes later destroy/split/shrink safe.
- Implementation validates flags and refuses revoke if destroy/finalize/revoke is already underway.
- Sets communicator abort/revoke state, including `revokedFlag`.
- Synchronizes outstanding work through an async job path.
- Stops proxy activity and joins proxy threads when needed.
- Later enqueue paths reject work on revoked communicators.

Practical meaning:

- Revoke is a communicator-level emergency stop / quiesce path.
- It is not a rollback hook and not a plugin callback surface.

### `ncclCommShrink`

Source behavior:

- Creates a new communicator; it does not mutate the parent in place.
- Excluded ranks do not participate.
- Surviving ranks are compacted in original order.
- `NCCL_SHRINK_ABORT` can be used to force an abort-style path while creating the child communicator.
- NCCL may share resources with the parent only if the parent is not revoked, `SHRINK_ABORT` is not used, and sharing is enabled.

Practical meaning:

- Shrink is a child-communicator constructor over a surviving rank subset.

### `ncclCommGetUniqueId` + `ncclCommGrow`

Source behavior:

- Grow also creates a new communicator; it does not resize the parent in place.
- Existing ranks keep their rank numbers.
- New ranks join at explicitly assigned ranks.
- NCCL uses a child namespace / magic scheme in bootstrap to distinguish the new communicator.
- New ranks execute the usual init path before joining.

Practical meaning:

- Grow is "create a larger child communicator around an existing one", not "hot-add ranks to the current object in place".

### Elastic API Takeaway

These APIs are useful for an eBPF-based orchestration layer, but they are not plugin hooks. If the policy plane must observe or gate them, the practical mechanisms are:

- wrapper library / interception
- profiler correlation
- uprobes/eBPF on the NCCL public API entrypoints

## One-sided / Host API / Put-Wait Functionality

NCCL has a real public one-sided host API in `nccl.h.in`:

```c
ncclResult_t ncclPutSignal(const void* localbuff, size_t count,
                           ncclDataType_t datatype, int peer,
                           ncclWindow_t peerWin, size_t peerWinOffset,
                           int sigIdx, int ctx, unsigned int flags,
                           ncclComm_t comm, cudaStream_t stream);

ncclResult_t ncclSignal(int peer, int sigIdx, int ctx,
                        unsigned int flags, ncclComm_t comm,
                        cudaStream_t stream);

typedef struct {
  int opCnt;
  int peer;
  int sigIdx;
  int ctx;
} ncclWaitSignalDesc_t;

ncclResult_t ncclWaitSignal(int nDesc, ncclWaitSignalDesc_t* signalDescs,
                            ncclComm_t comm, cudaStream_t stream);
```

Related window APIs also exist:

- `ncclCommWindowRegister`
- `ncclCommWindowDeregister`
- `ncclWinGetUserPtr`

Current core restrictions in `enqueue.cc`:

- `comm->hostRmaSupport` must be true.
- `ctx` must be `0`.
- `sigIdx` must be `0`.
- `flags` must be `0`.
- `ncclPutSignal` requires both source and destination to be in valid symmetric windows.
- `ncclSignal` requires `count == 0`.
- `ncclWaitSignal` validates each descriptor and currently accepts only the restricted form above.

How NCCL executes it:

- The planner enqueues RMA tasks.
- NCCL may split execution between:
  - a CE path for LSA-accessible peers
  - a proxy path for non-LSA peers
- The proxy path issues remote operations through the GIN plugin's `iput` and `iputSignal` functions.

Relevant communicator capability logic:

```c
comm->globalRmaProxySupport =
  globalRmaPluginSupport && globalCrossNicSupport &&
  !globalNicFused && globalCuMemGdrSupport;

comm->hostRmaSupport =
  comm->symmetricSupport &&
  ((ncclTeamLsa(comm).nRanks == comm->nRanks) ||
   comm->globalRmaProxySupport);
```

Takeaway:

- One-sided is present and usable, but only when the communicator is built with the required symmetric-memory and GIN/RMA support.
- If the policy plane eventually wants to control one-sided progress or remote signaling, GIN is the real extension point.

## Build Status

### Example tuner plugin

Command run:

```bash
make -C nccl/plugins/tuner/example
```

Status:

- Exit code `0`.
- The example tuner plugin build is working in this environment.
- An earlier run compiled `plugin.c` into `libnccl-tuner-example.so`; the later rerun completed cleanly with no rebuild needed.

### NCCL core

Command run:

```bash
cd nccl
make -j$(nproc) src.build
```

Observed status during this exploration:

- The build completed successfully in this environment.
- Final artifacts observed:
  - `nccl/build/lib/libnccl.so.2.29.7`
  - `nccl/build/lib/libnccl_static.a`
- The repeated warnings were NVCC deprecation warnings about offline compilation for architectures earlier than `sm_75`.

## Assessment for an eBPF Policy Plane

### Best hooks

| Hook | What it gives you | Fit for eBPF policy plane | Main limitation |
| --- | --- | --- | --- |
| TUNER | per-collective algo/proto/channel choice via cost tables | best direct control hook | cannot change transport behavior or communicator lifecycle |
| PROFILER | structured runtime events from API, proxy, kernel, CE, and NET-plugin layers | best observability hook | it observes and reports; it does not directly steer algorithm selection |
| ENV | bootstrap/config indirection for NCCL params | useful for initial policy injection | coarse-grained and mostly static |
| NET | full transport implementation | only if policy must own networking path | high complexity, high maintenance, transport-correctness burden |
| GIN | one-sided / symmetric-memory remote put-signal path | only if policy must own RMA/proxy semantics | high complexity and tied to advanced communicator capabilities |

### Recommended architecture

For an eBPF-backed policy plane, the practical first design is:

1. Start with a mixed userspace plugin that combines:
   - `TUNER` for decisions
   - `PROFILER` for telemetry
2. Keep `NET` and `GIN` untouched initially.
3. Use eBPF only as the external telemetry/control substrate:
   - uprobes/kprobes/tracepoints gather host and kernel-side signals
   - userspace policy daemon writes current policy into pinned BPF maps or a shared memory control block
   - tuner plugin reads policy state during `init()` and `getCollInfo()`
   - profiler plugin exports NCCL runtime observations back into the policy loop

Why this is the right first cut:

- It uses NCCL's stable-enough extension seams.
- It avoids replacing the transport stack.
- It keeps the policy logic in user space while still letting eBPF supply low-overhead telemetry and shared state.

### What is realistically controllable

With TUNER plus PROFILER you can realistically control:

- algorithm choice
- protocol choice
- effective channel budget
- workload-specific overrides by size, collective type, rank count, node count, and registered-buffer state
- feedback loops driven by observed proxy/kernel/network event timing

You cannot directly control through these hooks:

- communicator revoke/shrink/grow admission
- one-sided transport progress
- NIC/device memory registration semantics
- bootstrap/connect/listen behavior

Those require either API interception or a custom NET/GIN implementation.

### If deeper control is required

Move to NET or GIN only if one of these is true:

- the policy plane must choose NIC-level behavior that NCCL does not surface through tuning
- the policy plane must own one-sided remote progress or signal semantics
- the policy plane must attach transport-defined profiler events at the source

That step is much more invasive because NET and GIN are not lightweight callbacks; they are full plugin-owned backends with lifecycle, memory registration, async request handling, and correctness responsibilities.

### Versioning risk

Out-of-tree plugins are tightly coupled to NCCL's internal plugin headers.

Evidence:

- NCCL ships versioned plugin ABI headers in-tree.
- The example tuner plugin vendors a forked header copy.
- That vendored copy can drift from the internal ABI definitions.

Operational recommendation:

- Pin the NCCL version.
- Build the plugin against the exact NCCL tree you deploy.
- Treat ABI compatibility as release-specific, especially for TUNER and PROFILER.

## Bottom Line

If the goal is an eBPF policy plane for NCCL, the most credible path is:

- `PROFILER` for observation
- `TUNER` for control
- optional `ENV` for bootstrap/config

Use a mixed shared object if convenient, but remember that automatic NET-library piggybacking applies to `TUNER`, `PROFILER`, and `GIN`, not `ENV`.

Only move into `NET` or `GIN` once the policy plane truly needs transport ownership or one-sided/RMA control, because that crosses from "policy hook" into "re-implement a communication backend".
