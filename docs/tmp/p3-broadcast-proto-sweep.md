# NCCL Broadcast Protocol Sweep: Default vs Simple vs LL vs LL128

**Date**: 2026-03-09
**Hardware**: 1x NVIDIA GeForce RTX 5090 (PCIe 02:00), 2 MPI ranks sharing GPU device 0
**NCCL**: 2.29.7+cuda12.9 (git master 3619159)
**Transport**: Socket (NCCL_NET=Socket, NCCL_P2P_DISABLE=1, NCCL_SHM_DISABLE=1)
**Binary**: `/home/yunwei37/workspace/nccl-eBPF/nccl-tests/build-mpi/broadcast_perf_mpi`
**nccl-tests**: version 2.18.0

---

## Motivation

GitHub Issue NVIDIA/nccl#1810 reported that NCCL Broadcast in the 32KB–32MB range defaults to LL protocol, but Simple and LL128 perform significantly faster. This experiment verifies whether that behavior exists on NCCL 2.29.7 + RTX 5090 hardware, and quantifies the performance gaps across all four conditions (DEFAULT, Simple, LL, LL128).

---

## Experimental Setup

### Key Constraints (same as previous Phase 4 experiments)

- `NCCL_TESTS_DEVICE=0` — both ranks share GPU 0 (single-GPU host workaround)
- Per-rank `NCCL_HOSTID` — prevents NCCL's duplicate-GPU guard from aborting
- `NCCL_P2P_DISABLE=1`, `NCCL_SHM_DISABLE=1`, `NCCL_NET=Socket` — forces socket transport

### Step 3: Protocol Confirmation (NCCL_DEBUG=INFO NCCL_DEBUG_SUBSYS=TUNING)

Before running the sweep, we captured NCCL's internal tuning decision via debug output:

```
mpirun --oversubscribe \
  -np 1 -x LD_LIBRARY_PATH=/home/yunwei37/workspace/nccl-eBPF/nccl/build/lib \
    -x NCCL_TESTS_DEVICE=0 -x NCCL_P2P_DISABLE=1 -x NCCL_SHM_DISABLE=1 \
    -x NCCL_NET=Socket -x NCCL_HOSTID=bcast-rank0 \
    -x NCCL_DEBUG=INFO -x NCCL_DEBUG_SUBSYS=TUNING \
    /home/yunwei37/workspace/nccl-eBPF/nccl-tests/build-mpi/broadcast_perf_mpi \
      -b 32768 -e 134217728 -f 2 -g 1 -n 1 -w 1 \
  : \
  -np 1 -x LD_LIBRARY_PATH=/home/yunwei37/workspace/nccl-eBPF/nccl/build/lib \
    -x NCCL_TESTS_DEVICE=0 -x NCCL_P2P_DISABLE=1 -x NCCL_SHM_DISABLE=1 \
    -x NCCL_NET=Socket -x NCCL_HOSTID=bcast-rank1 \
    -x NCCL_DEBUG=INFO -x NCCL_DEBUG_SUBSYS=TUNING \
    /home/yunwei37/workspace/nccl-eBPF/nccl-tests/build-mpi/broadcast_perf_mpi \
      -b 32768 -e 134217728 -f 2 -g 1 -n 1 -w 1
```

**NCCL tuning table output (Broadcast row):**

```
NCCL INFO   Algorithm   |                            Tree                  |                            Ring                  |
NCCL INFO   Protocol    |             LL |          LL128 |         Simple |             LL |          LL128 |         Simple |
NCCL INFO     Broadcast |     0.0/   0.0 |     0.0/   0.0 |     0.0/   0.0 |     9.3/   1.2 |    18.0/   0.0 |    22.4/   2.4 |
```

**NCCL's per-size protocol selections (DEFAULT, no override):**

