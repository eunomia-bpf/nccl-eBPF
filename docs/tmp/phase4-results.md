# Phase 4 Results: Single-GPU 2-Rank NCCL Integration

## Outcome

`getCollInfo()` was successfully exercised on a single RTX 5090 by running two MPI
ranks on GPU 0 with:

- `NCCL_TESTS_DEVICE=0` to bypass `nccl-tests` local GPU assignment.
- Per-rank `NCCL_HOSTID` values to bypass NCCL's duplicate-GPU guard.
- `NCCL_P2P_DISABLE=1`, `NCCL_SHM_DISABLE=1`, and `NCCL_NET=Socket` to force
  the socket transport.

This produced a real `nranks=2` communicator and non-zero plugin call counts:

- `size_aware_v2`: `finalize calls=495` on each rank
- `noop`: `finalize calls=495` on each rank

## What Failed First

1. Existing `nccl-tests/build/all_reduce_perf` was built without MPI support.
   Under `mpirun`, it launched two unrelated rank-0 jobs and never formed a
   2-rank communicator.

2. MPI-enabled `all_reduce_perf_mpi` without `NCCL_TESTS_DEVICE=0` failed in
   `nccl-tests` with:
   `Invalid number of GPUs: 2 requested but only 1 were found.`

3. MPI-enabled `all_reduce_perf_mpi` with `NCCL_TESTS_DEVICE=0` but without
   `NCCL_HOSTID` failed in NCCL init with:
   `Duplicate GPU detected : rank 0 and rank 1 both on CUDA device 2000`

## What Worked

### Build

Built an MPI-enabled test binary:

```bash
make -j4 MPI=1 NAME_SUFFIX=_mpi \
  MPI_HOME=/usr/lib/x86_64-linux-gnu/openmpi \
  NCCL_HOME=/home/yunwei37/workspace/nccl-eBPF/nccl/build \
  CUDA_HOME=/usr/local/cuda-12.9 \
  ../build/all_reduce_perf_mpi
```

### Working Command

```bash
mpirun --oversubscribe \
  -np 1 /usr/bin/env \
    LD_LIBRARY_PATH=/home/yunwei37/workspace/nccl-eBPF/nccl/build/lib \
    NCCL_TESTS_DEVICE=0 \
    NCCL_DEBUG=INFO \
    NCCL_P2P_DISABLE=1 \
    NCCL_SHM_DISABLE=1 \
    NCCL_NET=Socket \
    NCCL_HOSTID=phase4-rank0 \
    NCCL_TUNER_PLUGIN=/home/yunwei37/workspace/nccl-eBPF/src/nccl-policy-plugin/build-bpftime-migration-prebuilt/libnccl-policy.so \
    NCCL_POLICY_BPF_PATH=/home/yunwei37/workspace/nccl-eBPF/src/nccl-policy-plugin/build-bpftime-migration-prebuilt/ebpf-policies/size_aware_v2.bpf.o \
    NCCL_POLICY_VERIFY_MODE=strict \
    /home/yunwei37/workspace/nccl-eBPF/nccl-tests/build/all_reduce_perf_mpi \
      -b 1K -e 1M -f 2 -g 1 -n 20 \
  : \
  -np 1 /usr/bin/env \
    LD_LIBRARY_PATH=/home/yunwei37/workspace/nccl-eBPF/nccl/build/lib \
    NCCL_TESTS_DEVICE=0 \
    NCCL_DEBUG=INFO \
    NCCL_P2P_DISABLE=1 \
    NCCL_SHM_DISABLE=1 \
    NCCL_NET=Socket \
    NCCL_HOSTID=phase4-rank1 \
    NCCL_TUNER_PLUGIN=/home/yunwei37/workspace/nccl-eBPF/src/nccl-policy-plugin/build-bpftime-migration-prebuilt/libnccl-policy.so \
    NCCL_POLICY_BPF_PATH=/home/yunwei37/workspace/nccl-eBPF/src/nccl-policy-plugin/build-bpftime-migration-prebuilt/ebpf-policies/size_aware_v2.bpf.o \
    NCCL_POLICY_VERIFY_MODE=strict \
    /home/yunwei37/workspace/nccl-eBPF/nccl-tests/build/all_reduce_perf_mpi \
      -b 1K -e 1M -f 2 -g 1 -n 20
```

