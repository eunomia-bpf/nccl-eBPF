# NCCL Net Plugin Experiment

Date: 2026-03-10

Goal:
- Prototype an eBPF-based NCCL net plugin that wraps the built-in Socket transport.
- Verify that bpftime-based hook execution works in the net-plugin path, not just the tuner/profiler path.
- Measure whether the wrapper adds material overhead on a 128 MB socket-only `all_reduce_perf_mpi` run.

## Setup

Testbed:
- GPU: NVIDIA GeForce RTX 5090
- Ranks: 2 MPI ranks, both using GPU 0
- NCCL: `/home/yunwei37/workspace/nccl-eBPF/nccl` (2.29.7)
- nccl-tests: `/home/yunwei37/workspace/nccl-eBPF/nccl-tests/build/all_reduce_perf_mpi`
- Transport forcing:
  - `NCCL_NET=Socket`
  - `NCCL_P2P_DISABLE=1`
  - `NCCL_SHM_DISABLE=1`

Artifacts:
- Wrapper plugin: `/home/yunwei37/workspace/nccl-eBPF/src/nccl-net-ebpf-plugin/build/libnccl-net-ebpf.so`
- eBPF object: `/home/yunwei37/workspace/nccl-eBPF/src/nccl-net-ebpf-plugin/build/net_trace.bpf.o`
- Raw results TSV: `/home/yunwei37/workspace/nccl-eBPF/docs/tmp/net-plugin-results.tsv`
- Per-run logs: `/home/yunwei37/workspace/nccl-eBPF/docs/tmp/net-plugin-logs/`

## 1. Net Plugin API Analysis

Current API:
- In NCCL 2.29.7, `ncclNet_t` is an alias for `ncclNet_v11_t`.
- Header path: `nccl/src/include/plugin/nccl_net.h`
- Struct definition: `nccl/src/include/plugin/net/net_v11.h`

Relevant `ncclNet_v11_t` callback signatures:

```c
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
ncclResult_t (*getDeviceMr)(void* comm, void* mhandle,
                            void** dptr_mhandle);
ncclResult_t (*irecvConsumed)(void* recvComm, int n, void* request);
ncclResult_t (*makeVDevice)(int* d, ncclNetVDeviceProps_v11_t* props);
ncclResult_t (*finalize)(void* ctx);
ncclResult_t (*setNetAttr)(void* ctx, ncclNetAttr_v11_t* netAttr);
```

Behavioral details that matter for wrapper design:
- `connect` and `accept` must be non-blocking. NCCL may call them repeatedly until `sendComm` or `recvComm` becomes non-NULL.
- `isend` and `irecv` are asynchronous and may return `request == NULL` when the operation cannot be posted yet.
- `test` completes async requests and optionally returns the completed byte count.
- Several callbacks are optional in practice for the Socket backend: `regMrDmaBuf`, `getDeviceMr`, `irecvConsumed`, `makeVDevice`, `setNetAttr`.

Most useful hook points for eBPF policies:

| Hook | Why it is useful | Prototype choice |
| --- | --- | --- |
| `init` | Per-communicator policy/bootstrap context | Used |
| `listen` | Connection setup visibility | Used |
| `connect` | Outgoing connection policy / logging | Used |
| `accept` | Incoming connection policy / logging | Used |
| `regMr` / `deregMr` | Buffer registration policies / accounting | Not used in quick prototype |
| `isend` | Send-side byte accounting / rate policy | Used |
| `irecv` | Receive-side byte accounting / rate policy | Used |
| `test` | Completion-time accounting / latency policy | Skipped to avoid extra request wrapping in the quick prototype |
| `finalize` | Dumping state / cleanup | Used |

## 2. Feasibility And Constraint

The main obstacle was not bpftime. It was access to NCCL's built-in Socket backend.

Problem:
- NCCL registers Socket internally via `ncclNetSocket`.
- In the stock `libnccl.so`, that symbol was not exported, so an external plugin could not `dlsym()` it.

Observed before patch:
- `nm -D nccl/build/lib/libnccl.so` showed no `ncclNetSocket`.
- `readelf -Ws` showed it as a `LOCAL` symbol only.

Minimal fix:
- Export `ncclNetSocket` from `nccl/src/transport/net_socket.cc` with default visibility.

Observed after rebuild:

```text
$ nm -D nccl/build/lib/libnccl.so | rg ncclNetSocket
00000000160ab0c0 D ncclNetSocket
```

Conclusion:
- A practical wrapper plugin is feasible, but in this tree it required a one-line NCCL patch to export `ncclNetSocket`.
- Without that patch, the next-smallest option would be build-time reuse via `libnccl_static.a` or copying the Socket transport sources into the plugin.