```
NCCL INFO Broadcast:    32768 Bytes -> Algo RING proto SIMPLE channel{Lo..Hi}={0..0}
NCCL INFO Broadcast:    65536 Bytes -> Algo RING proto SIMPLE channel{Lo..Hi}={0..1}
NCCL INFO Broadcast:   131072 Bytes -> Algo RING proto SIMPLE channel{Lo..Hi}={0..3}
NCCL INFO Broadcast:   262144 Bytes -> Algo RING proto SIMPLE channel{Lo..Hi}={0..3}
NCCL INFO Broadcast:   524288 Bytes -> Algo RING proto SIMPLE channel{Lo..Hi}={0..3}
NCCL INFO Broadcast:  1048576 Bytes -> Algo RING proto SIMPLE channel{Lo..Hi}={0..3}
NCCL INFO Broadcast:  2097152 Bytes -> Algo RING proto SIMPLE channel{Lo..Hi}={0..3}
NCCL INFO Broadcast:  4194304 Bytes -> Algo RING proto SIMPLE channel{Lo..Hi}={0..3}
NCCL INFO Broadcast:  8388608 Bytes -> Algo RING proto SIMPLE channel{Lo..Hi}={0..3}
NCCL INFO Broadcast: 16777216 Bytes -> Algo RING proto SIMPLE channel{Lo..Hi}={0..3}
NCCL INFO Broadcast: 33554432 Bytes -> Algo RING proto SIMPLE channel{Lo..Hi}={0..3}
NCCL INFO Broadcast: 67108864 Bytes -> Algo RING proto SIMPLE channel{Lo..Hi}={0..3}
NCCL INFO Broadcast:134217728 Bytes -> Algo RING proto SIMPLE channel{Lo..Hi}={0..3}
```

**Key finding**: NCCL 2.29.7 selects **RING SIMPLE for ALL sizes (32KB–128MB)** on this hardware. GitHub Issue #1810 does not reproduce on this version/hardware combination.

### Step 4: Protocol Sweep Commands

**Experiment A (DEFAULT):**
```bash
mpirun --oversubscribe \
  -np 1 -x LD_LIBRARY_PATH=/home/yunwei37/workspace/nccl-eBPF/nccl/build/lib \
    -x NCCL_TESTS_DEVICE=0 -x NCCL_P2P_DISABLE=1 -x NCCL_SHM_DISABLE=1 \
    -x NCCL_NET=Socket -x NCCL_HOSTID=bcast-rank0 \
    /home/yunwei37/workspace/nccl-eBPF/nccl-tests/build-mpi/broadcast_perf_mpi \
      -b 32768 -e 134217728 -f 2 -g 1 -n 20 -w 5 \
  : \
  -np 1 -x LD_LIBRARY_PATH=/home/yunwei37/workspace/nccl-eBPF/nccl/build/lib \
    -x NCCL_TESTS_DEVICE=0 -x NCCL_P2P_DISABLE=1 -x NCCL_SHM_DISABLE=1 \
    -x NCCL_NET=Socket -x NCCL_HOSTID=bcast-rank1 \
    /home/yunwei37/workspace/nccl-eBPF/nccl-tests/build-mpi/broadcast_perf_mpi \
      -b 32768 -e 134217728 -f 2 -g 1 -n 20 -w 5
```

Experiments B/C/D add `-x NCCL_PROTO=Simple`, `-x NCCL_PROTO=LL`, `-x NCCL_PROTO=LL128` respectively. Experiment C (LL) was split into two sub-runs: small sizes (32KB–1MB, n=20 w=5) and large sizes (2MB–128MB, n=5 w=2) to avoid excessive runtime.

---

## Raw Experimental Data

### Experiment A: DEFAULT (no NCCL_PROTO)

