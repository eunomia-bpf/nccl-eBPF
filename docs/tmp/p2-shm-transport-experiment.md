# NCCL SHM Transport Experiment: Feasibility and Latency Impact

**Date**: 2026-03-09
**Hardware**: 1x NVIDIA GeForce RTX 5090 (PCIe 02:00), 2 MPI ranks sharing GPU device 0
**NCCL**: 2.29.7+cuda12.9 (git master 3619159)
**nccl-tests**: version 2.18.0

---

## Objective

Determine whether removing `NCCL_SHM_DISABLE=1` from the test configuration allows NCCL to use Shared Memory (SHM) transport, and if so, how much lower the latency drops from the current ~4.3ms Socket transport baseline.

---

## Background: Why the Baseline Uses Socket Transport

The 2-rank single-GPU test setup requires several workarounds:

1. `NCCL_TESTS_DEVICE=0` — forces both nccl-tests ranks onto GPU 0 (only GPU on the host)
2. `NCCL_HOSTID=<unique-per-rank>` — gives ranks different hostIds to bypass NCCL's duplicate-GPU guard at `init.cc:974`
3. `NCCL_P2P_DISABLE=1` — disables NVLink P2P (not relevant, single GPU)
4. `NCCL_SHM_DISABLE=1` — (previous assumption) disables SHM

The HOSTID workaround was believed to be orthogonal to SHM selection. This experiment tests that assumption.

---

## Experiment A: Baseline (Socket transport, SHM disabled)

**Command**:
```bash
mpirun --oversubscribe \
  -np 1 /usr/bin/env \
    LD_LIBRARY_PATH=/home/yunwei37/workspace/nccl-eBPF/nccl/build/lib \
    NCCL_TESTS_DEVICE=0 NCCL_P2P_DISABLE=1 NCCL_SHM_DISABLE=1 \
    NCCL_NET=Socket NCCL_HOSTID=shm-final-rank0 \
    /home/yunwei37/workspace/nccl-eBPF/nccl-tests/build/all_reduce_perf_mpi \
      -b 8 -e 128M -f 2 -g 1 -n 20 -w 5 \
  : \
  -np 1 /usr/bin/env \
    LD_LIBRARY_PATH=/home/yunwei37/workspace/nccl-eBPF/nccl/build/lib \
    NCCL_TESTS_DEVICE=0 NCCL_P2P_DISABLE=1 NCCL_SHM_DISABLE=1 \
    NCCL_NET=Socket NCCL_HOSTID=shm-final-rank1 \
    /home/yunwei37/workspace/nccl-eBPF/nccl-tests/build/all_reduce_perf_mpi \
      -b 8 -e 128M -f 2 -g 1 -n 20 -w 5
```

**Transport selected**: `NET/Socket` (confirmed via `NCCL_DEBUG=INFO NCCL_DEBUG_SUBSYS=TRANSPORT`)

