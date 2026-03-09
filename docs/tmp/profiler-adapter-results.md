# NCCL Profiler Adapter Results

Date: 2026-03-09

## Outcome

The mixed NCCL plugin now exports both `ncclTunerPlugin_v5` and
`ncclProfiler_v6` from the same shared object, and the profiler path writes
real collective latency samples into `telemetry_map` for the tuner policy to
consume.

This closes the main telemetry loop:

1. NCCL profiler emits collective completion timing.
2. The plugin computes per-collective latency and updates `telemetry_map`.
3. `adaptive_channels` reads that map on later `getCollInfo()` calls instead of
   relying on synthetic plugin-side latency.

## Code Changes

- `src/nccl-policy-plugin/plugin.cpp`
  - Added mixed-plugin export for `ncclProfiler_v6`.
  - Added profiler lifecycle callbacks:
    - `init`
    - `startEvent`
    - `stopEvent`
    - `recordEventState`
    - `finalize`
  - Added shared communicator state so tuner and profiler use the same loaded
    bpftime policy/maps.
  - Added real telemetry write path for `telemetry_map`.
  - Used `ncclProfileKernelCh` stop timestamps to derive collective latency.
  - Fixed teardown ordering so bpftime-backed map cleanup runs before shared
    memory teardown.
  - Fixed tuner cost-table writes to use NCCL's actual `float**` layout.

- `src/ebpf-policies/adaptive_channels.bpf.c`
  - Switched the policy to treat `telemetry_map` as profiler-owned input.
  - Added one-step-per-sample adaptation via `applied_samples`, so a single
    telemetry sample does not get re-applied across multiple `getCollInfo()`
    calls.

- `src/ebpf-policies/policy_maps.h`
  - Extended `nccl_policy_telemetry_value` with `applied_samples`.

- `src/nccl-policy-plugin/CMakeLists.txt`
  - Linked `CUDA::cudart` for CE/profiler runtime support.

- `src/nccl-policy-plugin/test/test_ebpf_plugin.c`
  - Added mixed-plugin loading and profiler bridge coverage.
  - Added synthetic kernel-timestamp event injection for the profiler bridge.
  - Hardened mismatch reporting so test failures do not dereference invalidated
    map pointers after shutdown.

## Build / Unit Validation

Build:

```bash
cmake --build /home/yunwei37/workspace/nccl-eBPF/src/nccl-policy-plugin/build \
  --target nccl-policy test_ebpf_plugin -j4
```

Result:

- `nm -D src/nccl-policy-plugin/build/libnccl-policy.so` shows:
  - `ncclTunerPlugin_v5`
  - `ncclProfiler_v6`

Unit test:

```bash
/home/yunwei37/workspace/nccl-eBPF/src/nccl-policy-plugin/build/test_ebpf_plugin
```

Observed:

- `size_aware correctness: PASS`
- `adaptive_channels map state: PASS`
- `profiler telemetry bridge: PASS`

Key bridge evidence from the unit test:

- First injected sample: `latency_ns=520`, next tuner decision changed to
  `channels=9`
- Second injected sample: `latency_ns=920`, next tuner decision changed back to
  `channels=8`

This is the clean synthetic closed-loop proof.

## Real NCCL Mixed-Plugin Run

Command used:

```bash
mpirun --oversubscribe \
  -np 1 /usr/bin/env \
    LD_LIBRARY_PATH=/home/yunwei37/workspace/nccl-eBPF/nccl/build/lib \
    NCCL_TESTS_DEVICE=0 \
    NCCL_DEBUG=INFO \
    NCCL_DEBUG_SUBSYS=INIT,ENV,TUNING,PROFILE \
    NCCL_P2P_DISABLE=1 \
    NCCL_SHM_DISABLE=1 \
    NCCL_NET=Socket \
    NCCL_HOSTID=profiler-rank0 \
    NCCL_TUNER_PLUGIN=/home/yunwei37/workspace/nccl-eBPF/src/nccl-policy-plugin/build/libnccl-policy.so \
    NCCL_PROFILER_PLUGIN=/home/yunwei37/workspace/nccl-eBPF/src/nccl-policy-plugin/build/libnccl-policy.so \
    NCCL_POLICY_BPF_PATH=/home/yunwei37/workspace/nccl-eBPF/src/nccl-policy-plugin/build/ebpf-policies/adaptive_channels.bpf.o \
    NCCL_POLICY_VERIFY_MODE=strict \
    /home/yunwei37/workspace/nccl-eBPF/nccl-tests/build/all_reduce_perf_mpi \
      -b 1M -e 1M -f 2 -g 1 -n 5 \
  : \
  -np 1 /usr/bin/env \
    LD_LIBRARY_PATH=/home/yunwei37/workspace/nccl-eBPF/nccl/build/lib \
    NCCL_TESTS_DEVICE=0 \
    NCCL_DEBUG=INFO \
    NCCL_DEBUG_SUBSYS=INIT,ENV,TUNING,PROFILE \
    NCCL_P2P_DISABLE=1 \
    NCCL_SHM_DISABLE=1 \
    NCCL_NET=Socket \
    NCCL_HOSTID=profiler-rank1 \
    NCCL_TUNER_PLUGIN=/home/yunwei37/workspace/nccl-eBPF/src/nccl-policy-plugin/build/libnccl-policy.so \
    NCCL_PROFILER_PLUGIN=/home/yunwei37/workspace/nccl-eBPF/src/nccl-policy-plugin/build/libnccl-policy.so \
    NCCL_POLICY_BPF_PATH=/home/yunwei37/workspace/nccl-eBPF/src/nccl-policy-plugin/build/ebpf-policies/adaptive_channels.bpf.o \
    NCCL_POLICY_VERIFY_MODE=strict \
    /home/yunwei37/workspace/nccl-eBPF/nccl-tests/build/all_reduce_perf_mpi \
      -b 1M -e 1M -f 2 -g 1 -n 5
```

Log:

- `docs/tmp/profiler-adapter-real-short-20260309.log`

Observed mixed-plugin load:

- `PROFILER/Plugin: Loaded eBPFPolicyProfiler (v6)`
- `TUNER/Plugin: Using eBPFPolicy (v5)`

Observed real telemetry writes:

- rank 0 first sample: `latency_ns=9264480 samples=1`
- rank 1 first sample: `latency_ns=3688000 samples=1`
- later samples remained nonzero and updated `samples=2..8`

Observed adaptive readback:

- initial tuner decision on both ranks: `channels=8`
- after first profiler sample:
  - rank 0: `channels=9`
  - rank 1: `channels=9`
- after later samples:
  - rank 0 reached `channels=10`
  - rank 1 remained at `channels=9` in the first logged post-sample step

Result:

- The mixed plugin loaded correctly.
- Profiler callbacks fired during real NCCL collectives.
- Profiler wrote nonzero latency into the telemetry path.
- `adaptive_channels` changed its returned channel count in response to those
  real samples.

## Control Run Without Profiler

Command difference:

- `NCCL_PROFILER_PLUGIN=none`

Log:

- `docs/tmp/profiler-adapter-control-no-profiler-20260309.log`

Observed:

- No profiler event logs.
- Tuner decisions stayed flat at `channels=8` on all logged calls.

This is the practical control for the real NCCL path:

- with profiler telemetry: `8 -> 9 -> 10/9`
- without profiler telemetry: `8 -> 8 -> 8`

## Benchmark Snapshot

`all_reduce_perf_mpi`, 1 MiB all-reduce, 2 ranks, socket transport, single-GPU
Phase 4 setup:

| Run | Out-of-place time (us) | In-place time (us) | Avg bus BW |
| --- | ---: | ---: | ---: |
| adaptive_channels + real profiler telemetry | 4391.60 | 4401.90 | 0.238489 |
| adaptive_channels, no profiler plugin | 4404.07 | 4400.76 | 0.238182 |

Interpretation:

- The end-to-end telemetry loop works.
- Runtime performance difference in this tiny 1 MiB / single-GPU / socket-only
  setup is negligible.
- The meaningful result here is control-loop correctness, not throughput gain.

## Important Caveat

The tuner's returned `n_channels` changed, but NCCL's own debug line still
showed:

```text
AllReduce: 1048576 Bytes -> Algo RING proto SIMPLE channel{Lo..Hi}={0..3}
```

throughout the short real run.

On this topology, NCCL appears to clamp the effective runtime collective
channels to 4, so the plugin-side returned channel count can change without the
runtime channel range increasing beyond `{0..3}`. That is a NCCL/runtime
constraint, not a failure of the profiler bridge.

## Final Status

Implemented and validated:

- Mixed tuner + profiler export from one `.so`
- Real profiler latency written into `telemetry_map`
- Tuner policy reading profiler-fed telemetry
- NCCL loading both plugin roles from the same library
- Real-vs-control behavior difference in the NCCL run
- Synthetic closed-loop proof in the unit harness

Remaining caveat:

- On the tested single-GPU/socket Phase 4 topology, NCCL's effective runtime
  collective channels remained capped at 4 even when the tuner returned larger
  values.