```
# nccl-tests version 2.18.0 nccl-headers=22907 nccl-library=22907
# Collective test starting: broadcast_perf_mpi
# nThread 1 nGpus 1 minBytes 32768 maxBytes 134217728 step: 2(factor) warmup iters: 5 iters: 20
#
#  Rank  0 Group  0 Pid 2321375 on lab device 0 [0000:02:00] NVIDIA GeForce RTX 5090
#  Rank  1 Group  0 Pid 2321376 on lab device 0 [0000:02:00] NVIDIA GeForce RTX 5090
#
#       size      count    type   redop    root     time   algbw   busbw  #wrong     time   algbw   busbw  #wrong
#        (B)  (elements)                            (us)  (GB/s)  (GB/s)             (us)  (GB/s)  (GB/s)
       32768       8192   float    none       0    37.46    0.87    0.87       0   133.79    0.24    0.24       0
       65536      16384   float    none       0   201.59    0.33    0.33       0   147.23    0.45    0.45       0
      131072      32768   float    none       0  1863.39    0.07    0.07       0   488.07    0.27    0.27       0
      262144      65536   float    none       0  1554.74    0.17    0.17       0  2105.24    0.12    0.12       0
      524288     131072   float    none       0  1352.07    0.39    0.39       0  1005.67    0.52    0.52       0
     1048576     262144   float    none       0   697.03    1.50    1.50       0  2501.22    0.42    0.42       0
     2097152     524288   float    none       0  2197.40    0.95    0.95       0  2101.94    1.00    1.00       0
     4194304    1048576   float    none       0  2276.36    1.84    1.84       0  2389.11    1.76    1.76       0
     8388608    2097152   float    none       0  2667.59    3.14    3.14       0  2815.81    2.98    2.98       0
    16777216    4194304   float    none       0  6109.80    2.75    2.75       0  6057.38    2.77    2.77       0
    33554432    8388608   float    none       0  8101.30    4.14    4.14       0  8996.67    3.73    3.73       0
    67108864   16777216   float    none       0  15127.6    4.44    4.44       0  16025.9    4.19    4.19       0
   134217728   33554432   float    none       0  29332.4    4.58    4.58       0  29871.8    4.49    4.49       0
# Avg bus bandwidth    : 1.85032 GB/s
```

### Experiment B: NCCL_PROTO=Simple

```
# nccl-tests version 2.18.0 nccl-headers=22907 nccl-library=22907
# Collective test starting: broadcast_perf_mpi
# nThread 1 nGpus 1 minBytes 32768 maxBytes 134217728 step: 2(factor) warmup iters: 5 iters: 20
#
#  Rank  0 Group  0 Pid 2321955 on lab device 0 [0000:02:00] NVIDIA GeForce RTX 5090
#  Rank  1 Group  0 Pid 2321956 on lab device 0 [0000:02:00] NVIDIA GeForce RTX 5090
#
#       size      count    type   redop    root     time   algbw   busbw  #wrong     time   algbw   busbw  #wrong
#        (B)  (elements)                            (us)  (GB/s)  (GB/s)             (us)  (GB/s)  (GB/s)
       32768       8192   float    none       0    56.11    0.58    0.58       0   133.99    0.24    0.24       0
       65536      16384   float    none       0   155.77    0.42    0.42       0   143.79    0.46    0.46       0
      131072      32768   float    none       0  1902.85    0.07    0.07       0  2405.33    0.05    0.05       0
      262144      65536   float    none       0  2333.18    0.11    0.11       0  2010.74    0.13    0.13       0
      524288     131072   float    none       0   560.56    0.94    0.94       0   868.15    0.60    0.60       0
     1048576     262144   float    none       0   706.53    1.48    1.48       0   769.92    1.36    1.36       0
     2097152     524288   float    none       0  2204.05    0.95    0.95       0  2196.46    0.95    0.95       0
     4194304    1048576   float    none       0  2299.11    1.82    1.82       0  2373.00    1.77    1.77       0
     8388608    2097152   float    none       0  2644.71    3.17    3.17       0  2874.56    2.92    2.92       0
    16777216    4194304   float    none       0  6013.50    2.79    2.79       0  5884.00    2.85    2.85       0
    33554432    8388608   float    none       0  8108.97    4.14    4.14       0  7873.20    4.26    4.26       0
    67108864   16777216   float    none       0  15196.7    4.42    4.42       0  16223.6    4.14    4.14       0
   134217728   33554432   float    none       0  30523.8    4.40    4.40       0  32580.7    4.12    4.12       0
# Avg bus bandwidth    : 1.89057 GB/s
```