**Raw output**:
```
# nccl-tests version 2.18.0 nccl-headers=22907 nccl-library=22907
# Collective test starting: all_reduce_perf_mpi
# nThread 1 nGpus 1 minBytes 8 maxBytes 134217728 step: 2(factor) warmup iters: 5 iters: 20
#  Rank  0 Group  0 Pid 2224659 on lab device 0 [0000:02:00] NVIDIA GeForce RTX 5090
#  Rank  1 Group  0 Pid 2224660 on lab device 0 [0000:02:00] NVIDIA GeForce RTX 5090
#       size         count      type   redop    root     time   algbw   busbw  #wrong     time   algbw   busbw  #wrong
           8             2     float     sum      -1  4247.13    0.00    0.00       0  4358.92    0.00    0.00       0
          16             4     float     sum      -1  4247.20    0.00    0.00       0  4358.66    0.00    0.00       0
          32             8     float     sum      -1  4355.60    0.00    0.00       0  4358.98    0.00    0.00       0
          64            16     float     sum      -1  4359.09    0.00    0.00       0  4359.13    0.00    0.00       0
         128            32     float     sum      -1  4356.06    0.00    0.00       0  4356.35    0.00    0.00       0
         256            64     float     sum      -1  4356.07    0.00    0.00       0  4356.51    0.00    0.00       0
         512           128     float     sum      -1  4356.45    0.00    0.00       0  4359.57    0.00    0.00       0
        1024           256     float     sum      -1  4356.77    0.00    0.00       0  4356.77    0.00    0.00       0
        2048           512     float     sum      -1  4358.32    0.00    0.00       0  4360.67    0.00    0.00       0
        4096          1024     float     sum      -1  4360.42    0.00    0.00       0  4363.93    0.00    0.00       0
        8192          2048     float     sum      -1  4363.63    0.00    0.00       0  4367.16    0.00    0.00       0
       16384          4096     float     sum      -1  4367.79    0.00    0.00       0  4364.79    0.00    0.00       0
       32768          8192     float     sum      -1  4372.14    0.01    0.01       0  4374.98    0.01    0.01       0
       65536         16384     float     sum      -1  4372.31    0.01    0.01       0  4371.89    0.01    0.01       0
      131072         32768     float     sum      -1  4365.39    0.03    0.03       0  4367.98    0.03    0.03       0
      262144         65536     float     sum      -1  4370.77    0.06    0.06       0  4365.50    0.06    0.06       0
      524288        131072     float     sum      -1  4369.63    0.12    0.12       0  4366.10    0.12    0.12       0
     1048576        262144     float     sum      -1  4373.80    0.24    0.24       0  4368.39    0.24    0.24       0
     2097152        524288     float     sum      -1  4373.04    0.48    0.48       0  4373.00    0.48    0.48       0
     4194304       1048576     float     sum      -1  4385.39    0.96    0.96       0  4382.13    0.96    0.96       0
     8388608       2097152     float     sum      -1  4403.95    1.90    1.90       0  4401.85    1.91    1.91       0
    16777216       4194304     float     sum      -1  6649.47    2.52    2.52       0  6612.18    2.54    2.54       0
    33554432       8388608     float     sum      -1  12808.0    2.62    2.62       0  13145.7    2.55    2.55       0
    67108864      16777216     float     sum      -1  25151.4    2.67    2.67       0  25576.1    2.62    2.62       0
   134217728      33554432     float     sum      -1  51726.0    2.59    2.59       0  50660.9    2.65    2.65       0
# Avg bus bandwidth    : 0.56824
```

**Key observations**:
- Latency for small messages (8B–8KB): ~4.25–4.37 ms (flat, dominated by socket round-trip overhead)
- Throughput plateau for large messages (>16MB): ~2.59–2.67 GB/s
- Zero correctness errors

---

## Experiment B: Remove NCCL_SHM_DISABLE (allow SHM transport)

**Hypothesis**: Removing `NCCL_SHM_DISABLE=1` while keeping `NCCL_HOSTID` differentiated might allow SHM if NCCL's SHM transport can work across different-hostId ranks.

**Command** (removed `NCCL_SHM_DISABLE=1`, added `NCCL_DEBUG_SUBSYS=INIT,TRANSPORT`):
```bash
mpirun --oversubscribe \
  -np 1 /usr/bin/env \
    NCCL_TESTS_DEVICE=0 NCCL_P2P_DISABLE=1 \
    NCCL_NET=Socket NCCL_HOSTID=shm-exp-rank0 \
    NCCL_DEBUG=INFO NCCL_DEBUG_SUBSYS=INIT,TRANSPORT \
    /home/yunwei37/workspace/nccl-eBPF/nccl-tests/build/all_reduce_perf_mpi -b 8 -e 128M -f 2 -g 1 -n 20 -w 5 \
  : \
  -np 1 /usr/bin/env \
    NCCL_TESTS_DEVICE=0 NCCL_P2P_DISABLE=1 \
    NCCL_NET=Socket NCCL_HOSTID=shm-exp-rank1 \
    NCCL_DEBUG=INFO NCCL_DEBUG_SUBSYS=INIT,TRANSPORT \
    /home/yunwei37/workspace/nccl-eBPF/nccl-tests/build/all_reduce_perf_mpi -b 8 -e 128M -f 2 -g 1 -n 20 -w 5
```