## NCCL / Plugin Evidence

From `docs/tmp/phase4-step2-hostid-socket-mpi-device0-fixed.log`:

- NCCL formed a true 2-rank communicator:
  `comm ... rank 0 nRanks 2 nNodes 2`
- External tuner plugin was selected:
  `TUNER/Plugin: Using eBPFPolicy (v5)`
- Plugin initialized in multi-rank mode:
  `initialized for 2 ranks across 2 nodes`
- `getCollInfo()` executed immediately:
  `call=1 bytes=1024 action=64458195456 latency_ns=454 channels=2 aggr=2`
- Final call count was non-zero:
  `finalize calls=495 ...`

## Plugin Bug Found and Fixed

Once the real 2-rank path was active, the plugin initially crashed in
`pluginGetCollInfo()` on the first collective.

Root cause:

- NCCL passes the cost table as a contiguous `[algo][proto]` array cast to
  `float**`.
- The plugin treated it as a true pointer-to-pointer and dereferenced an invalid
  address when applying the selected `(algo, proto)` override.

Fix applied in `src/nccl-policy-plugin/plugin.cpp`:

- Reinterpret `coll_cost_table` as
  `float (*)[NCCL_NUM_PROTOCOLS]`
- Read/write `table[algo][proto]` through that flat array view

## Performance Comparison

All runs used the same single-GPU / socket-only / `NCCL_HOSTID` setup.

### Aggregate

| Configuration | Avg bus bandwidth |
| --- | ---: |
| Baseline (no plugin) | 0.0436174 |
| `noop` policy | 0.0435451 |
| `size_aware_v2` policy | 0.0381551 |

### Representative Out-of-Place Latency (us)

| Size | Baseline | `noop` | `size_aware_v2` |
| --- | ---: | ---: | ---: |
| 1 KiB | 4359.24 | 4359.55 | 4365.11 |
| 4 KiB | 4360.22 | 4363.11 | 4365.21 |
| 1 MiB | 4371.70 | 4382.46 | 4366.95 |

### Plugin Overhead / Behavior

- `noop` policy stayed within noise of baseline on this transport-constrained
  setup.
- `size_aware_v2` changed decisions immediately:
  first call used `channels=2`, later calls used `channels=4`.
- `size_aware_v2` reduced aggregate bus bandwidth in this socket-only
  single-GPU emulation, which is plausible because the policy was not tuned for
  this artificial two-host-on-one-GPU topology.

## Logs

- Non-MPI `nccl-tests` under `mpirun`: `docs/tmp/phase4-step2-socket.log`
- MPI build without `NCCL_TESTS_DEVICE=0`: `docs/tmp/phase4-step2-socket-mpi.log`
- MPI build with `NCCL_TESTS_DEVICE=0`, no `NCCL_HOSTID`: `docs/tmp/phase4-step2-socket-mpi-device0.log`
- Successful `size_aware_v2` run: `docs/tmp/phase4-step2-hostid-socket-mpi-device0-fixed.log`
- Baseline comparison: `docs/tmp/phase4-baseline.log`
- `noop` comparison: `docs/tmp/phase4-noop.log`

## Conclusion

Phase 4 succeeded.

The paper claim can now be backed by a real NCCL integration run, not just the
CPU-only harness:

- a single physical GPU can be used to force a 2-rank NCCL path,
- NCCL does call `getCollInfo()` in that configuration,
- the plugin sees and changes live collective decisions,
- and the end-to-end path exposed a real integration bug that is now fixed.