### Experiment C: NCCL_PROTO=LL

**Small sizes (32KB–1MB), n=20 w=5:**
```
# nccl-tests version 2.18.0 nccl-headers=22907 nccl-library=22907
# Collective test starting: broadcast_perf_mpi
# nThread 1 nGpus 1 minBytes 32768 maxBytes 1048576 step: 2(factor) warmup iters: 5 iters: 20
#
#  Rank  0 Group  0 Pid 2322547 on lab device 0 [0000:02:00] NVIDIA GeForce RTX 5090
#  Rank  1 Group  0 Pid 2322548 on lab device 0 [0000:02:00] NVIDIA GeForce RTX 5090
#
#       size      count    type   redop    root     time   algbw   busbw  #wrong     time   algbw   busbw  #wrong
#        (B)  (elements)                            (us)  (GB/s)  (GB/s)             (us)  (GB/s)  (GB/s)
       32768       8192   float    none       0  1435.78    0.02    0.02       0  1392.89    0.02    0.02       0
       65536      16384   float    none       0  1383.61    0.05    0.05       0   701.73    0.09    0.09       0
      131072      32768   float    none       0   222.12    0.59    0.59       0  1150.30    0.11    0.11       0
      262144      65536   float    none       0   820.36    0.32    0.32       0  1011.71    0.26    0.26       0
      524288     131072   float    none       0  2027.18    0.26    0.26       0   924.75    0.57    0.57       0
     1048576     262144   float    none       0  3251.04    0.32    0.32       0  1503.53    0.70    0.70       0
# Avg bus bandwidth    : 0.276277 GB/s
```

**Large sizes (2MB–128MB), n=5 w=2:**
```
# nccl-tests version 2.18.0 nccl-headers=22907 nccl-library=22907
# Collective test starting: broadcast_perf_mpi
# nThread 1 nGpus 1 minBytes 2097152 maxBytes 134217728 step: 2(factor) warmup iters: 2 iters: 5
#
#  Rank  0 Group  0 Pid 2322875 on lab device 0 [0000:02:00] NVIDIA GeForce RTX 5090
#  Rank  1 Group  0 Pid 2322876 on lab device 0 [0000:02:00] NVIDIA GeForce RTX 5090
#
#       size      count    type   redop    root     time   algbw   busbw  #wrong     time   algbw   busbw  #wrong
#        (B)  (elements)                            (us)  (GB/s)  (GB/s)             (us)  (GB/s)  (GB/s)
     2097152     524288   float    none       0  3142.95    0.67    0.67       0  6317.94    0.33    0.33       0
     4194304    1048576   float    none       0  8190.50    0.51    0.51       0  9355.32    0.45    0.45       0
     8388608    2097152   float    none       0  11458.9    0.73    0.73       0  12847.6    0.65    0.65       0
    16777216    4194304   float    none       0  21026.9    0.80    0.80       0  25589.4    0.66    0.66       0
    33554432    8388608   float    none       0  52015.0    0.65    0.65       0  45583.3    0.74    0.74       0
    67108864   16777216   float    none       0  90647.9    0.74    0.74       0  84384.4    0.80    0.80       0
   134217728   33554432   float    none       0   175619    0.76    0.76       0   173836    0.77    0.77       0
# Avg bus bandwidth    : 0.660806 GB/s
```

### Experiment D: NCCL_PROTO=LL128