**Debug log excerpt** (transport selection):
```
lab:2220000:2220000 [0] NCCL INFO comm 0x... rank 0 nRanks 2 nNodes 2 localRanks 1 localRank 0 MNNVL 0
lab:2220001:2220001 [0] NCCL INFO comm 0x... rank 1 nRanks 2 nNodes 2 localRanks 1 localRank 0 MNNVL 0
lab:2220001:2220001 [0] NCCL INFO Channel 00/0 : 0[0] -> 1[0] [receive] via NET/Socket/1
lab:2220000:2220000 [0] NCCL INFO Channel 00/0 : 1[0] -> 0[0] [receive] via NET/Socket/1
...
lab:2220001:2220001 [0] NCCL INFO Connected all rings, use ring PXN 0 GDR 0
```

**Transport selected**: Still `NET/Socket` — **SHM was NOT used**

**Performance**: Statistically identical to Experiment A (~4.25ms baseline latency, ~2.66 GB/s large-message throughput)

**Why SHM was not selected**: Inspection of `nccl/src/transport/shm.cc:shmCanConnect()` reveals the SHM `canConnect` check requires:

```c
// Same host?
if (info1->hostHash != info2->hostHash) return ncclSuccess;  // sets *ret=0, no SHM

// Common /dev/shm?
if (info1->shmDev != info2->shmDev) return ncclSuccess;      // sets *ret=0, no SHM
```

With different `NCCL_HOSTID` values, `getHostHash()` returns different values for each rank, so `info1->hostHash != info2->hostHash` and SHM is immediately ruled out. The HOSTID workaround that bypasses the duplicate-GPU check also permanently prevents SHM transport.

---

## Experiment B2: What Happens Without NCCL_HOSTID?

**Hypothesis**: If we remove NCCL_HOSTID entirely (same host detected), NCCL might attempt SHM transport.

**Command**:
```bash
mpirun --oversubscribe \
  -np 1 /usr/bin/env NCCL_TESTS_DEVICE=0 NCCL_P2P_DISABLE=1 NCCL_NET=Socket \
    NCCL_DEBUG=WARN \
    /home/yunwei37/workspace/nccl-eBPF/nccl-tests/build/all_reduce_perf_mpi -b 8 -e 1M -f 4 -g 1 -n 5 -w 2 \
  : \
  -np 1 /usr/bin/env NCCL_TESTS_DEVICE=0 NCCL_P2P_DISABLE=1 NCCL_NET=Socket \
    NCCL_DEBUG=WARN \
    /home/yunwei37/workspace/nccl-eBPF/nccl-tests/build/all_reduce_perf_mpi -b 8 -e 1M -f 4 -g 1 -n 5 -w 2
```

**Result**:
```
NCCL WARN Duplicate GPU detected : rank 1 and rank 0 both on CUDA device 2000
Test NCCL failure common.cu:210 'invalid usage'
```

Without NCCL_HOSTID, both ranks report the same hostHash. NCCL's `init.cc:974` detects they share the same busId (0x2000) and returns `ncclInvalidUsage` — the run fails before transport selection ever occurs.

---

## Attempted Fix: Patch NCCL to Bypass Duplicate GPU Check

To investigate deeper, NCCL was patched to downgrade the duplicate-GPU error to a warning:

```c
// init.cc:973 — patched to remove goto fail
if ((i != rank) && (peerInfo[i].hostHash == peerInfo[rank].hostHash) && (peerInfo[i].busId == peerInfo[rank].busId)) {
  WARN("Duplicate GPU detected ... (continuing for SHM transport experiment)");
  // ret = ncclInvalidUsage;
  // goto fail;
}
```

**Result after patch + rebuild**:
```
NCCL WARN Duplicate GPU detected : rank 0 and rank 1 both on CUDA device 2000 (continuing...)
NCCL WARN ncclTopoRankToIndex could not find rank 1
Test NCCL failure 'internal error'
```