## 3. Implementation Approach

New code:
- `src/nccl-net-ebpf-plugin/plugin.cpp`
- `src/nccl-net-ebpf-plugin/net_ebpf_ctx.h`
- `src/nccl-net-ebpf-plugin/net_trace.bpf.c`
- `src/nccl-net-ebpf-plugin/CMakeLists.txt`

Design:
1. Implement an external `ncclNet_v11` plugin whose exported name is still `"Socket"`.
2. Resolve the built-in backend at runtime with `dlsym(RTLD_DEFAULT, "ncclNetSocket")`.
3. Forward the real transport operations to the built-in Socket plugin.
4. Execute a bpftime program around selected hook points:
   - `init`
   - `listen`
   - `connect`
   - `accept`
   - `isend`
   - `irecv`
   - `finalize`
5. Load a tiny BPF object that maintains an array-map `stats_map` keyed by hook id.
6. On finalize, dump the map so each run leaves a visible proof that hooks fired and bytes were counted.

What the eBPF program does:
- Per hook, increment `calls`
- Per hook, accumulate `bytes`
- Record `last_comm_id`
- Record `last_tag`

What it does not do yet:
- No request-completion interception in `test`
- No active policy decisions that modify transport behavior
- No memory-registration or `setNetAttr` policy

That is intentional for a quick prototype: the goal here was to prove that the net-plugin path can host bpftime-executed logic with minimal transport changes.

## 4. Build Commands

Rebuild NCCL after exporting `ncclNetSocket`:

```bash
make -C /home/yunwei37/workspace/nccl-eBPF/nccl -j4 \
  src.build BUILDDIR=/home/yunwei37/workspace/nccl-eBPF/nccl/build
```

Build the wrapper plugin and BPF object:

```bash
cmake -S /home/yunwei37/workspace/nccl-eBPF/src/nccl-net-ebpf-plugin \
      -B /home/yunwei37/workspace/nccl-eBPF/src/nccl-net-ebpf-plugin/build
cmake --build /home/yunwei37/workspace/nccl-eBPF/src/nccl-net-ebpf-plugin/build -j4
```

## 5. Benchmark Method

Benchmark:
- Binary: `all_reduce_perf_mpi`
- Size: `134217728` bytes only
- Warmup iters per invocation: `-w 2`
- Measured iters per invocation: `-n 5`
- Independent invocations per mode: 5

Mode A, baseline:
- No external net plugin

Mode B, wrapped:
- `NCCL_NET_PLUGIN=/home/yunwei37/workspace/nccl-eBPF/src/nccl-net-ebpf-plugin/build/libnccl-net-ebpf.so`

Command template:

```bash
mpirun --allow-run-as-root --oversubscribe \
  -np 1 /usr/bin/env -u DISPLAY \
    LD_LIBRARY_PATH=/home/yunwei37/workspace/nccl-eBPF/nccl/build/lib \
    NCCL_TESTS_DEVICE=0 \
    NCCL_DEBUG=WARN \
    NCCL_NET=Socket \
    NCCL_P2P_DISABLE=1 \
    NCCL_SHM_DISABLE=1 \
    NCCL_HOSTID=<mode>-rank0 \
    <plugin envs> \
    /home/yunwei37/workspace/nccl-eBPF/nccl-tests/build/all_reduce_perf_mpi \
      -b 134217728 -e 134217728 -g 1 -n 5 -w 2 \
  : \
  -np 1 /usr/bin/env -u DISPLAY \
    LD_LIBRARY_PATH=/home/yunwei37/workspace/nccl-eBPF/nccl/build/lib \
    NCCL_TESTS_DEVICE=0 \
    NCCL_DEBUG=WARN \
    NCCL_NET=Socket \
    NCCL_P2P_DISABLE=1 \
    NCCL_SHM_DISABLE=1 \
    NCCL_HOSTID=<mode>-rank1 \
    <plugin envs> \
    /home/yunwei37/workspace/nccl-eBPF/nccl-tests/build/all_reduce_perf_mpi \
      -b 134217728 -e 134217728 -g 1 -n 5 -w 2
```

## 6. Benchmark Results

Raw per-run `algbw`:

| Mode | Run1 | Run2 | Run3 | Run4 | Run5 |
| --- | ---: | ---: | ---: | ---: | ---: |
| baseline Socket | 2.13 | 2.10 | 2.10 | 2.19 | 2.15 |
| wrapped Socket+eBPF | 1.92 | 2.02 | 2.15 | 2.18 | 2.20 |

Statistics:

| Mode | Mean algbw (GB/s) | Median (GB/s) | Std (GB/s) |
| --- | ---: | ---: | ---: |
| baseline Socket | 2.134 | 2.13 | 0.034 |
| wrapped Socket+eBPF | 2.094 | 2.15 | 0.107 |