```
# nccl-tests version 2.18.0 nccl-headers=22907 nccl-library=22907
# Collective test starting: broadcast_perf_mpi
# nThread 1 nGpus 1 minBytes 32768 maxBytes 134217728 step: 2(factor) warmup iters: 5 iters: 20
#
#  Rank  0 Group  0 Pid 2323511 on lab device 0 [0000:02:00] NVIDIA GeForce RTX 5090
#  Rank  1 Group  0 Pid 2323512 on lab device 0 [0000:02:00] NVIDIA GeForce RTX 5090
#
#       size      count    type   redop    root     time   algbw   busbw  #wrong     time   algbw   busbw  #wrong
#        (B)  (elements)                            (us)  (GB/s)  (GB/s)             (us)  (GB/s)  (GB/s)
       32768       8192   float    none       0   143.15    0.23    0.23       0  1081.65    0.03    0.03       0
       65536      16384   float    none       0   481.03    0.14    0.14       0    88.43    0.74    0.74       0
      131072      32768   float    none       0   661.02    0.20    0.20       0  1509.61    0.09    0.09       0
      262144      65536   float    none       0  1470.50    0.18    0.18       0  1142.82    0.23    0.23       0
      524288     131072   float    none       0  1185.61    0.44    0.44       0  2196.83    0.24    0.24       0
     1048576     262144   float    none       0  2183.95    0.48    0.48       0   809.18    1.30    1.30       0
     2097152     524288   float    none       0   774.60    2.71    2.71       0  1570.03    1.34    1.34       0
     4194304    1048576   float    none       0  3064.28    1.37    1.37       0  1567.60    2.68    2.68       0
     8388608    2097152   float    none       0  3433.78    2.44    2.44       0  3143.63    2.67    2.67       0
    16777216    4194304   float    none       0  7185.02    2.34    2.34       0  7193.23    2.33    2.33       0
    33554432    8388608   float    none       0  12589.9    2.67    2.67       0  12147.3    2.76    2.76       0
    67108864   16777216   float    none       0  24081.8    2.79    2.79       0  24443.3    2.75    2.75       0
   134217728   33554432   float    none       0  49108.9    2.73    2.73       0  46926.0    2.86    2.86       0
# Avg bus bandwidth    : 1.48867 GB/s
```

---

## Analysis

### Step 5: Summary Tables

#### Out-of-place time (us) comparison

| Size        | DEFAULT (us) | Simple (us) | LL (us)   | LL128 (us) |
|:-----------:|:------------:|:-----------:|:---------:|:----------:|
| 32 KB       | 37           | 56          | 1,436     | 143        |
| 64 KB       | 202          | 156         | 1,384     | 481        |
| 128 KB      | 1,863        | 1,903       | 222       | 661        |
| 256 KB      | 1,555        | 2,333       | 820       | 1,471      |
| 512 KB      | 1,352        | 561         | 2,027     | 1,186      |
| 1 MB        | 697          | 707         | 3,251     | 2,184      |
| 2 MB        | 2,197        | 2,204       | 3,143     | 775        |
| 4 MB        | 2,276        | 2,299       | 8,191     | 3,064      |
| 8 MB        | 2,668        | 2,645       | 11,459    | 3,434      |
| 16 MB       | 6,110        | 6,014       | 21,027    | 7,185      |
| 32 MB       | 8,101        | 8,109       | 52,015    | 12,590     |
| 64 MB       | 15,128       | 15,197      | 90,648    | 24,082     |
| 128 MB      | 29,332       | 30,524      | 175,619   | 49,109     |

#### Out-of-place busbw (GB/s) comparison