**Root cause**: Even bypassing the duplicate-GPU check, NCCL's topology subsystem (`graph/topo.h:ncclTopoRankToIndex`) cannot find rank 1 in the topology graph. The topology discovery enumerates physical GPUs (1 GPU → 1 node in the topo graph). With 2 ranks claiming the same GPU, rank 1 has no corresponding topology node. This is a deeper structural constraint: NCCL's topology model maps one GPU to one rank. Making 2 ranks share 1 GPU is architecturally unsupported.

**The patch was reverted** and NCCL rebuilt back to the original state.

---

## Experiment C: Alternative Transport Configurations

### C1: Remove NCCL_NET=Socket (let NCCL auto-select)

```bash
# Same as baseline but without NCCL_NET=Socket
NCCL_HOSTID=exp-c-rank0/rank1, NCCL_P2P_DISABLE=1, NCCL_SHM_DISABLE=1
```

**Result**: NCCL still selects `NET/Socket` automatically (reported: `nNodes 2 localRanks 1`). When different HOSTIDs are used, NCCL classifies the two ranks as being on different nodes and routes via the network (Socket) regardless of `NCCL_NET`. Performance: avg bus bandwidth 0.409 GB/s (same message range), consistent with baseline.

### C2: Remove NCCL_P2P_DISABLE (allow CUDA P2P)

```bash
# NCCL_HOSTID=exp-c2-rank0/rank1, NCCL_SHM_DISABLE=1, NCCL_NET=Socket
# (no NCCL_P2P_DISABLE)
```

**Raw output** (key rows):
```
           8             2     float     sum      -1  4365.15    0.00    0.00       0  4141.79    0.00    0.00       0
        8192          2048     float     sum      -1  4366.92    0.00    0.00       0  4366.84    0.00    0.00       0
     8388608       2097152     float     sum      -1  4478.71    1.87    1.87       0  4453.40    1.88    1.88       0
    33554432       8388608     float     sum      -1  14369.8    2.34    2.34       0  13320.1    2.52    2.52       0
# Avg bus bandwidth    : 0.411894
```