Derived comparison:
- Mean slowdown of wrapped vs baseline: about `1.87%`
- Median result is effectively unchanged in this small sample: `2.13` vs `2.15 GB/s`

Interpretation:
- The wrapper did not cause a large regression.
- The 5-run mean suggests a small overhead, but the per-run spread overlaps heavily.
- On this quick prototype, the result is consistent with "low overhead, roughly noise-scale" rather than a structural throughput collapse.

## 7. eBPF Hook Evidence

Representative excerpt from `wrapped.run1.log`:

```text
[nccl-net-ebpf] stats comm=17601057159367469848 hook=init calls=1 bytes=0 lastTag=-1
[nccl-net-ebpf] stats comm=17601057159367469848 hook=listen calls=2 bytes=0 lastTag=-1
[nccl-net-ebpf] stats comm=17601057159367469848 hook=connect calls=2 bytes=0 lastTag=-1
[nccl-net-ebpf] stats comm=17601057159367469848 hook=accept calls=2 bytes=0 lastTag=-1
[nccl-net-ebpf] stats comm=17601057159367469848 hook=isend calls=2048 bytes=2147483648 lastTag=1
[nccl-net-ebpf] stats comm=17601057159367469848 hook=irecv calls=2048 bytes=2147483648 lastTag=0
[nccl-net-ebpf] stats comm=17601057159367469848 hook=finalize calls=1 bytes=0 lastTag=-1
```

What this proves:
- The external net plugin was actually selected.
- The wrapper forwarded the transport correctly.
- The bpftime program executed in the live net-plugin path and updated shared eBPF map state.

Why counted bytes exceed 128 MB:
- The counters are per-hook submission totals across all warmup and measured iterations.
- NCCL also splits a collective into many network requests across channels, so the hook count is much larger than 1 per collective call.

## 8. Raw Data And Logs

Raw TSV:

```tsv
mode	run	algbw_gbps	busbw_gbps	log
baseline	1	2.13	2.13	docs/tmp/net-plugin-logs/baseline.run1.log
baseline	2	2.10	2.10	docs/tmp/net-plugin-logs/baseline.run2.log
baseline	3	2.10	2.10	docs/tmp/net-plugin-logs/baseline.run3.log
baseline	4	2.19	2.19	docs/tmp/net-plugin-logs/baseline.run4.log
baseline	5	2.15	2.15	docs/tmp/net-plugin-logs/baseline.run5.log
wrapped	1	1.92	1.92	docs/tmp/net-plugin-logs/wrapped.run1.log
wrapped	2	2.02	2.02	docs/tmp/net-plugin-logs/wrapped.run2.log
wrapped	3	2.15	2.15	docs/tmp/net-plugin-logs/wrapped.run3.log
wrapped	4	2.18	2.18	docs/tmp/net-plugin-logs/wrapped.run4.log
wrapped	5	2.20	2.20	docs/tmp/net-plugin-logs/wrapped.run5.log
```

Log files:
- Smoke run with `NCCL_DEBUG=INFO`: `docs/tmp/net-plugin-logs/smoke.log`
- Baseline runs:
  - `docs/tmp/net-plugin-logs/baseline.run1.log`
  - `docs/tmp/net-plugin-logs/baseline.run2.log`
  - `docs/tmp/net-plugin-logs/baseline.run3.log`
  - `docs/tmp/net-plugin-logs/baseline.run4.log`
  - `docs/tmp/net-plugin-logs/baseline.run5.log`
- Wrapped runs:
  - `docs/tmp/net-plugin-logs/wrapped.run1.log`
  - `docs/tmp/net-plugin-logs/wrapped.run2.log`
  - `docs/tmp/net-plugin-logs/wrapped.run3.log`
  - `docs/tmp/net-plugin-logs/wrapped.run4.log`
  - `docs/tmp/net-plugin-logs/wrapped.run5.log`

## 9. Takeaways

Bottom line:
- An eBPF-extended NCCL net plugin is feasible in this tree.
- The cleanest quick prototype is a pass-through wrapper around the built-in Socket backend.
- In the current NCCL build, that required exporting `ncclNetSocket`.
- The measured overhead on this 128 MB socket-only test is small.

What would be next:
1. Move byte accounting to `test()` for exact completion-time semantics.
2. Add `regMr`/`deregMr` hooks for memory policy experiments.
3. Expose a policy-return value so BPF can do more than counting, for example selective logging, rate buckets, or connection admission/tuning.
4. Remove the NCCL patch either by upstreaming the export or by switching to static-link reuse of Socket transport objects for the wrapper.