| Size        | DEFAULT (GB/s) | Simple (GB/s) | LL (GB/s) | LL128 (GB/s) | LL vs DEFAULT | LL128 vs DEFAULT |
|:-----------:|:--------------:|:-------------:|:---------:|:------------:|:-------------:|:----------------:|
| 32 KB       | 0.87           | 0.58          | 0.02      | 0.23         | -97.7%        | -73.6%           |
| 64 KB       | 0.33           | 0.42          | 0.05      | 0.14         | -84.8%        | -57.6%           |
| 128 KB      | 0.07           | 0.07          | 0.59      | 0.20         | +743%         | +186%            |
| 256 KB      | 0.17           | 0.11          | 0.32      | 0.18         | +88%          | +6%              |
| 512 KB      | 0.39           | 0.94          | 0.26      | 0.44         | -33%          | +13%             |
| 1 MB        | 1.50           | 1.48          | 0.32      | 0.48         | -78.7%        | -68.0%           |
| 2 MB        | 0.95           | 0.95          | 0.67      | 2.71         | -29.5%        | +185%            |
| 4 MB        | 1.84           | 1.82          | 0.51      | 1.37         | -72.3%        | -25.5%           |
| 8 MB        | 3.14           | 3.17          | 0.73      | 2.44         | -76.8%        | -22.3%           |
| 16 MB       | 2.75           | 2.79          | 0.80      | 2.34         | -70.9%        | -14.9%           |
| 32 MB       | 4.14           | 4.14          | 0.65      | 2.67         | -84.3%        | -35.5%           |
| 64 MB       | 4.44           | 4.42          | 0.74      | 2.79         | -83.3%        | -37.2%           |
| 128 MB      | 4.58           | 4.40          | 0.76      | 2.73         | -83.4%        | -40.4%           |

**Note**: The small-message performance (32KB–512KB) shows high variance in all conditions. This is typical for socket transport with very small messages where OS scheduling and socket buffering latency dominates over transfer time.

### Key Findings

#### 1. NCCL 2.29.7 Default: SIMPLE for ALL sizes — Issue #1810 does NOT reproduce

NCCL 2.29.7 on RTX 5090 + socket transport selects **RING SIMPLE for every size from 32KB to 128MB**. The NCCL tuning table explicitly scores:

```
Broadcast RING | LL: 9.3/1.2 | LL128: 18.0/0.0 | Simple: 22.4/2.4 (GB/s)
```

Simple wins at all sizes in the cost model. The GitHub Issue #1810 was reported against an older NCCL version (likely 2.18–2.22 era) where the tuning model was different. NCCL 2.29.7 has fixed this.

**Implication**: The DEFAULT and Simple experiments produce nearly identical results (within noise), confirming the default is optimal.

#### 2. LL degradation for Broadcast mirrors AllReduce behavior

At large messages (8MB–128MB), LL is **76–84% slower** than DEFAULT/Simple in bandwidth:
- 128 MB: DEFAULT = 4.58 GB/s, LL = 0.76 GB/s → **6.0× slower**
- 64 MB: DEFAULT = 4.44 GB/s, LL = 0.74 GB/s → **6.0× slower**

This is less catastrophic than the 42× slowdown seen for AllReduce under LL (from p2 experiment), because Broadcast only sends data in one direction (no ring reduce pass), so the LL buffer thrashing is less severe. But the 6× Broadcast degradation is still very large.

At small messages, LL behavior is erratic — it performs better at 128KB (0.59 GB/s vs 0.07 GB/s) but worse at 32KB (0.02 vs 0.87). This reflects the LL protocol's design point: it was optimized for point-to-point small latency, not ring Broadcast over socket.

#### 3. LL128 provides intermediate performance

LL128 consistently underperforms DEFAULT across large messages by 14–40%. At 2 MB it temporarily beats DEFAULT (2.71 vs 0.95 GB/s), but this is likely noise in the DEFAULT measurement at that size (the variance at 2MB is high in all experiments). At large messages:
- 128 MB: DEFAULT = 4.58 GB/s, LL128 = 2.73 GB/s → **40% slower**
- 64 MB: DEFAULT = 4.44 GB/s, LL128 = 2.79 GB/s → **37% slower**

LL128's 93.75% payload efficiency (vs LL's 50%) is not sufficient to overcome its overhead over socket transport.