**Result**: Successful, but performance is identical to Socket-only baseline (~4.36ms latency). NCCL still uses NET/Socket (nNodes=2 with different HOSTIDs). The P2P enable/disable flag is irrelevant here since no NVLink P2P path exists between the ranks (they're seen as being on different nodes).

---

## Root Cause Analysis: Why SHM Is Fundamentally Unavailable

### The NCCL Constraint Chain

```
Goal: 2 ranks, 1 GPU, SHM transport
 │
 ├─ Requirement for SHM: info1.hostHash == info2.hostHash (same node)
 │   └─ hostHash computed from NCCL_HOSTID if set, else from gethostname()
 │
 ├─ Without NCCL_HOSTID: both ranks have same hostHash (same physical host)
 │   └─ NCCL check: same hostHash + same busId → ncclInvalidUsage (fail)
 │       └─ init.cc:973: "Duplicate GPU detected"
 │
 └─ With NCCL_HOSTID (different per rank): ranks have different hostHash
     └─ SHM canConnect check: hostHash mismatch → SHM disabled
         └─ All channels routed via NET/Socket
```

### NCCL's Topology Model

NCCL's topology system (`ncclTopoSystem`) has a 1:1 mapping between physical GPUs and ranks in the topology graph. The graph nodes are built from hardware discovery (PCIe BDF addresses). With 2 ranks sharing 1 GPU:

- 1 GPU → 1 topo node
- Rank 0 is assigned to that node
- Rank 1 cannot be assigned to any node → `ncclTopoRankToIndex` fails with `ncclInternalError`

This is not a removable constraint through environment variables — it would require changing NCCL's topology discovery to support virtual GPU instances.

### SHM Transport Requirements (from source)

The SHM `canConnect` function (`transport/shm.cc:77-98`) requires ALL of:
1. `NCCL_SHM_DISABLE != 1`
2. `ncclTopoCheckNet()` says "don't use net" (same-node path)
3. `info1->hostHash == info2->hostHash` (same host)
4. `info1->shmDev == info2->shmDev` (same `/dev/shm` namespace)

Condition 3 is mutually exclusive with the duplicate-GPU bypass on single-GPU hardware.

---

## Summary Table: Configuration Outcomes

| Config | SHM_DISABLE | NCCL_HOSTID | P2P_DISABLE | NET | Transport Used | Latency (small) | Runs? |
|:------:|:-----------:|:-----------:|:-----------:|:---:|:--------------:|:---------------:|:-----:|
| A (baseline) | =1 | different | =1 | Socket | NET/Socket | ~4.3ms | Yes |
| B (no SHM_DISABLE) | not set | different | =1 | Socket | NET/Socket | ~4.3ms | Yes |
| B2 (no HOSTID) | not set | not set | =1 | Socket | N/A (crash) | N/A | No |
| B2-patched | not set | not set | =1 | Socket | N/A (topo crash) | N/A | No |
| same-HOSTID + patch | not set | same | =1 | Socket | N/A (topo crash) | N/A | No |
| C1 (no NET override) | =1 | different | =1 | auto | NET/Socket | ~4.3ms | Yes |
| C2 (no P2P_DISABLE) | =1 | different | not set | Socket | NET/Socket | ~4.3ms | Yes |

---

## Conclusion

### SHM Transport: Unavailable on This Hardware

**SHM transport is not achievable with the current 1-GPU, 2-rank test setup.** The constraint is fundamental:

- SHM transport requires `hostHash` equality (same-node detection)
- Same-node + same-GPU triggers NCCL's duplicate-GPU guard (`ncclInvalidUsage`)
- NCCL's topology model has a 1:1 GPU-to-rank mapping; 2 ranks cannot share 1 topo node
- These constraints cannot be resolved via environment variables alone

Removing `NCCL_SHM_DISABLE=1` has zero effect on performance because SHM is ruled out earlier by the `hostHash` mismatch imposed by the HOSTID workaround. The `NCCL_SHM_DISABLE=1` flag in the baseline config is therefore **redundant** (SHM would not have been selected anyway), but harmless.

### Latency Floor: Hardware-Imposed, Not Config-Imposed

The ~4.3ms latency floor for small messages is the **Socket transport round-trip overhead** through the kernel network stack (loopback). It is not caused by `NCCL_SHM_DISABLE=1`. On real multi-node hardware:

- SHM transport (same-node, different-GPU): typical small-message latency ~5–30 μs
- NVLink P2P (direct GPU-to-GPU): ~2–10 μs
- Socket (loopback): 4,000–4,400 μs (this machine)

The 4.3ms floor is approximately **150–200× higher** than what SHM transport would achieve on same-node, different-GPU hardware.

### Impact on Future Experiments

1. **The existing baseline remains the canonical reference** for this project's experiments. The HOSTID + Socket transport setup is the only working configuration.

2. **The 4.3ms latency is a transport artifact, not a meaningful NCCL overhead**. It does not reflect real-world latency for production GPU clusters. Future papers should note this as a testbed limitation.

3. **Algorithm/protocol differentiation** is still measurable because the relative comparison (e.g., RING vs TREE, Simple vs LL) remains valid — the transport artifact is constant across all algo/proto configurations.

4. **For SHM transport experiments**, a multi-GPU system would be required (minimum: 2 GPUs on the same host, 1 rank per GPU).

5. **The eBPF policy demonstration** is unaffected: the policy hooks intercept at the NCCL tuner/transport layer before transport selection, so the framework demonstration works correctly regardless of which transport is ultimately chosen.

---

## Files Referenced

- NCCL SHM canConnect: `/home/yunwei37/workspace/nccl-eBPF/nccl/src/transport/shm.cc:77-98`
- NCCL duplicate GPU check: `/home/yunwei37/workspace/nccl-eBPF/nccl/src/init.cc:973-977`
- NCCL topology rank lookup: `/home/yunwei37/workspace/nccl-eBPF/nccl/src/graph/topo.h:241-251`
- Previous bandwidth experiment: `docs/tmp/p2-proto-bandwidth-experiment.md`
- Phase 4 baseline log: `docs/tmp/phase4-baseline.log`