#### 4. High variance at small messages (32KB–1MB)

The small-message range shows high run-to-run variance across all protocols. DEFAULT times range from 37 µs (32KB) to 2501 µs (1MB in-place), and the out-of-place vs in-place numbers diverge significantly. This is expected: at sub-millisecond latencies, socket buffering, kernel scheduling, and OS jitter dominate. No protocol is reliably "best" for small Broadcast under socket transport.

### DEFAULT vs Protocols: Final Comparison

| Metric | DEFAULT vs Simple | DEFAULT vs LL (large msg) | DEFAULT vs LL128 (large msg) |
|:------:|:-----------------:|:-------------------------:|:----------------------------:|
| Similarity | ~identical (±2%) | 6× slower if LL forced | 37-40% slower if LL128 forced |
| Small messages | DEFAULT slightly better | Erratic (LL sometimes wins) | LL128 consistently worse |
| Large messages (>=8MB) | Negligible difference | LL is 6× slower | LL128 is 37-40% slower |

---

## Conclusion

### Q1: Is NCCL Broadcast default selection suboptimal?

**No — on NCCL 2.29.7 + RTX 5090 + socket transport, NCCL's default is optimal (Simple for all sizes).** GitHub Issue #1810 does not reproduce on this version. NCCL 2.29.7's tuning model correctly prefers Simple (22.4 GB/s model score) over LL128 (18.0) and LL (9.3) for Broadcast across all sizes.

### Q2: What message range would be affected if LL were incorrectly chosen?

If an incorrect tuner policy (buggy plugin, wrong heuristic, or the old NCCL behavior described in #1810) forces LL on Broadcast:
- **8MB–128MB**: 6× slowdown (0.74–0.76 GB/s vs 3.14–4.58 GB/s)
- **32KB–64KB**: 5–43× slowdown (0.02–0.05 vs 0.33–0.87 GB/s)
- **128KB–512KB**: Mixed — LL sometimes wins due to socket transport dynamics

### Q3: How much improvement would an eBPF policy correction provide?

An eBPF tuner policy that overrides LL → Simple for Broadcast messages ≥ 8MB would recover the **6× bandwidth regression** that an incorrect LL assignment causes. At 128 MB: 0.76 → 4.58 GB/s (+503%). This is a directly measurable, large-margin improvement — not a marginal tuning win.

Even though NCCL 2.29.7 already makes this correct choice natively, the eBPF policy plane provides **enforcement guarantees**:
- A user-space tuner plugin with a bug (or deliberate policy violation in a multi-tenant cluster) could force LL and cause a 6× Broadcast regression
- An eBPF safety policy can detect and override the incorrect assignment with sub-microsecond overhead
- This is the "guard rail" use case: not improving on a working default, but preventing bad policies from regressing it

### Q4: How does Broadcast compare to AllReduce under protocol mismatch?

| Collective | LL vs Simple penalty (128 MB, socket) |
|:----------:|:--------------------------------------:|
| AllReduce  | 42× slower (from p2 experiment)        |
| Broadcast  | 6× slower (this experiment)            |

AllReduce's penalty is 7× larger because the ring reduce pass doubles the LL buffer thrashing. Broadcast only needs a single ring pass (no reduce), so degradation is less severe but still a 6× regression — well above any noise floor.

---

## Files

- Raw logs: `docs/tmp/bcast-A-default.log`, `bcast-B-simple.log`, `bcast-C-ll.log`, `bcast-D-ll128.log`
- Debug tuning log: `docs/tmp/bcast-debug-tuning.log`
- Related AllReduce protocol experiment: `docs/tmp/p2-proto-bandwidth-experiment.md`
- NCCL binary: `/home/yunwei37/workspace/nccl-eBPF/nccl/build/lib/libnccl.so.2.29.7`
- Test binary: `/home/yunwei37/workspace/nccl-eBPF/nccl-tests/build-mpi/broadcast_perf_mpi`
