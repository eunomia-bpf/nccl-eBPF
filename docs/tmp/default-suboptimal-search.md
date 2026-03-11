# NCCL DEFAULT Suboptimality Search

**Date**: 2026-03-10
**Hardware**: 1x NVIDIA GeForce RTX 5090, 2 MPI ranks sharing GPU 0
**NCCL library path**: `/home/yunwei37/workspace/nccl-eBPF/nccl/build/lib`
**nccl-tests path**: `/home/yunwei37/workspace/nccl-eBPF/nccl-tests/build`
**Method**: user-specified experiments executed with Open MPI oversubscribe mode.
**Common transport settings unless noted**: `NCCL_TESTS_DEVICE=0`, `NCCL_P2P_DISABLE=1`, `NCCL_NET=Socket`.

All verdicts use out-of-place time (`time`, us) as the primary metric because `nccl-tests` prints `busbw` with only two decimals, which can create rounding artifacts at small sizes. For each alternative, `% difference vs DEFAULT` is negative when the alternative is faster. `>2σ?` is computed as `|alt_mean - default_mean| > 2 * sqrt(default_std^2 + alt_std^2)`. `DEFAULT genuinely slower?` only applies when the alternative is faster and the difference exceeds that 2σ bar.

## Experiment 1: Broadcast Protocol Sweep

Primary comparison metric for verdicts: out-of-place time (us). Lower is better. Reported busbw remains as a secondary view.

### DEFAULT Tuning Selections

| Size | Algo | Proto | Channels | Stable choice? |
| --- | --- | --- | --- | --- |
| 1KB | RING | LL | 0..0 | Yes |
| 2KB | RING | LL | 0..0 | Yes |
| 4KB | RING | LL | 0..0 | Yes |
| 8KB | RING | LL | 0..0 | Yes |
| 16KB | RING | LL | 0..1 | Yes |
| 32KB | RING | SIMPLE | 0..0 | Yes |
| 64KB | RING | SIMPLE | 0..1 | Yes |
| 128KB | RING | SIMPLE | 0..3 | Yes |
| 256KB | RING | SIMPLE | 0..3 | Yes |
| 512KB | RING | SIMPLE | 0..3 | Yes |
| 1MB | RING | SIMPLE | 0..3 | Yes |
| 2MB | RING | SIMPLE | 0..3 | Yes |
| 4MB | RING | SIMPLE | 0..3 | Yes |
| 8MB | RING | SIMPLE | 0..3 | Yes |
| 16MB | RING | SIMPLE | 0..3 | Yes |
| 32MB | RING | SIMPLE | 0..3 | Yes |
| 64MB | RING | SIMPLE | 0..3 | Yes |
| 128MB | RING | SIMPLE | 0..3 | Yes |

This table comes from the dedicated `NCCL_DEBUG=INFO NCCL_DEBUG_SUBSYS=TUNING` step.

### Summary Table: out-of-place time

| Size | DEFAULT time (us) | Simple time (us) | LL time (us) | Simple vs DEFAULT | Simple >2σ? | LL vs DEFAULT | LL >2σ? | DEFAULT genuinely slower? |
| --- | --- | --- | --- | --- | --- | --- | --- | --- |
| 1KB | 98.96±45.08 | 133.96±25.85 | 188.10±0.14 | +35.37% | No | +90.07% | No | No |
| 2KB | 55.97±1.09 | 59.64±0.08 | 56.07±1.27 | +6.56% | Yes | +0.18% | No | No |
| 4KB | 58.30±0.87 | 59.79±0.10 | 243.75±254.17 | +2.56% | No | +318.11% | No | No |
| 8KB | 138.05±136.71 | 60.19±0.05 | 391.94±576.55 | -56.40% | No | +183.91% | No | No |
| 16KB | 335.64±413.84 | 63.64±0.10 | 502.54±266.12 | -81.04% | No | +49.73% | No | No |
| 32KB | 185.12±25.99 | 126.22±70.85 | 714.92±438.76 | -31.82% | No | +286.20% | No | No |
| 64KB | 317.11±211.70 | 395.95±463.86 | 529.80±288.61 | +24.86% | No | +67.07% | No | No |
| 128KB | 621.08±476.67 | 710.57±208.68 | 501.44±269.30 | +14.41% | No | -19.26% | No | No |
| 256KB | 892.78±59.47 | 572.01±281.68 | 754.13±49.84 | -35.93% | No | -15.53% | No | No |
| 512KB | 389.70±146.59 | 509.26±257.37 | 927.67±135.88 | +30.68% | No | +138.05% | Yes | No |
| 1MB | 428.97±91.39 | 774.07±309.46 | 1795.69±462.94 | +80.45% | No | +318.61% | Yes | No |
| 2MB | 1967.66±1646.22 | 1226.19±334.84 | 3010.89±92.10 | -37.68% | No | +53.02% | No | No |
| 4MB | 1412.77±131.28 | 1298.37±95.73 | 6197.21±316.11 | -8.10% | No | +338.66% | Yes | No |
| 8MB | 2479.67±240.70 | 2514.49±223.55 | 12529.63±288.99 | +1.40% | No | +405.30% | Yes | No |
| 16MB | 4769.48±132.98 | 4988.83±169.37 | 24474.17±129.47 | +4.60% | No | +413.14% | Yes | No |
| 32MB | 9009.06±45.62 | 8983.78±159.44 | 48671.57±284.59 | -0.28% | No | +440.25% | Yes | No |
| 64MB | 17829.67±201.70 | 17933.00±143.80 | 97991.37±784.06 | +0.58% | No | +449.60% | Yes | No |
| 128MB | 35489.97±139.56 | 35580.13±202.99 | 195776.33±1069.17 | +0.25% | No | +451.64% | Yes | No |

### Secondary Table: out-of-place busbw

| Size | DEFAULT busbw (GB/s) | Simple busbw (GB/s) | LL busbw (GB/s) | Simple vs DEFAULT | LL vs DEFAULT |
| --- | --- | --- | --- | --- | --- |
| 1KB | 0.013±0.006 | 0.010±0.000 | 0.010±0.000 | -25.00% | -25.00% |
| 2KB | 0.040±0.000 | 0.030±0.000 | 0.040±0.000 | -25.00% | +0.00% |
| 4KB | 0.070±0.000 | 0.070±0.000 | 0.037±0.031 | +0.00% | -47.62% |
| 8KB | 0.103±0.064 | 0.140±0.000 | 0.097±0.075 | +35.48% | -6.45% |
| 16KB | 0.123±0.093 | 0.260±0.000 | 0.037±0.015 | +110.81% | -70.27% |
| 32KB | 0.177±0.029 | 0.323±0.182 | 0.063±0.042 | +83.02% | -64.15% |
| 64KB | 0.400±0.433 | 0.537±0.612 | 0.147±0.058 | +34.17% | -63.33% |
| 128KB | 0.580±0.745 | 0.200±0.062 | 0.310±0.135 | -65.52% | -46.55% |
| 256KB | 0.293±0.023 | 0.527±0.206 | 0.347±0.021 | +79.55% | +18.18% |
| 512KB | 1.517±0.693 | 1.183±0.462 | 0.570±0.078 | -21.98% | -62.42% |
| 1MB | 2.530±0.594 | 1.500±0.560 | 0.610±0.139 | -40.71% | -75.89% |
| 2MB | 1.637±1.095 | 1.787±0.422 | 0.700±0.020 | +9.16% | -57.23% |
| 4MB | 2.983±0.293 | 3.243±0.232 | 0.677±0.032 | +8.72% | -77.32% |
| 8MB | 3.407±0.342 | 3.353±0.295 | 0.670±0.017 | -1.57% | -80.33% |
| 16MB | 3.520±0.098 | 3.363±0.115 | 0.687±0.006 | -4.45% | -80.49% |
| 32MB | 3.723±0.021 | 3.733±0.065 | 0.690±0.000 | +0.27% | -81.47% |
| 64MB | 3.763±0.042 | 3.743±0.031 | 0.687±0.006 | -0.53% | -81.75% |
| 128MB | 3.780±0.017 | 3.773±0.021 | 0.687±0.006 | -0.18% | -81.83% |

### Verdict

- No size in this experiment met the bar for DEFAULT being genuinely slower than an alternative.

### Raw Output

### Step 1 Raw Output: DEFAULT tuning check

#### exp1-step1-default-tuning

Command:
```bash
mpirun --oversubscribe -np 1 /usr/bin/env LD_LIBRARY_PATH=/home/yunwei37/workspace/nccl-eBPF/nccl/build/lib NCCL_TESTS_DEVICE=0 NCCL_P2P_DISABLE=1 NCCL_NET=Socket NCCL_SHM_DISABLE=1 NCCL_HOSTID=bcast-default-tuning-rank0 NCCL_DEBUG=INFO NCCL_DEBUG_SUBSYS=TUNING NCCL_DEBUG_FILE=/home/yunwei37/workspace/nccl-eBPF/docs/tmp/default-suboptimal-search-logs/exp1-step1-default.debug.%h.%p /home/yunwei37/workspace/nccl-eBPF/nccl-tests/build/broadcast_perf_mpi -b 1024 -e 134217728 -f 4 -g 1 -n 1 -w 1 : -np 1 /usr/bin/env LD_LIBRARY_PATH=/home/yunwei37/workspace/nccl-eBPF/nccl/build/lib NCCL_TESTS_DEVICE=0 NCCL_P2P_DISABLE=1 NCCL_NET=Socket NCCL_SHM_DISABLE=1 NCCL_HOSTID=bcast-default-tuning-rank1 NCCL_DEBUG=INFO NCCL_DEBUG_SUBSYS=TUNING NCCL_DEBUG_FILE=/home/yunwei37/workspace/nccl-eBPF/docs/tmp/default-suboptimal-search-logs/exp1-step1-default.debug.%h.%p /home/yunwei37/workspace/nccl-eBPF/nccl-tests/build/broadcast_perf_mpi -b 1024 -e 134217728 -f 4 -g 1 -n 1 -w 1
```

stdout:
```text
# nccl-tests version 2.18.0 nccl-headers=22907 nccl-library=22907
# Collective test starting: broadcast_perf_mpi
# nThread 1 nGpus 1 minBytes 1024 maxBytes 134217728 step: 4(factor) warmup iters: 1 iters: 1 agg iters: 1 validation: 1 graph: 0
#
# Using devices
#  Rank  0 Group  0 Pid 3038984 on        lab device  0 [0000:02:00] NVIDIA GeForce RTX 5090
#  Rank  1 Group  0 Pid 3038985 on        lab device  0 [0000:02:00] NVIDIA GeForce RTX 5090
#
#                                                              out-of-place                       in-place          
#       size         count      type   redop    root     time   algbw   busbw  #wrong     time   algbw   busbw  #wrong 
#        (B)    (elements)                               (us)  (GB/s)  (GB/s)             (us)  (GB/s)  (GB/s)         
        1024           256     float    none       0  2239.78    0.00    0.00       0  2233.58    0.00    0.00       0
        4096          1024     float    none       0  2233.57    0.00    0.00       0  2233.47    0.00    0.00       0
       16384          4096     float    none       0  2241.03    0.01    0.01       0  2241.45    0.01    0.01       0
       65536         16384     float    none       0  2247.94    0.03    0.03       0  2247.88    0.03    0.03       0
      262144         65536     float    none       0  2283.48    0.11    0.11       0  2277.66    0.12    0.12       0
     1048576        262144     float    none       0  2396.00    0.44    0.44       0  2394.44    0.44    0.44       0
     4194304       1048576     float    none       0  2873.11    1.46    1.46       0  2958.62    1.42    1.42       0
    16777216       4194304     float    none       0  4887.84    3.43    3.43       0  4269.55    3.93    3.93       0
    67108864      16777216     float    none       0  28066.8    2.39    2.39       0  37664.5    1.78    1.78       0
# Out of bounds values : 0 OK
# Avg bus bandwidth    : 0.8664 
#
# Collective test concluded: broadcast_perf_mpi
#
```

stderr:
```text
Authorization required, but no authorization protocol specified

Authorization required, but no authorization protocol specified

Authorization required, but no authorization protocol specified

Authorization required, but no authorization protocol specified
```

Debug log: `exp1-step1-default.debug.lab.3038984`
```text
lab:3038984:3038984 [0] NCCL INFO NCCL version 2.29.7+cuda12.9
lab:3038984:3038984 [0] NCCL INFO NCCL git version master 3619159
lab:3038984:3038984 [0] NCCL INFO NCCL_P2P_DISABLE set by environment to 1
lab:3038984:3038984 [0] NCCL INFO TUNER/Plugin: Could not find: libnccl-tuner.so
lab:3038984:3038984 [0] NCCL INFO   Algorithm   |                            Tree                  |                            Ring                  |                   CollNetDirect                  |
lab:3038984:3038984 [0] NCCL INFO   Protocol    |             LL |          LL128 |         Simple |             LL |          LL128 |         Simple |             LL |          LL128 |         Simple |
lab:3038984:3038984 [0] NCCL INFO  Max NThreads |            512 |            640 |            512 |            512 |            640 |            256 |              0 |              0 |            640 |
lab:3038984:3038984 [0] NCCL INFO     Broadcast |     0.0/   0.0 |     0.0/   0.0 |     0.0/   0.0 |     9.3/   1.2 |    18.0/   0.0 |    22.4/   2.4 |     0.0/   0.0 |     0.0/   0.0 |     0.0/   0.0 |
lab:3038984:3038984 [0] NCCL INFO        Reduce |     0.0/   0.0 |     0.0/   0.0 |     0.0/   0.0 |     9.3/   1.2 |    18.0/   0.0 |    22.4/   2.4 |     0.0/   0.0 |     0.0/   0.0 |     0.0/   0.0 |
lab:3038984:3038984 [0] NCCL INFO     AllGather |     0.0/   0.0 |     0.0/   0.0 |     0.0/   0.0 |    11.6/   2.4 |    22.5/   0.0 |    22.4/   4.8 |     5.0/   0.0 |     8.5/   0.0 |    14.0/   0.0 |
lab:3038984:3038984 [0] NCCL INFO ReduceScatter |     0.0/   0.0 |     0.0/   0.0 |     0.0/   0.0 |    11.6/   2.4 |    22.5/   0.0 |    22.4/   4.8 |     5.0/   0.0 |     8.5/   0.0 |    14.0/   0.0 |
lab:3038984:3038984 [0] NCCL INFO     AllReduce |    16.8/   0.5 |    31.0/   0.0 |    36.4/   1.9 |    16.6/   1.2 |    31.0/   0.0 |    36.4/   2.4 |     5.0/   0.0 |     8.5/   0.0 |    14.0/   0.0 |
lab:3038984:3038984 [0] NCCL INFO   Algorithm   |                    CollNetChain                  |                            NVLS                  |                        NVLSTree                  |
lab:3038984:3038984 [0] NCCL INFO   Protocol    |             LL |          LL128 |         Simple |             LL |          LL128 |         Simple |             LL |          LL128 |         Simple |
lab:3038984:3038984 [0] NCCL INFO  Max NThreads |              0 |              0 |            640 |              0 |              0 |            640 |              0 |              0 |            640 |
lab:3038984:3038984 [0] NCCL INFO     Broadcast |     0.0/   0.0 |     0.0/   0.0 |     0.0/   0.0 |     0.0/   0.0 |     0.0/   0.0 |     0.0/   0.0 |     0.0/   0.0 |     0.0/   0.0 |     0.0/   0.0 |
lab:3038984:3038984 [0] NCCL INFO        Reduce |     0.0/   0.0 |     0.0/   0.0 |     0.0/   0.0 |     0.0/   0.0 |     0.0/   0.0 |     0.0/   0.0 |     0.0/   0.0 |     0.0/   0.0 |     0.0/   0.0 |
lab:3038984:3038984 [0] NCCL INFO     AllGather |     0.0/   0.0 |     0.0/   0.0 |     0.0/   0.0 |     0.0/   0.0 |     0.0/   0.0 |     0.0/   0.0 |     0.0/   0.0 |     0.0/   0.0 |     0.0/   0.0 |
lab:3038984:3038984 [0] NCCL INFO ReduceScatter |     0.0/   0.0 |     0.0/   0.0 |     0.0/   0.0 |     0.0/   0.0 |     0.0/   0.0 |     0.0/   0.0 |     0.0/   0.0 |     0.0/   0.0 |     0.0/   0.0 |
lab:3038984:3038984 [0] NCCL INFO     AllReduce |     5.0/   0.0 |     8.5/   0.0 |    14.0/   0.0 |     0.0/   0.0 |     0.0/   0.0 |     0.0/   0.0 |     0.0/   0.0 |     0.0/   0.0 |     0.0/   0.0 |
lab:3038984:3038984 [0] NCCL INFO   Algorithm   |                             PAT                  |
lab:3038984:3038984 [0] NCCL INFO   Protocol    |             LL |          LL128 |         Simple |             LL |          LL128 |         Simple |             LL |          LL128 |         Simple |
lab:3038984:3038984 [0] NCCL INFO  Max NThreads |              0 |              0 |              0 |
lab:3038984:3038984 [0] NCCL INFO     Broadcast |     0.0/   0.0 |     0.0/   0.0 |     0.0/   0.0 |
lab:3038984:3038984 [0] NCCL INFO        Reduce |     0.0/   0.0 |     0.0/   0.0 |     0.0/   0.0 |
lab:3038984:3038984 [0] NCCL INFO     AllGather |     0.0/   0.0 |     0.0/   0.0 |    17.6/   3.6 |
lab:3038984:3038984 [0] NCCL INFO ReduceScatter |     0.0/   0.0 |     0.0/   0.0 |    17.6/   3.6 |
lab:3038984:3038984 [0] NCCL INFO     AllReduce |     0.0/   0.0 |     0.0/   0.0 |     0.0/   0.0 |
lab:3038984:3038984 [0] NCCL INFO Broadcast: 1024 Bytes -> Algo RING proto LL channel{Lo..Hi}={0..0}
lab:3038984:3038984 [0] NCCL INFO Broadcast: 2048 Bytes -> Algo RING proto LL channel{Lo..Hi}={0..0}
lab:3038984:3038984 [0] NCCL INFO Broadcast: 4096 Bytes -> Algo RING proto LL channel{Lo..Hi}={0..0}
lab:3038984:3038984 [0] NCCL INFO Broadcast: 8192 Bytes -> Algo RING proto LL channel{Lo..Hi}={0..0}
lab:3038984:3038984 [0] NCCL INFO Broadcast: 16384 Bytes -> Algo RING proto LL channel{Lo..Hi}={0..1}
lab:3038984:3038984 [0] NCCL INFO Broadcast: 32768 Bytes -> Algo RING proto SIMPLE channel{Lo..Hi}={0..0}
lab:3038984:3038984 [0] NCCL INFO Broadcast: 65536 Bytes -> Algo RING proto SIMPLE channel{Lo..Hi}={0..1}
lab:3038984:3038984 [0] NCCL INFO Broadcast: 131072 Bytes -> Algo RING proto SIMPLE channel{Lo..Hi}={0..3}
lab:3038984:3038984 [0] NCCL INFO Broadcast: 262144 Bytes -> Algo RING proto SIMPLE channel{Lo..Hi}={0..3}
lab:3038984:3038984 [0] NCCL INFO Broadcast: 524288 Bytes -> Algo RING proto SIMPLE channel{Lo..Hi}={0..3}
lab:3038984:3038984 [0] NCCL INFO Broadcast: 1048576 Bytes -> Algo RING proto SIMPLE channel{Lo..Hi}={0..3}
lab:3038984:3038984 [0] NCCL INFO Broadcast: 2097152 Bytes -> Algo RING proto SIMPLE channel{Lo..Hi}={0..3}
lab:3038984:3038984 [0] NCCL INFO Broadcast: 4194304 Bytes -> Algo RING proto SIMPLE channel{Lo..Hi}={0..3}
lab:3038984:3038984 [0] NCCL INFO Broadcast: 8388608 Bytes -> Algo RING proto SIMPLE channel{Lo..Hi}={0..3}
lab:3038984:3038984 [0] NCCL INFO Broadcast: 16777216 Bytes -> Algo RING proto SIMPLE channel{Lo..Hi}={0..3}
lab:3038984:3038984 [0] NCCL INFO Broadcast: 33554432 Bytes -> Algo RING proto SIMPLE channel{Lo..Hi}={0..3}
lab:3038984:3038984 [0] NCCL INFO Broadcast: 67108864 Bytes -> Algo RING proto SIMPLE channel{Lo..Hi}={0..3}
lab:3038984:3038984 [0] NCCL INFO Broadcast: 134217728 Bytes -> Algo RING proto SIMPLE channel{Lo..Hi}={0..3}
lab:3038984:3038984 [0] NCCL INFO Broadcast: 1024 Bytes -> Algo RING proto LL channel{Lo..Hi}={0..0}
lab:3038984:3038984 [0] NCCL INFO Broadcast: 1024 Bytes -> Algo RING proto LL channel{Lo..Hi}={0..0}
lab:3038984:3038984 [0] NCCL INFO Broadcast: 1024 Bytes -> Algo RING proto LL channel{Lo..Hi}={0..0}
lab:3038984:3038984 [0] NCCL INFO Broadcast: 1024 Bytes -> Algo RING proto LL channel{Lo..Hi}={0..0}
lab:3038984:3038984 [0] NCCL INFO Broadcast: 1024 Bytes -> Algo RING proto LL channel{Lo..Hi}={0..0}
lab:3038984:3038984 [0] NCCL INFO Broadcast: 1024 Bytes -> Algo RING proto LL channel{Lo..Hi}={0..0}
lab:3038984:3038984 [0] NCCL INFO Broadcast: 4096 Bytes -> Algo RING proto LL channel{Lo..Hi}={0..0}
lab:3038984:3038984 [0] NCCL INFO Broadcast: 4096 Bytes -> Algo RING proto LL channel{Lo..Hi}={0..0}
lab:3038984:3038984 [0] NCCL INFO Broadcast: 4096 Bytes -> Algo RING proto LL channel{Lo..Hi}={0..0}
lab:3038984:3038984 [0] NCCL INFO Broadcast: 4096 Bytes -> Algo RING proto LL channel{Lo..Hi}={0..0}
lab:3038984:3038984 [0] NCCL INFO Broadcast: 4096 Bytes -> Algo RING proto LL channel{Lo..Hi}={0..0}
lab:3038984:3038984 [0] NCCL INFO Broadcast: 4096 Bytes -> Algo RING proto LL channel{Lo..Hi}={0..0}
lab:3038984:3038984 [0] NCCL INFO Broadcast: 16384 Bytes -> Algo RING proto LL channel{Lo..Hi}={0..1}
lab:3038984:3038984 [0] NCCL INFO Broadcast: 16384 Bytes -> Algo RING proto LL channel{Lo..Hi}={0..1}
lab:3038984:3038984 [0] NCCL INFO Broadcast: 16384 Bytes -> Algo RING proto LL channel{Lo..Hi}={0..1}
lab:3038984:3038984 [0] NCCL INFO Broadcast: 16384 Bytes -> Algo RING proto LL channel{Lo..Hi}={0..1}
lab:3038984:3038984 [0] NCCL INFO Broadcast: 16384 Bytes -> Algo RING proto LL channel{Lo..Hi}={0..1}
lab:3038984:3038984 [0] NCCL INFO Broadcast: 16384 Bytes -> Algo RING proto LL channel{Lo..Hi}={0..1}
lab:3038984:3038984 [0] NCCL INFO Broadcast: 65536 Bytes -> Algo RING proto SIMPLE channel{Lo..Hi}={0..1}
lab:3038984:3038984 [0] NCCL INFO Broadcast: 65536 Bytes -> Algo RING proto SIMPLE channel{Lo..Hi}={0..1}
lab:3038984:3038984 [0] NCCL INFO Broadcast: 65536 Bytes -> Algo RING proto SIMPLE channel{Lo..Hi}={0..1}
lab:3038984:3038984 [0] NCCL INFO Broadcast: 65536 Bytes -> Algo RING proto SIMPLE channel{Lo..Hi}={0..1}
lab:3038984:3038984 [0] NCCL INFO Broadcast: 65536 Bytes -> Algo RING proto SIMPLE channel{Lo..Hi}={0..1}
lab:3038984:3038984 [0] NCCL INFO Broadcast: 65536 Bytes -> Algo RING proto SIMPLE channel{Lo..Hi}={0..1}
lab:3038984:3038984 [0] NCCL INFO Broadcast: 262144 Bytes -> Algo RING proto SIMPLE channel{Lo..Hi}={0..3}
lab:3038984:3038984 [0] NCCL INFO Broadcast: 262144 Bytes -> Algo RING proto SIMPLE channel{Lo..Hi}={0..3}
lab:3038984:3038984 [0] NCCL INFO Broadcast: 262144 Bytes -> Algo RING proto SIMPLE channel{Lo..Hi}={0..3}
lab:3038984:3038984 [0] NCCL INFO Broadcast: 262144 Bytes -> Algo RING proto SIMPLE channel{Lo..Hi}={0..3}
lab:3038984:3038984 [0] NCCL INFO Broadcast: 262144 Bytes -> Algo RING proto SIMPLE channel{Lo..Hi}={0..3}
lab:3038984:3038984 [0] NCCL INFO Broadcast: 262144 Bytes -> Algo RING proto SIMPLE channel{Lo..Hi}={0..3}
lab:3038984:3038984 [0] NCCL INFO Broadcast: 1048576 Bytes -> Algo RING proto SIMPLE channel{Lo..Hi}={0..3}
lab:3038984:3038984 [0] NCCL INFO Broadcast: 1048576 Bytes -> Algo RING proto SIMPLE channel{Lo..Hi}={0..3}
lab:3038984:3038984 [0] NCCL INFO Broadcast: 1048576 Bytes -> Algo RING proto SIMPLE channel{Lo..Hi}={0..3}
lab:3038984:3038984 [0] NCCL INFO Broadcast: 1048576 Bytes -> Algo RING proto SIMPLE channel{Lo..Hi}={0..3}
lab:3038984:3038984 [0] NCCL INFO Broadcast: 1048576 Bytes -> Algo RING proto SIMPLE channel{Lo..Hi}={0..3}
lab:3038984:3038984 [0] NCCL INFO Broadcast: 1048576 Bytes -> Algo RING proto SIMPLE channel{Lo..Hi}={0..3}
lab:3038984:3038984 [0] NCCL INFO Broadcast: 4194304 Bytes -> Algo RING proto SIMPLE channel{Lo..Hi}={0..3}
lab:3038984:3038984 [0] NCCL INFO Broadcast: 4194304 Bytes -> Algo RING proto SIMPLE channel{Lo..Hi}={0..3}
lab:3038984:3038984 [0] NCCL INFO Broadcast: 4194304 Bytes -> Algo RING proto SIMPLE channel{Lo..Hi}={0..3}
lab:3038984:3038984 [0] NCCL INFO Broadcast: 4194304 Bytes -> Algo RING proto SIMPLE channel{Lo..Hi}={0..3}
lab:3038984:3038984 [0] NCCL INFO Broadcast: 4194304 Bytes -> Algo RING proto SIMPLE channel{Lo..Hi}={0..3}
lab:3038984:3038984 [0] NCCL INFO Broadcast: 4194304 Bytes -> Algo RING proto SIMPLE channel{Lo..Hi}={0..3}
lab:3038984:3038984 [0] NCCL INFO Broadcast: 16777216 Bytes -> Algo RING proto SIMPLE channel{Lo..Hi}={0..3}
lab:3038984:3038984 [0] NCCL INFO Broadcast: 16777216 Bytes -> Algo RING proto SIMPLE channel{Lo..Hi}={0..3}
lab:3038984:3038984 [0] NCCL INFO Broadcast: 16777216 Bytes -> Algo RING proto SIMPLE channel{Lo..Hi}={0..3}
lab:3038984:3038984 [0] NCCL INFO Broadcast: 16777216 Bytes -> Algo RING proto SIMPLE channel{Lo..Hi}={0..3}
lab:3038984:3038984 [0] NCCL INFO Broadcast: 16777216 Bytes -> Algo RING proto SIMPLE channel{Lo..Hi}={0..3}
lab:3038984:3038984 [0] NCCL INFO Broadcast: 16777216 Bytes -> Algo RING proto SIMPLE channel{Lo..Hi}={0..3}
lab:3038984:3038984 [0] NCCL INFO Broadcast: 67108864 Bytes -> Algo RING proto SIMPLE channel{Lo..Hi}={0..3}
lab:3038984:3038984 [0] NCCL INFO Broadcast: 67108864 Bytes -> Algo RING proto SIMPLE channel{Lo..Hi}={0..3}
lab:3038984:3038984 [0] NCCL INFO Broadcast: 67108864 Bytes -> Algo RING proto SIMPLE channel{Lo..Hi}={0..3}
lab:3038984:3038984 [0] NCCL INFO Broadcast: 67108864 Bytes -> Algo RING proto SIMPLE channel{Lo..Hi}={0..3}
lab:3038984:3038984 [0] NCCL INFO Broadcast: 67108864 Bytes -> Algo RING proto SIMPLE channel{Lo..Hi}={0..3}
lab:3038984:3038984 [0] NCCL INFO Broadcast: 67108864 Bytes -> Algo RING proto SIMPLE channel{Lo..Hi}={0..3}
```

Debug log: `exp1-step1-default.debug.lab.3038985`
```text
lab:3038985:3038985 [0] NCCL INFO NCCL version 2.29.7+cuda12.9
lab:3038985:3038985 [0] NCCL INFO NCCL git version master 3619159
lab:3038985:3038985 [0] NCCL INFO NCCL_P2P_DISABLE set by environment to 1
lab:3038985:3038985 [0] NCCL INFO TUNER/Plugin: Could not find: libnccl-tuner.so
```

### Step 2 Raw Output: DEFAULT vs Simple vs LL

#### exp1-broadcast-default-rep1

Command:
```bash
mpirun --oversubscribe -np 1 /usr/bin/env LD_LIBRARY_PATH=/home/yunwei37/workspace/nccl-eBPF/nccl/build/lib NCCL_TESTS_DEVICE=0 NCCL_P2P_DISABLE=1 NCCL_NET=Socket NCCL_SHM_DISABLE=1 NCCL_HOSTID=exp1-broadcast-default-rep1-rank0 /home/yunwei37/workspace/nccl-eBPF/nccl-tests/build/broadcast_perf_mpi -b 1024 -e 134217728 -f 2 -g 1 -n 50 -w 10 : -np 1 /usr/bin/env LD_LIBRARY_PATH=/home/yunwei37/workspace/nccl-eBPF/nccl/build/lib NCCL_TESTS_DEVICE=0 NCCL_P2P_DISABLE=1 NCCL_NET=Socket NCCL_SHM_DISABLE=1 NCCL_HOSTID=exp1-broadcast-default-rep1-rank1 /home/yunwei37/workspace/nccl-eBPF/nccl-tests/build/broadcast_perf_mpi -b 1024 -e 134217728 -f 2 -g 1 -n 50 -w 10
```

stdout:
```text
# nccl-tests version 2.18.0 nccl-headers=22907 nccl-library=22907
# Collective test starting: broadcast_perf_mpi
# nThread 1 nGpus 1 minBytes 1024 maxBytes 134217728 step: 2(factor) warmup iters: 10 iters: 50 agg iters: 1 validation: 1 graph: 0
#
# Using devices
#  Rank  0 Group  0 Pid 3039008 on        lab device  0 [0000:02:00] NVIDIA GeForce RTX 5090
#  Rank  1 Group  0 Pid 3039009 on        lab device  0 [0000:02:00] NVIDIA GeForce RTX 5090
#
#                                                              out-of-place                       in-place          
#       size         count      type   redop    root     time   algbw   busbw  #wrong     time   algbw   busbw  #wrong 
#        (B)    (elements)                               (us)  (GB/s)  (GB/s)             (us)  (GB/s)  (GB/s)         
        1024           256     float    none       0   143.97    0.01    0.01       0    55.84    0.02    0.02       0
        2048           512     float    none       0    56.79    0.04    0.04       0    54.60    0.04    0.04       0
        4096          1024     float    none       0    59.24    0.07    0.07       0    60.19    0.07    0.07       0
        8192          2048     float    none       0    58.65    0.14    0.14       0    58.20    0.14    0.14       0
       16384          4096     float    none       0   111.98    0.15    0.15       0   431.22    0.04    0.04       0
       32768          8192     float    none       0   155.11    0.21    0.21       0    62.56    0.52    0.52       0
       65536         16384     float    none       0    73.09    0.90    0.90       0   343.80    0.19    0.19       0
      131072         32768     float    none       0   758.78    0.17    0.17       0   302.01    0.43    0.43       0
      262144         65536     float    none       0   927.05    0.28    0.28       0   234.03    1.12    1.12       0
      524288        131072     float    none       0   226.61    2.31    2.31       0   642.28    0.82    0.82       0
     1048576        262144     float    none       0   452.58    2.32    2.32       0   499.31    2.10    2.10       0
     2097152        524288     float    none       0  3843.98    0.55    0.55       0  1039.24    2.02    2.02       0
     4194304       1048576     float    none       0  1262.01    3.32    3.32       0  1246.20    3.37    3.37       0
     8388608       2097152     float    none       0  2220.30    3.78    3.78       0  2932.13    2.86    2.86       0
    16777216       4194304     float    none       0  4662.01    3.60    3.60       0  4937.84    3.40    3.40       0
    33554432       8388608     float    none       0  9003.47    3.73    3.73       0  8996.56    3.73    3.73       0
    67108864      16777216     float    none       0  18001.3    3.73    3.73       0  17874.1    3.75    3.75       0
   134217728      33554432     float    none       0  35562.1    3.77    3.77       0  34784.6    3.86    3.86       0
# Out of bounds values : 0 OK
# Avg bus bandwidth    : 1.59832 
#
# Collective test concluded: broadcast_perf_mpi
#
```

stderr:
```text
Authorization required, but no authorization protocol specified

Authorization required, but no authorization protocol specified

Authorization required, but no authorization protocol specified

Authorization required, but no authorization protocol specified
```

#### exp1-broadcast-simple-rep1

Command:
```bash
mpirun --oversubscribe -np 1 /usr/bin/env LD_LIBRARY_PATH=/home/yunwei37/workspace/nccl-eBPF/nccl/build/lib NCCL_TESTS_DEVICE=0 NCCL_P2P_DISABLE=1 NCCL_NET=Socket NCCL_PROTO=Simple NCCL_SHM_DISABLE=1 NCCL_HOSTID=exp1-broadcast-simple-rep1-rank0 /home/yunwei37/workspace/nccl-eBPF/nccl-tests/build/broadcast_perf_mpi -b 1024 -e 134217728 -f 2 -g 1 -n 50 -w 10 : -np 1 /usr/bin/env LD_LIBRARY_PATH=/home/yunwei37/workspace/nccl-eBPF/nccl/build/lib NCCL_TESTS_DEVICE=0 NCCL_P2P_DISABLE=1 NCCL_NET=Socket NCCL_PROTO=Simple NCCL_SHM_DISABLE=1 NCCL_HOSTID=exp1-broadcast-simple-rep1-rank1 /home/yunwei37/workspace/nccl-eBPF/nccl-tests/build/broadcast_perf_mpi -b 1024 -e 134217728 -f 2 -g 1 -n 50 -w 10
```

stdout:
```text
# nccl-tests version 2.18.0 nccl-headers=22907 nccl-library=22907
# Collective test starting: broadcast_perf_mpi
# nThread 1 nGpus 1 minBytes 1024 maxBytes 134217728 step: 2(factor) warmup iters: 10 iters: 50 agg iters: 1 validation: 1 graph: 0
#
# Using devices
#  Rank  0 Group  0 Pid 3039125 on        lab device  0 [0000:02:00] NVIDIA GeForce RTX 5090
#  Rank  1 Group  0 Pid 3039126 on        lab device  0 [0000:02:00] NVIDIA GeForce RTX 5090
#
#                                                              out-of-place                       in-place          
#       size         count      type   redop    root     time   algbw   busbw  #wrong     time   algbw   busbw  #wrong 
#        (B)    (elements)                               (us)  (GB/s)  (GB/s)             (us)  (GB/s)  (GB/s)         
        1024           256     float    none       0   104.12    0.01    0.01       0    61.07    0.02    0.02       0
        2048           512     float    none       0    59.73    0.03    0.03       0    59.02    0.03    0.03       0
        4096          1024     float    none       0    59.84    0.07    0.07       0    59.43    0.07    0.07       0
        8192          2048     float    none       0    60.14    0.14    0.14       0    60.11    0.14    0.14       0
       16384          4096     float    none       0    63.69    0.26    0.26       0    60.92    0.27    0.27       0
       32768          8192     float    none       0   112.13    0.29    0.29       0   889.36    0.04    0.04       0
       65536         16384     float    none       0   210.53    0.31    0.31       0   813.83    0.08    0.08       0
      131072         32768     float    none       0   902.86    0.15    0.15       0   237.40    0.55    0.55       0
      262144         65536     float    none       0   896.70    0.29    0.29       0   784.00    0.33    0.33       0
      524288        131072     float    none       0   360.87    1.45    1.45       0   411.63    1.27    1.27       0
     1048576        262144     float    none       0   508.16    2.06    2.06       0  1331.93    0.79    0.79       0
     2097152        524288     float    none       0  1030.01    2.04    2.04       0   769.94    2.72    2.72       0
     4194304       1048576     float    none       0  1407.32    2.98    2.98       0  1357.63    3.09    3.09       0
     8388608       2097152     float    none       0  2303.83    3.64    3.64       0  2229.77    3.76    3.76       0
    16777216       4194304     float    none       0  4819.62    3.48    3.48       0  4417.67    3.80    3.80       0
    33554432       8388608     float    none       0  9139.03    3.67    3.67       0  8853.14    3.79    3.79       0
    67108864      16777216     float    none       0  18091.1    3.71    3.71       0  17515.6    3.83    3.83       0
   134217728      33554432     float    none       0  35810.7    3.75    3.75       0  35458.0    3.79    3.79       0
# Out of bounds values : 0 OK
# Avg bus bandwidth    : 1.57503 
#
# Collective test concluded: broadcast_perf_mpi
#
```

stderr:
```text
Authorization required, but no authorization protocol specified

Authorization required, but no authorization protocol specified

Authorization required, but no authorization protocol specified

Authorization required, but no authorization protocol specified
```

#### exp1-broadcast-ll-rep1

Command:
```bash
mpirun --oversubscribe -np 1 /usr/bin/env LD_LIBRARY_PATH=/home/yunwei37/workspace/nccl-eBPF/nccl/build/lib NCCL_TESTS_DEVICE=0 NCCL_P2P_DISABLE=1 NCCL_NET=Socket NCCL_PROTO=LL NCCL_SHM_DISABLE=1 NCCL_HOSTID=exp1-broadcast-ll-rep1-rank0 /home/yunwei37/workspace/nccl-eBPF/nccl-tests/build/broadcast_perf_mpi -b 1024 -e 134217728 -f 2 -g 1 -n 50 -w 10 : -np 1 /usr/bin/env LD_LIBRARY_PATH=/home/yunwei37/workspace/nccl-eBPF/nccl/build/lib NCCL_TESTS_DEVICE=0 NCCL_P2P_DISABLE=1 NCCL_NET=Socket NCCL_PROTO=LL NCCL_SHM_DISABLE=1 NCCL_HOSTID=exp1-broadcast-ll-rep1-rank1 /home/yunwei37/workspace/nccl-eBPF/nccl-tests/build/broadcast_perf_mpi -b 1024 -e 134217728 -f 2 -g 1 -n 50 -w 10
```

stdout:
```text
# nccl-tests version 2.18.0 nccl-headers=22907 nccl-library=22907
# Collective test starting: broadcast_perf_mpi
# nThread 1 nGpus 1 minBytes 1024 maxBytes 134217728 step: 2(factor) warmup iters: 10 iters: 50 agg iters: 1 validation: 1 graph: 0
#
# Using devices
#  Rank  0 Group  0 Pid 3039239 on        lab device  0 [0000:02:00] NVIDIA GeForce RTX 5090
#  Rank  1 Group  0 Pid 3039240 on        lab device  0 [0000:02:00] NVIDIA GeForce RTX 5090
#
#                                                              out-of-place                       in-place          
#       size         count      type   redop    root     time   algbw   busbw  #wrong     time   algbw   busbw  #wrong 
#        (B)    (elements)                               (us)  (GB/s)  (GB/s)             (us)  (GB/s)  (GB/s)         
        1024           256     float    none       0   188.10    0.01    0.01       0    55.95    0.02    0.02       0
        2048           512     float    none       0    56.70    0.04    0.04       0    53.91    0.04    0.04       0
        4096          1024     float    none       0    61.50    0.07    0.07       0    56.62    0.07    0.07       0
        8192          2048     float    none       0    58.21    0.14    0.14       0   314.36    0.03    0.03       0
       16384          4096     float    none       0   382.21    0.04    0.04       0   944.71    0.02    0.02       0
       32768          8192     float    none       0  1169.14    0.03    0.03       0   757.75    0.04    0.04       0
       65536         16384     float    none       0   355.97    0.18    0.18       0   941.42    0.07    0.07       0
      131072         32768     float    none       0   810.27    0.16    0.16       0  1349.89    0.10    0.10       0
      262144         65536     float    none       0   699.80    0.37    0.37       0   475.26    0.55    0.55       0
      524288        131072     float    none       0   840.39    0.62    0.62       0   841.44    0.62    0.62       0
     1048576        262144     float    none       0  1527.16    0.69    0.69       0  1528.11    0.69    0.69       0
     2097152        524288     float    none       0  3100.83    0.68    0.68       0  3246.69    0.65    0.65       0
     4194304       1048576     float    none       0  6038.78    0.69    0.69       0  6179.53    0.68    0.68       0
     8388608       2097152     float    none       0  12199.7    0.69    0.69       0  12757.4    0.66    0.66       0
    16777216       4194304     float    none       0  24620.3    0.68    0.68       0  24724.7    0.68    0.68       0
    33554432       8388608     float    none       0  48348.2    0.69    0.69       0  48660.8    0.69    0.69       0
    67108864      16777216     float    none       0  97316.8    0.69    0.69       0  96621.4    0.69    0.69       0
   134217728      33554432     float    none       0   194670    0.69    0.69       0   194368    0.69    0.69       0
# Out of bounds values : 0 OK
# Avg bus bandwidth    : 0.392834 
#
# Collective test concluded: broadcast_perf_mpi
#
```

stderr:
```text
Authorization required, but no authorization protocol specified

Authorization required, but no authorization protocol specified

Authorization required, but no authorization protocol specified

Authorization required, but no authorization protocol specified
```

#### exp1-broadcast-default-rep2

Command:
```bash
mpirun --oversubscribe -np 1 /usr/bin/env LD_LIBRARY_PATH=/home/yunwei37/workspace/nccl-eBPF/nccl/build/lib NCCL_TESTS_DEVICE=0 NCCL_P2P_DISABLE=1 NCCL_NET=Socket NCCL_SHM_DISABLE=1 NCCL_HOSTID=exp1-broadcast-default-rep2-rank0 /home/yunwei37/workspace/nccl-eBPF/nccl-tests/build/broadcast_perf_mpi -b 1024 -e 134217728 -f 2 -g 1 -n 50 -w 10 : -np 1 /usr/bin/env LD_LIBRARY_PATH=/home/yunwei37/workspace/nccl-eBPF/nccl/build/lib NCCL_TESTS_DEVICE=0 NCCL_P2P_DISABLE=1 NCCL_NET=Socket NCCL_SHM_DISABLE=1 NCCL_HOSTID=exp1-broadcast-default-rep2-rank1 /home/yunwei37/workspace/nccl-eBPF/nccl-tests/build/broadcast_perf_mpi -b 1024 -e 134217728 -f 2 -g 1 -n 50 -w 10
```

stdout:
```text
# nccl-tests version 2.18.0 nccl-headers=22907 nccl-library=22907
# Collective test starting: broadcast_perf_mpi
# nThread 1 nGpus 1 minBytes 1024 maxBytes 134217728 step: 2(factor) warmup iters: 10 iters: 50 agg iters: 1 validation: 1 graph: 0
#
# Using devices
#  Rank  0 Group  0 Pid 3039589 on        lab device  0 [0000:02:00] NVIDIA GeForce RTX 5090
#  Rank  1 Group  0 Pid 3039590 on        lab device  0 [0000:02:00] NVIDIA GeForce RTX 5090
#
#                                                              out-of-place                       in-place          
#       size         count      type   redop    root     time   algbw   busbw  #wrong     time   algbw   busbw  #wrong 
#        (B)    (elements)                               (us)  (GB/s)  (GB/s)             (us)  (GB/s)  (GB/s)         
        1024           256     float    none       0    99.10    0.01    0.01       0    12.43    0.08    0.08       0
        2048           512     float    none       0    56.38    0.04    0.04       0    53.43    0.04    0.04       0
        4096          1024     float    none       0    58.12    0.07    0.07       0    59.97    0.07    0.07       0
        8192          2048     float    none       0    59.59    0.14    0.14       0    60.26    0.14    0.14       0
       16384          4096     float    none       0    81.76    0.20    0.20       0   274.94    0.06    0.06       0
       32768          8192     float    none       0   199.95    0.16    0.16       0   817.03    0.04    0.04       0
       65536         16384     float    none       0   426.52    0.15    0.15       0   254.54    0.26    0.26       0
      131072         32768     float    none       0    90.72    1.44    1.44       0   149.27    0.88    0.88       0
      262144         65536     float    none       0   927.18    0.28    0.28       0   875.34    0.30    0.30       0
      524288        131072     float    none       0   432.00    1.21    1.21       0   314.52    1.67    1.67       0
     1048576        262144     float    none       0   328.09    3.20    3.20       0   333.71    3.14    3.14       0
     2097152        524288     float    none       0   765.71    2.74    2.74       0   996.97    2.10    2.10       0
     4194304       1048576     float    none       0  1501.88    2.79    2.79       0  1299.77    3.23    3.23       0
     8388608       2097152     float    none       0  2522.85    3.33    3.33       0  2815.10    2.98    2.98       0
    16777216       4194304     float    none       0  4728.24    3.55    3.55       0  4781.36    3.51    3.51       0
    33554432       8388608     float    none       0  9057.22    3.70    3.70       0  8841.94    3.79    3.79       0
    67108864      16777216     float    none       0  17607.5    3.81    3.81       0  17488.7    3.84    3.84       0
   134217728      33554432     float    none       0  35329.1    3.80    3.80       0  35027.7    3.83    3.83       0
# Out of bounds values : 0 OK
# Avg bus bandwidth    : 1.68283 
#
# Collective test concluded: broadcast_perf_mpi
#
```

stderr:
```text
Authorization required, but no authorization protocol specified

Authorization required, but no authorization protocol specified

Authorization required, but no authorization protocol specified

Authorization required, but no authorization protocol specified
```

#### exp1-broadcast-simple-rep2

Command:
```bash
mpirun --oversubscribe -np 1 /usr/bin/env LD_LIBRARY_PATH=/home/yunwei37/workspace/nccl-eBPF/nccl/build/lib NCCL_TESTS_DEVICE=0 NCCL_P2P_DISABLE=1 NCCL_NET=Socket NCCL_PROTO=Simple NCCL_SHM_DISABLE=1 NCCL_HOSTID=exp1-broadcast-simple-rep2-rank0 /home/yunwei37/workspace/nccl-eBPF/nccl-tests/build/broadcast_perf_mpi -b 1024 -e 134217728 -f 2 -g 1 -n 50 -w 10 : -np 1 /usr/bin/env LD_LIBRARY_PATH=/home/yunwei37/workspace/nccl-eBPF/nccl/build/lib NCCL_TESTS_DEVICE=0 NCCL_P2P_DISABLE=1 NCCL_NET=Socket NCCL_PROTO=Simple NCCL_SHM_DISABLE=1 NCCL_HOSTID=exp1-broadcast-simple-rep2-rank1 /home/yunwei37/workspace/nccl-eBPF/nccl-tests/build/broadcast_perf_mpi -b 1024 -e 134217728 -f 2 -g 1 -n 50 -w 10
```

stdout:
```text
# nccl-tests version 2.18.0 nccl-headers=22907 nccl-library=22907
# Collective test starting: broadcast_perf_mpi
# nThread 1 nGpus 1 minBytes 1024 maxBytes 134217728 step: 2(factor) warmup iters: 10 iters: 50 agg iters: 1 validation: 1 graph: 0
#
# Using devices
#  Rank  0 Group  0 Pid 3039677 on        lab device  0 [0000:02:00] NVIDIA GeForce RTX 5090
#  Rank  1 Group  0 Pid 3039678 on        lab device  0 [0000:02:00] NVIDIA GeForce RTX 5090
#
#                                                              out-of-place                       in-place          
#       size         count      type   redop    root     time   algbw   busbw  #wrong     time   algbw   busbw  #wrong 
#        (B)    (elements)                               (us)  (GB/s)  (GB/s)             (us)  (GB/s)  (GB/s)         
        1024           256     float    none       0   148.87    0.01    0.01       0    61.00    0.02    0.02       0
        2048           512     float    none       0    59.58    0.03    0.03       0    59.14    0.03    0.03       0
        4096          1024     float    none       0    59.67    0.07    0.07       0    61.68    0.07    0.07       0
        8192          2048     float    none       0    60.20    0.14    0.14       0    60.04    0.14    0.14       0
       16384          4096     float    none       0    63.71    0.26    0.26       0    63.38    0.26    0.26       0
       32768          8192     float    none       0    63.47    0.52    0.52       0    65.07    0.50    0.50       0
       65536         16384     float    none       0   923.83    0.07    0.07       0   273.73    0.24    0.24       0
      131072         32768     float    none       0   488.66    0.27    0.27       0   719.05    0.18    0.18       0
      262144         65536     float    none       0   426.22    0.62    0.62       0   877.70    0.30    0.30       0
      524288        131072     float    none       0   360.47    1.45    1.45       0   223.87    2.34    2.34       0
     1048576        262144     float    none       0   700.31    1.50    1.50       0   586.10    1.79    1.79       0
     2097152        524288     float    none       0  1612.81    1.30    1.30       0  1068.53    1.96    1.96       0
     4194304       1048576     float    none       0  1227.68    3.42    3.42       0  1246.12    3.37    3.37       0
     8388608       2097152     float    none       0  2749.01    3.05    3.05       0  2467.09    3.40    3.40       0
    16777216       4194304     float    none       0  5158.35    3.25    3.25       0  4789.10    3.50    3.50       0
    33554432       8388608     float    none       0  8820.45    3.80    3.80       0  9048.77    3.71    3.71       0
    67108864      16777216     float    none       0  17810.0    3.77    3.77       0  18225.3    3.68    3.68       0
   134217728      33554432     float    none       0  35501.4    3.78    3.78       0  34812.7    3.86    3.86       0
# Out of bounds values : 0 OK
# Avg bus bandwidth    : 1.57346 
#
# Collective test concluded: broadcast_perf_mpi
#
```

stderr:
```text
Authorization required, but no authorization protocol specified

Authorization required, but no authorization protocol specified

Authorization required, but no authorization protocol specified

Authorization required, but no authorization protocol specified
```

#### exp1-broadcast-ll-rep2

Command:
```bash
mpirun --oversubscribe -np 1 /usr/bin/env LD_LIBRARY_PATH=/home/yunwei37/workspace/nccl-eBPF/nccl/build/lib NCCL_TESTS_DEVICE=0 NCCL_P2P_DISABLE=1 NCCL_NET=Socket NCCL_PROTO=LL NCCL_SHM_DISABLE=1 NCCL_HOSTID=exp1-broadcast-ll-rep2-rank0 /home/yunwei37/workspace/nccl-eBPF/nccl-tests/build/broadcast_perf_mpi -b 1024 -e 134217728 -f 2 -g 1 -n 50 -w 10 : -np 1 /usr/bin/env LD_LIBRARY_PATH=/home/yunwei37/workspace/nccl-eBPF/nccl/build/lib NCCL_TESTS_DEVICE=0 NCCL_P2P_DISABLE=1 NCCL_NET=Socket NCCL_PROTO=LL NCCL_SHM_DISABLE=1 NCCL_HOSTID=exp1-broadcast-ll-rep2-rank1 /home/yunwei37/workspace/nccl-eBPF/nccl-tests/build/broadcast_perf_mpi -b 1024 -e 134217728 -f 2 -g 1 -n 50 -w 10
```

stdout:
```text
# nccl-tests version 2.18.0 nccl-headers=22907 nccl-library=22907
# Collective test starting: broadcast_perf_mpi
# nThread 1 nGpus 1 minBytes 1024 maxBytes 134217728 step: 2(factor) warmup iters: 10 iters: 50 agg iters: 1 validation: 1 graph: 0
#
# Using devices
#  Rank  0 Group  0 Pid 3039771 on        lab device  0 [0000:02:00] NVIDIA GeForce RTX 5090
#  Rank  1 Group  0 Pid 3039772 on        lab device  0 [0000:02:00] NVIDIA GeForce RTX 5090
#
#                                                              out-of-place                       in-place          
#       size         count      type   redop    root     time   algbw   busbw  #wrong     time   algbw   busbw  #wrong 
#        (B)    (elements)                               (us)  (GB/s)  (GB/s)             (us)  (GB/s)  (GB/s)         
        1024           256     float    none       0   188.24    0.01    0.01       0    13.08    0.08    0.08       0
        2048           512     float    none       0    54.61    0.04    0.04       0    54.33    0.04    0.04       0
        4096          1024     float    none       0   135.64    0.03    0.03       0   149.36    0.03    0.03       0
        8192          2048     float    none       0    59.92    0.14    0.14       0    58.40    0.14    0.14       0
       16384          4096     float    none       0   317.84    0.05    0.05       0   286.37    0.06    0.06       0
       32768          8192     float    none       0   682.16    0.05    0.05       0   530.65    0.06    0.06       0
       65536         16384     float    none       0   862.96    0.08    0.08       0   921.19    0.07    0.07       0
      131072         32768     float    none       0   378.43    0.35    0.35       0   861.85    0.15    0.15       0
      262144         65536     float    none       0   797.74    0.33    0.33       0  1285.36    0.20    0.20       0
      524288        131072     float    none       0  1084.22    0.48    0.48       0   839.40    0.62    0.62       0
     1048576        262144     float    none       0  1529.67    0.69    0.69       0  1557.87    0.67    0.67       0
     2097152        524288     float    none       0  3015.08    0.70    0.70       0  3032.09    0.69    0.69       0
     4194304       1048576     float    none       0  5991.65    0.70    0.70       0  5976.70    0.70    0.70       0
     8388608       2097152     float    none       0  12651.3    0.66    0.66       0  11966.0    0.70    0.70       0
    16777216       4194304     float    none       0  24373.8    0.69    0.69       0  24536.3    0.68    0.68       0
    33554432       8388608     float    none       0  48883.9    0.69    0.69       0  48543.3    0.69    0.69       0
    67108864      16777216     float    none       0  97805.7    0.69    0.69       0  97281.5    0.69    0.69       0
   134217728      33554432     float    none       0   195855    0.69    0.69       0   195276    0.69    0.69       0
# Out of bounds values : 0 OK
# Avg bus bandwidth    : 0.389121 
#
# Collective test concluded: broadcast_perf_mpi
#
```

stderr:
```text
Authorization required, but no authorization protocol specified

Authorization required, but no authorization protocol specified

Authorization required, but no authorization protocol specified

Authorization required, but no authorization protocol specified
```

#### exp1-broadcast-default-rep3

Command:
```bash
mpirun --oversubscribe -np 1 /usr/bin/env LD_LIBRARY_PATH=/home/yunwei37/workspace/nccl-eBPF/nccl/build/lib NCCL_TESTS_DEVICE=0 NCCL_P2P_DISABLE=1 NCCL_NET=Socket NCCL_SHM_DISABLE=1 NCCL_HOSTID=exp1-broadcast-default-rep3-rank0 /home/yunwei37/workspace/nccl-eBPF/nccl-tests/build/broadcast_perf_mpi -b 1024 -e 134217728 -f 2 -g 1 -n 50 -w 10 : -np 1 /usr/bin/env LD_LIBRARY_PATH=/home/yunwei37/workspace/nccl-eBPF/nccl/build/lib NCCL_TESTS_DEVICE=0 NCCL_P2P_DISABLE=1 NCCL_NET=Socket NCCL_SHM_DISABLE=1 NCCL_HOSTID=exp1-broadcast-default-rep3-rank1 /home/yunwei37/workspace/nccl-eBPF/nccl-tests/build/broadcast_perf_mpi -b 1024 -e 134217728 -f 2 -g 1 -n 50 -w 10
```

stdout:
```text
# nccl-tests version 2.18.0 nccl-headers=22907 nccl-library=22907
# Collective test starting: broadcast_perf_mpi
# nThread 1 nGpus 1 minBytes 1024 maxBytes 134217728 step: 2(factor) warmup iters: 10 iters: 50 agg iters: 1 validation: 1 graph: 0
#
# Using devices
#  Rank  0 Group  0 Pid 3040128 on        lab device  0 [0000:02:00] NVIDIA GeForce RTX 5090
#  Rank  1 Group  0 Pid 3040129 on        lab device  0 [0000:02:00] NVIDIA GeForce RTX 5090
#
#                                                              out-of-place                       in-place          
#       size         count      type   redop    root     time   algbw   busbw  #wrong     time   algbw   busbw  #wrong 
#        (B)    (elements)                               (us)  (GB/s)  (GB/s)             (us)  (GB/s)  (GB/s)         
        1024           256     float    none       0    53.82    0.02    0.02       0    53.81    0.02    0.02       0
        2048           512     float    none       0    54.73    0.04    0.04       0    54.40    0.04    0.04       0
        4096          1024     float    none       0    57.53    0.07    0.07       0   369.48    0.01    0.01       0
        8192          2048     float    none       0   295.91    0.03    0.03       0   197.73    0.04    0.04       0
       16384          4096     float    none       0   813.18    0.02    0.02       0   841.42    0.02    0.02       0
       32768          8192     float    none       0   200.29    0.16    0.16       0    65.37    0.50    0.50       0
       65536         16384     float    none       0   451.71    0.15    0.15       0   481.86    0.14    0.14       0
      131072         32768     float    none       0  1013.74    0.13    0.13       0   807.10    0.16    0.16       0
      262144         65536     float    none       0   824.11    0.32    0.32       0   432.84    0.61    0.61       0
      524288        131072     float    none       0   510.49    1.03    1.03       0   880.86    0.60    0.60       0
     1048576        262144     float    none       0   506.24    2.07    2.07       0   656.69    1.60    1.60       0
     2097152        524288     float    none       0  1293.28    1.62    1.62       0   999.51    2.10    2.10       0
     4194304       1048576     float    none       0  1474.41    2.84    2.84       0  1285.00    3.26    3.26       0
     8388608       2097152     float    none       0  2695.85    3.11    3.11       0  2673.03    3.14    3.14       0
    16777216       4194304     float    none       0  4918.20    3.41    3.41       0  4823.65    3.48    3.48       0
    33554432       8388608     float    none       0  8966.50    3.74    3.74       0  9017.07    3.72    3.72       0
    67108864      16777216     float    none       0  17880.2    3.75    3.75       0  17809.8    3.77    3.77       0
   134217728      33554432     float    none       0  35578.7    3.77    3.77       0  35349.2    3.80    3.80       0
# Out of bounds values : 0 OK
# Avg bus bandwidth    : 1.47994 
#
# Collective test concluded: broadcast_perf_mpi
#
```

stderr:
```text
Authorization required, but no authorization protocol specified

Authorization required, but no authorization protocol specified

Authorization required, but no authorization protocol specified

Authorization required, but no authorization protocol specified
```

#### exp1-broadcast-simple-rep3

Command:
```bash
mpirun --oversubscribe -np 1 /usr/bin/env LD_LIBRARY_PATH=/home/yunwei37/workspace/nccl-eBPF/nccl/build/lib NCCL_TESTS_DEVICE=0 NCCL_P2P_DISABLE=1 NCCL_NET=Socket NCCL_PROTO=Simple NCCL_SHM_DISABLE=1 NCCL_HOSTID=exp1-broadcast-simple-rep3-rank0 /home/yunwei37/workspace/nccl-eBPF/nccl-tests/build/broadcast_perf_mpi -b 1024 -e 134217728 -f 2 -g 1 -n 50 -w 10 : -np 1 /usr/bin/env LD_LIBRARY_PATH=/home/yunwei37/workspace/nccl-eBPF/nccl/build/lib NCCL_TESTS_DEVICE=0 NCCL_P2P_DISABLE=1 NCCL_NET=Socket NCCL_PROTO=Simple NCCL_SHM_DISABLE=1 NCCL_HOSTID=exp1-broadcast-simple-rep3-rank1 /home/yunwei37/workspace/nccl-eBPF/nccl-tests/build/broadcast_perf_mpi -b 1024 -e 134217728 -f 2 -g 1 -n 50 -w 10
```

stdout:
```text
# nccl-tests version 2.18.0 nccl-headers=22907 nccl-library=22907
# Collective test starting: broadcast_perf_mpi
# nThread 1 nGpus 1 minBytes 1024 maxBytes 134217728 step: 2(factor) warmup iters: 10 iters: 50 agg iters: 1 validation: 1 graph: 0
#
# Using devices
#  Rank  0 Group  0 Pid 3040220 on        lab device  0 [0000:02:00] NVIDIA GeForce RTX 5090
#  Rank  1 Group  0 Pid 3040221 on        lab device  0 [0000:02:00] NVIDIA GeForce RTX 5090
#
#                                                              out-of-place                       in-place          
#       size         count      type   redop    root     time   algbw   busbw  #wrong     time   algbw   busbw  #wrong 
#        (B)    (elements)                               (us)  (GB/s)  (GB/s)             (us)  (GB/s)  (GB/s)         
        1024           256     float    none       0   148.90    0.01    0.01       0    60.97    0.02    0.02       0
        2048           512     float    none       0    59.61    0.03    0.03       0    59.44    0.03    0.03       0
        4096          1024     float    none       0    59.86    0.07    0.07       0    61.77    0.07    0.07       0
        8192          2048     float    none       0    60.23    0.14    0.14       0    62.45    0.13    0.13       0
       16384          4096     float    none       0    63.52    0.26    0.26       0    61.05    0.27    0.27       0
       32768          8192     float    none       0   203.05    0.16    0.16       0   198.62    0.16    0.16       0
       65536         16384     float    none       0    53.48    1.23    1.23       0   642.62    0.10    0.10       0
      131072         32768     float    none       0   740.20    0.18    0.18       0   232.68    0.56    0.56       0
      262144         65536     float    none       0   393.11    0.67    0.67       0   367.87    0.71    0.71       0
      524288        131072     float    none       0   806.45    0.65    0.65       0   671.30    0.78    0.78       0
     1048576        262144     float    none       0  1113.75    0.94    0.94       0   505.96    2.07    2.07       0
     2097152        524288     float    none       0  1035.74    2.02    2.02       0  1420.75    1.48    1.48       0
     4194304       1048576     float    none       0  1260.12    3.33    3.33       0  1223.70    3.43    3.43       0
     8388608       2097152     float    none       0  2490.62    3.37    3.37       0  2674.31    3.14    3.14       0
    16777216       4194304     float    none       0  4988.53    3.36    3.36       0  4950.24    3.39    3.39       0
    33554432       8388608     float    none       0  8991.87    3.73    3.73       0  8951.87    3.75    3.75       0
    67108864      16777216     float    none       0  17897.9    3.75    3.75       0  17896.2    3.75    3.75       0
   134217728      33554432     float    none       0  35428.3    3.79    3.79       0  35556.8    3.77    3.77       0
# Out of bounds values : 0 OK
# Avg bus bandwidth    : 1.536 
#
# Collective test concluded: broadcast_perf_mpi
#
```

stderr:
```text
Authorization required, but no authorization protocol specified

Authorization required, but no authorization protocol specified

Authorization required, but no authorization protocol specified

Authorization required, but no authorization protocol specified
```

#### exp1-broadcast-ll-rep3

Command:
```bash
mpirun --oversubscribe -np 1 /usr/bin/env LD_LIBRARY_PATH=/home/yunwei37/workspace/nccl-eBPF/nccl/build/lib NCCL_TESTS_DEVICE=0 NCCL_P2P_DISABLE=1 NCCL_NET=Socket NCCL_PROTO=LL NCCL_SHM_DISABLE=1 NCCL_HOSTID=exp1-broadcast-ll-rep3-rank0 /home/yunwei37/workspace/nccl-eBPF/nccl-tests/build/broadcast_perf_mpi -b 1024 -e 134217728 -f 2 -g 1 -n 50 -w 10 : -np 1 /usr/bin/env LD_LIBRARY_PATH=/home/yunwei37/workspace/nccl-eBPF/nccl/build/lib NCCL_TESTS_DEVICE=0 NCCL_P2P_DISABLE=1 NCCL_NET=Socket NCCL_PROTO=LL NCCL_SHM_DISABLE=1 NCCL_HOSTID=exp1-broadcast-ll-rep3-rank1 /home/yunwei37/workspace/nccl-eBPF/nccl-tests/build/broadcast_perf_mpi -b 1024 -e 134217728 -f 2 -g 1 -n 50 -w 10
```

stdout:
```text
# nccl-tests version 2.18.0 nccl-headers=22907 nccl-library=22907
# Collective test starting: broadcast_perf_mpi
# nThread 1 nGpus 1 minBytes 1024 maxBytes 134217728 step: 2(factor) warmup iters: 10 iters: 50 agg iters: 1 validation: 1 graph: 0
#
# Using devices
#  Rank  0 Group  0 Pid 3040341 on        lab device  0 [0000:02:00] NVIDIA GeForce RTX 5090
#  Rank  1 Group  0 Pid 3040342 on        lab device  0 [0000:02:00] NVIDIA GeForce RTX 5090
#
#                                                              out-of-place                       in-place          
#       size         count      type   redop    root     time   algbw   busbw  #wrong     time   algbw   busbw  #wrong 
#        (B)    (elements)                               (us)  (GB/s)  (GB/s)             (us)  (GB/s)  (GB/s)         
        1024           256     float    none       0   187.97    0.01    0.01       0    56.07    0.02    0.02       0
        2048           512     float    none       0    56.89    0.04    0.04       0    56.45    0.04    0.04       0
        4096          1024     float    none       0   534.10    0.01    0.01       0   358.53    0.01    0.01       0
        8192          2048     float    none       0  1057.68    0.01    0.01       0   311.96    0.03    0.03       0
       16384          4096     float    none       0   807.57    0.02    0.02       0   821.20    0.02    0.02       0
       32768          8192     float    none       0   293.46    0.11    0.11       0   691.26    0.05    0.05       0
       65536         16384     float    none       0   370.48    0.18    0.18       0   811.20    0.08    0.08       0
      131072         32768     float    none       0   315.61    0.42    0.42       0   180.01    0.73    0.73       0
      262144         65536     float    none       0   764.84    0.34    0.34       0   929.40    0.28    0.28       0
      524288        131072     float    none       0   858.39    0.61    0.61       0  1217.30    0.43    0.43       0
     1048576        262144     float    none       0  2330.25    0.45    0.45       0  1606.49    0.65    0.65       0
     2097152        524288     float    none       0  2916.77    0.72    0.72       0  2894.57    0.72    0.72       0
     4194304       1048576     float    none       0  6561.21    0.64    0.64       0  6796.11    0.62    0.62       0
     8388608       2097152     float    none       0  12737.9    0.66    0.66       0  11996.5    0.70    0.70       0
    16777216       4194304     float    none       0  24428.4    0.69    0.69       0  24424.3    0.69    0.69       0
    33554432       8388608     float    none       0  48782.6    0.69    0.69       0  49255.5    0.68    0.68       0
    67108864      16777216     float    none       0  98851.6    0.68    0.68       0  97637.1    0.69    0.69       0
   134217728      33554432     float    none       0   196804    0.68    0.68       0   196398    0.68    0.68       0
# Out of bounds values : 0 OK
# Avg bus bandwidth    : 0.390294 
#
# Collective test concluded: broadcast_perf_mpi
#
```

stderr:
```text
Authorization required, but no authorization protocol specified

Authorization required, but no authorization protocol specified

Authorization required, but no authorization protocol specified

Authorization required, but no authorization protocol specified
```


## Experiment 2: SHM Transport AllReduce

Primary comparison metric for verdicts: out-of-place time (us). Lower is better. Reported busbw remains as a secondary view.

### Summary Table: out-of-place time

| Size | DEFAULT time (us) | Simple time (us) | Tree+Simple time (us) | Simple vs DEFAULT | Simple >2σ? | Tree+Simple vs DEFAULT | Tree+Simple >2σ? | DEFAULT genuinely slower? |
| --- | --- | --- | --- | --- | --- | --- | --- | --- |
| 8B | 4355.23±0.21 | 4355.69±0.80 | 4346.35±25.37 | +0.01% | No | -0.20% | No | No |
| 16B | 4340.20±25.83 | 4355.80±0.26 | 4316.41±0.17 | +0.36% | No | -0.55% | No | No |
| 32B | 4354.84±0.82 | 4356.07±0.23 | 4331.84±25.32 | +0.03% | No | -0.53% | No | No |
| 64B | 4354.31±0.89 | 4356.20±0.55 | 4346.12±25.85 | +0.04% | No | -0.19% | No | No |
| 128B | 4355.08±0.76 | 4385.11±50.28 | 4346.07±26.30 | +0.69% | No | -0.21% | No | No |
| 256B | 4354.70±0.84 | 4355.81±1.37 | 4346.46±25.78 | +0.03% | No | -0.19% | No | No |
| 512B | 4355.56±0.61 | 4316.53±0.36 | 4331.69±26.45 | -0.90% | Yes | -0.55% | No | Simple |
| 1KB | 4356.46±0.44 | 4331.47±26.21 | 4361.23±0.34 | -0.57% | No | +0.11% | Yes | No |
| 2KB | 4356.61±0.22 | 4316.20±0.21 | 4346.08±25.77 | -0.93% | Yes | -0.24% | No | Simple |
| 4KB | 4359.14±0.16 | 4316.67±0.18 | 4331.34±25.86 | -0.97% | Yes | -0.64% | No | Simple |
| 8KB | 4362.96±0.23 | 4316.45±0.12 | 4346.37±26.10 | -1.07% | Yes | -0.38% | No | Simple |
| 16KB | 4363.63±0.47 | 4346.83±26.35 | 4361.00±0.14 | -0.39% | No | -0.06% | Yes | Tree+Simple |
| 32KB | 4400.95±50.78 | 4360.38±0.34 | 4331.44±25.70 | -0.92% | No | -1.58% | No | No |
| 64KB | 4371.27±1.21 | 4362.99±0.40 | 4347.68±26.12 | -0.19% | Yes | -0.54% | No | Simple |
| 128KB | 4363.47±0.18 | 4362.93±0.61 | 4365.91±0.91 | -0.01% | No | +0.06% | Yes | No |
| 256KB | 4363.68±0.27 | 4362.30±0.54 | 4366.61±0.35 | -0.03% | Yes | +0.07% | Yes | Simple |
| 512KB | 4364.53±1.31 | 4364.15±0.38 | 4367.34±0.96 | -0.01% | No | +0.06% | No | No |
| 1MB | 4366.38±1.17 | 4365.32±0.89 | 4368.63±1.01 | -0.02% | No | +0.05% | No | No |
| 2MB | 4370.75±1.52 | 4369.93±0.26 | 4372.12±0.66 | -0.02% | No | +0.03% | No | No |
| 4MB | 4374.00±0.76 | 4388.78±26.63 | 4377.87±0.33 | +0.34% | No | +0.09% | Yes | No |
| 8MB | 4423.71±21.89 | 4420.83±56.43 | 4419.81±28.14 | -0.07% | No | -0.09% | No | No |
| 16MB | 7599.36±52.11 | 7660.44±161.89 | 8397.03±86.50 | +0.80% | No | +10.50% | Yes | No |
| 32MB | 14942.77±82.71 | 15217.57±299.82 | 15255.90±60.97 | +1.84% | No | +2.10% | Yes | No |
| 64MB | 29175.90±108.94 | 29355.63±148.84 | 30435.63±201.07 | +0.62% | No | +4.32% | Yes | No |
| 128MB | 57923.63±179.80 | 58380.83±457.12 | 62250.37±1236.99 | +0.79% | No | +7.47% | Yes | No |

### Secondary Table: out-of-place busbw

| Size | DEFAULT busbw (GB/s) | Simple busbw (GB/s) | Tree+Simple busbw (GB/s) | Simple vs DEFAULT | Tree+Simple vs DEFAULT |
| --- | --- | --- | --- | --- | --- |
| 8B | 0.000±0.000 | 0.000±0.000 | 0.000±0.000 | +nan% | +nan% |
| 16B | 0.000±0.000 | 0.000±0.000 | 0.000±0.000 | +nan% | +nan% |
| 32B | 0.000±0.000 | 0.000±0.000 | 0.000±0.000 | +nan% | +nan% |
| 64B | 0.000±0.000 | 0.000±0.000 | 0.000±0.000 | +nan% | +nan% |
| 128B | 0.000±0.000 | 0.000±0.000 | 0.000±0.000 | +nan% | +nan% |
| 256B | 0.000±0.000 | 0.000±0.000 | 0.000±0.000 | +nan% | +nan% |
| 512B | 0.000±0.000 | 0.000±0.000 | 0.000±0.000 | +nan% | +nan% |
| 1KB | 0.000±0.000 | 0.000±0.000 | 0.000±0.000 | +nan% | +nan% |
| 2KB | 0.000±0.000 | 0.000±0.000 | 0.000±0.000 | +nan% | +nan% |
| 4KB | 0.000±0.000 | 0.000±0.000 | 0.000±0.000 | +nan% | +nan% |
| 8KB | 0.000±0.000 | 0.000±0.000 | 0.000±0.000 | +nan% | +nan% |
| 16KB | 0.000±0.000 | 0.000±0.000 | 0.000±0.000 | +nan% | +nan% |
| 32KB | 0.010±0.000 | 0.010±0.000 | 0.010±0.000 | +0.00% | +0.00% |
| 64KB | 0.010±0.000 | 0.020±0.000 | 0.020±0.000 | +100.00% | +100.00% |
| 128KB | 0.030±0.000 | 0.030±0.000 | 0.030±0.000 | +0.00% | +0.00% |
| 256KB | 0.060±0.000 | 0.060±0.000 | 0.060±0.000 | +0.00% | +0.00% |
| 512KB | 0.120±0.000 | 0.120±0.000 | 0.120±0.000 | +0.00% | +0.00% |
| 1MB | 0.240±0.000 | 0.240±0.000 | 0.240±0.000 | +0.00% | +0.00% |
| 2MB | 0.480±0.000 | 0.480±0.000 | 0.480±0.000 | +0.00% | +0.00% |
| 4MB | 0.960±0.000 | 0.957±0.006 | 0.960±0.000 | -0.35% | +0.00% |
| 8MB | 1.897±0.012 | 1.897±0.023 | 1.897±0.012 | +0.00% | +0.00% |
| 16MB | 2.207±0.015 | 2.190±0.044 | 2.000±0.020 | -0.76% | -9.37% |
| 32MB | 2.247±0.015 | 2.207±0.040 | 2.197±0.012 | -1.78% | -2.23% |
| 64MB | 2.300±0.010 | 2.287±0.012 | 2.203±0.012 | -0.58% | -4.20% |
| 128MB | 2.317±0.012 | 2.300±0.020 | 2.153±0.045 | -0.72% | -7.05% |

### Verdict

- 512B: DEFAULT is genuinely slower than Simple.
- 2KB: DEFAULT is genuinely slower than Simple.
- 4KB: DEFAULT is genuinely slower than Simple.
- 8KB: DEFAULT is genuinely slower than Simple.
- 16KB: DEFAULT is genuinely slower than Tree+Simple.
- 64KB: DEFAULT is genuinely slower than Simple.
- 256KB: DEFAULT is genuinely slower than Simple.

### Raw Output

### Raw Output: DEFAULT vs Simple vs Tree+Simple

#### exp2-shm-allreduce-default-rep1

Command:
```bash
mpirun --oversubscribe -np 1 /usr/bin/env LD_LIBRARY_PATH=/home/yunwei37/workspace/nccl-eBPF/nccl/build/lib NCCL_TESTS_DEVICE=0 NCCL_P2P_DISABLE=1 NCCL_NET=Socket NCCL_HOSTID=exp2-shm-allreduce-default-rep1-rank0 /home/yunwei37/workspace/nccl-eBPF/nccl-tests/build/all_reduce_perf_mpi -b 8 -e 134217728 -f 2 -g 1 -n 50 -w 10 : -np 1 /usr/bin/env LD_LIBRARY_PATH=/home/yunwei37/workspace/nccl-eBPF/nccl/build/lib NCCL_TESTS_DEVICE=0 NCCL_P2P_DISABLE=1 NCCL_NET=Socket NCCL_HOSTID=exp2-shm-allreduce-default-rep1-rank1 /home/yunwei37/workspace/nccl-eBPF/nccl-tests/build/all_reduce_perf_mpi -b 8 -e 134217728 -f 2 -g 1 -n 50 -w 10
```

stdout:
```text
# nccl-tests version 2.18.0 nccl-headers=22907 nccl-library=22907
# Collective test starting: all_reduce_perf_mpi
# nThread 1 nGpus 1 minBytes 8 maxBytes 134217728 step: 2(factor) warmup iters: 10 iters: 50 agg iters: 1 validation: 1 graph: 0
#
# Using devices
#  Rank  0 Group  0 Pid 3040962 on        lab device  0 [0000:02:00] NVIDIA GeForce RTX 5090
#  Rank  1 Group  0 Pid 3040963 on        lab device  0 [0000:02:00] NVIDIA GeForce RTX 5090
#
#                                                              out-of-place                       in-place          
#       size         count      type   redop    root     time   algbw   busbw  #wrong     time   algbw   busbw  #wrong 
#        (B)    (elements)                               (us)  (GB/s)  (GB/s)             (us)  (GB/s)  (GB/s)         
           8             2     float     sum      -1  4355.47    0.00    0.00       0  4310.58    0.00    0.00       0
          16             4     float     sum      -1  4355.19    0.00    0.00       0  4310.03    0.00    0.00       0
          32             8     float     sum      -1  4355.42    0.00    0.00       0  4355.21    0.00    0.00       0
          64            16     float     sum      -1  4355.32    0.00    0.00       0  4355.27    0.00    0.00       0
         128            32     float     sum      -1  4355.17    0.00    0.00       0  4355.43    0.00    0.00       0
         256            64     float     sum      -1  4355.21    0.00    0.00       0  4356.27    0.00    0.00       0
         512           128     float     sum      -1  4355.79    0.00    0.00       0  4355.71    0.00    0.00       0
        1024           256     float     sum      -1  4356.33    0.00    0.00       0  4355.92    0.00    0.00       0
        2048           512     float     sum      -1  4356.62    0.00    0.00       0  4356.51    0.00    0.00       0
        4096          1024     float     sum      -1  4358.96    0.00    0.00       0  4357.61    0.00    0.00       0
        8192          2048     float     sum      -1  4362.71    0.00    0.00       0  4362.95    0.00    0.00       0
       16384          4096     float     sum      -1  4363.36    0.00    0.00       0  4361.92    0.00    0.00       0
       32768          8192     float     sum      -1  4459.58    0.01    0.01       0  4370.51    0.01    0.01       0
       65536         16384     float     sum      -1  4372.21    0.01    0.01       0  4370.91    0.01    0.01       0
      131072         32768     float     sum      -1  4363.32    0.03    0.03       0  4363.48    0.03    0.03       0
      262144         65536     float     sum      -1  4363.56    0.06    0.06       0  4362.14    0.06    0.06       0
      524288        131072     float     sum      -1  4364.33    0.12    0.12       0  4364.05    0.12    0.12       0
     1048576        262144     float     sum      -1  4365.17    0.24    0.24       0  4364.43    0.24    0.24       0
     2097152        524288     float     sum      -1  4370.47    0.48    0.48       0  4369.65    0.48    0.48       0
     4194304       1048576     float     sum      -1  4373.67    0.96    0.96       0  4372.55    0.96    0.96       0
     8388608       2097152     float     sum      -1  4399.41    1.91    1.91       0  4396.34    1.91    1.91       0
    16777216       4194304     float     sum      -1  7655.57    2.19    2.19       0  7599.53    2.21    2.21       0
    33554432       8388608     float     sum      -1  14935.2    2.25    2.25       0  15067.8    2.23    2.23       0
    67108864      16777216     float     sum      -1  29280.9    2.29    2.29       0  29139.2    2.30    2.30       0
   134217728      33554432     float     sum      -1  57980.0    2.31    2.31       0  57812.0    2.32    2.32       0
# Out of bounds values : 0 OK
# Avg bus bandwidth    : 0.515158 
#
# Collective test concluded: all_reduce_perf_mpi
#
```

stderr:
```text
Authorization required, but no authorization protocol specified

Authorization required, but no authorization protocol specified

Authorization required, but no authorization protocol specified

Authorization required, but no authorization protocol specified
```

#### exp2-shm-allreduce-simple-rep1

Command:
```bash
mpirun --oversubscribe -np 1 /usr/bin/env LD_LIBRARY_PATH=/home/yunwei37/workspace/nccl-eBPF/nccl/build/lib NCCL_TESTS_DEVICE=0 NCCL_P2P_DISABLE=1 NCCL_NET=Socket NCCL_PROTO=Simple NCCL_HOSTID=exp2-shm-allreduce-simple-rep1-rank0 /home/yunwei37/workspace/nccl-eBPF/nccl-tests/build/all_reduce_perf_mpi -b 8 -e 134217728 -f 2 -g 1 -n 50 -w 10 : -np 1 /usr/bin/env LD_LIBRARY_PATH=/home/yunwei37/workspace/nccl-eBPF/nccl/build/lib NCCL_TESTS_DEVICE=0 NCCL_P2P_DISABLE=1 NCCL_NET=Socket NCCL_PROTO=Simple NCCL_HOSTID=exp2-shm-allreduce-simple-rep1-rank1 /home/yunwei37/workspace/nccl-eBPF/nccl-tests/build/all_reduce_perf_mpi -b 8 -e 134217728 -f 2 -g 1 -n 50 -w 10
```

stdout:
```text
# nccl-tests version 2.18.0 nccl-headers=22907 nccl-library=22907
# Collective test starting: all_reduce_perf_mpi
# nThread 1 nGpus 1 minBytes 8 maxBytes 134217728 step: 2(factor) warmup iters: 10 iters: 50 agg iters: 1 validation: 1 graph: 0
#
# Using devices
#  Rank  0 Group  0 Pid 3041182 on        lab device  0 [0000:02:00] NVIDIA GeForce RTX 5090
#  Rank  1 Group  0 Pid 3041183 on        lab device  0 [0000:02:00] NVIDIA GeForce RTX 5090
#
#                                                              out-of-place                       in-place          
#       size         count      type   redop    root     time   algbw   busbw  #wrong     time   algbw   busbw  #wrong 
#        (B)    (elements)                               (us)  (GB/s)  (GB/s)             (us)  (GB/s)  (GB/s)         
           8             2     float     sum      -1  4356.37    0.00    0.00       0  4354.92    0.00    0.00       0
          16             4     float     sum      -1  4356.08    0.00    0.00       0  4354.88    0.00    0.00       0
          32             8     float     sum      -1  4356.33    0.00    0.00       0  4356.34    0.00    0.00       0
          64            16     float     sum      -1  4356.75    0.00    0.00       0  4356.67    0.00    0.00       0
         128            32     float     sum      -1  4356.13    0.00    0.00       0  4356.63    0.00    0.00       0
         256            64     float     sum      -1  4357.00    0.00    0.00       0  4355.80    0.00    0.00       0
         512           128     float     sum      -1  4316.22    0.00    0.00       0  4316.19    0.00    0.00       0
        1024           256     float     sum      -1  4316.31    0.00    0.00       0  4360.74    0.00    0.00       0
        2048           512     float     sum      -1  4316.16    0.00    0.00       0  4316.05    0.00    0.00       0
        4096          1024     float     sum      -1  4316.72    0.00    0.00       0  4316.83    0.00    0.00       0
        8192          2048     float     sum      -1  4316.57    0.00    0.00       0  4315.97    0.00    0.00       0
       16384          4096     float     sum      -1  4360.86    0.00    0.00       0  4316.91    0.00    0.00       0
       32768          8192     float     sum      -1  4360.77    0.01    0.01       0  4359.38    0.01    0.01       0
       65536         16384     float     sum      -1  4362.53    0.02    0.02       0  4362.81    0.02    0.02       0
      131072         32768     float     sum      -1  4362.70    0.03    0.03       0  4363.03    0.03    0.03       0
      262144         65536     float     sum      -1  4362.83    0.06    0.06       0  4362.91    0.06    0.06       0
      524288        131072     float     sum      -1  4363.75    0.12    0.12       0  4364.00    0.12    0.12       0
     1048576        262144     float     sum      -1  4364.30    0.24    0.24       0  4365.70    0.24    0.24       0
     2097152        524288     float     sum      -1  4370.22    0.48    0.48       0  4367.39    0.48    0.48       0
     4194304       1048576     float     sum      -1  4373.15    0.96    0.96       0  4372.88    0.96    0.96       0
     8388608       2097152     float     sum      -1  4389.48    1.91    1.91       0  4436.36    1.89    1.89       0
    16777216       4194304     float     sum      -1  7728.94    2.17    2.17       0  7722.70    2.17    2.17       0
    33554432       8388608     float     sum      -1  15053.9    2.23    2.23       0  15042.8    2.23    2.23       0
    67108864      16777216     float     sum      -1  29493.9    2.28    2.28       0  29370.8    2.28    2.28       0
   134217728      33554432     float     sum      -1  58786.8    2.28    2.28       0  58380.2    2.30    2.30       0
# Out of bounds values : 0 OK
# Avg bus bandwidth    : 0.511731 
#
# Collective test concluded: all_reduce_perf_mpi
#
```

stderr:
```text
Authorization required, but no authorization protocol specified

Authorization required, but no authorization protocol specified

Authorization required, but no authorization protocol specified

Authorization required, but no authorization protocol specified
```

#### exp2-shm-allreduce-tree-simple-rep1

Command:
```bash
mpirun --oversubscribe -np 1 /usr/bin/env LD_LIBRARY_PATH=/home/yunwei37/workspace/nccl-eBPF/nccl/build/lib NCCL_TESTS_DEVICE=0 NCCL_P2P_DISABLE=1 NCCL_NET=Socket NCCL_ALGO=Tree NCCL_PROTO=Simple NCCL_HOSTID=exp2-shm-allreduce-tree-simple-rep1-rank0 /home/yunwei37/workspace/nccl-eBPF/nccl-tests/build/all_reduce_perf_mpi -b 8 -e 134217728 -f 2 -g 1 -n 50 -w 10 : -np 1 /usr/bin/env LD_LIBRARY_PATH=/home/yunwei37/workspace/nccl-eBPF/nccl/build/lib NCCL_TESTS_DEVICE=0 NCCL_P2P_DISABLE=1 NCCL_NET=Socket NCCL_ALGO=Tree NCCL_PROTO=Simple NCCL_HOSTID=exp2-shm-allreduce-tree-simple-rep1-rank1 /home/yunwei37/workspace/nccl-eBPF/nccl-tests/build/all_reduce_perf_mpi -b 8 -e 134217728 -f 2 -g 1 -n 50 -w 10
```

stdout:
```text
# nccl-tests version 2.18.0 nccl-headers=22907 nccl-library=22907
# Collective test starting: all_reduce_perf_mpi
# nThread 1 nGpus 1 minBytes 8 maxBytes 134217728 step: 2(factor) warmup iters: 10 iters: 50 agg iters: 1 validation: 1 graph: 0
#
# Using devices
#  Rank  0 Group  0 Pid 3041453 on        lab device  0 [0000:02:00] NVIDIA GeForce RTX 5090
#  Rank  1 Group  0 Pid 3041454 on        lab device  0 [0000:02:00] NVIDIA GeForce RTX 5090
#
#                                                              out-of-place                       in-place          
#       size         count      type   redop    root     time   algbw   busbw  #wrong     time   algbw   busbw  #wrong 
#        (B)    (elements)                               (us)  (GB/s)  (GB/s)             (us)  (GB/s)  (GB/s)         
           8             2     float     sum      -1  4361.11    0.00    0.00       0  4316.18    0.00    0.00       0
          16             4     float     sum      -1  4316.61    0.00    0.00       0  4315.98    0.00    0.00       0
          32             8     float     sum      -1  4361.06    0.00    0.00       0  4316.12    0.00    0.00       0
          64            16     float     sum      -1  4360.90    0.00    0.00       0  4316.09    0.00    0.00       0
         128            32     float     sum      -1  4315.70    0.00    0.00       0  4316.25    0.00    0.00       0
         256            64     float     sum      -1  4361.29    0.00    0.00       0  4316.30    0.00    0.00       0
         512           128     float     sum      -1  4362.24    0.00    0.00       0  4316.43    0.00    0.00       0
        1024           256     float     sum      -1  4361.06    0.00    0.00       0  4316.67    0.00    0.00       0
        2048           512     float     sum      -1  4361.08    0.00    0.00       0  4315.93    0.00    0.00       0
        4096          1024     float     sum      -1  4316.40    0.00    0.00       0  4316.39    0.00    0.00       0
        8192          2048     float     sum      -1  4361.31    0.00    0.00       0  4361.28    0.00    0.00       0
       16384          4096     float     sum      -1  4360.86    0.00    0.00       0  4316.12    0.00    0.00       0
       32768          8192     float     sum      -1  4316.41    0.01    0.01       0  4316.11    0.01    0.01       0
       65536         16384     float     sum      -1  4362.49    0.02    0.02       0  4362.19    0.02    0.02       0
      131072         32768     float     sum      -1  4366.39    0.03    0.03       0  4366.14    0.03    0.03       0
      262144         65536     float     sum      -1  4366.58    0.06    0.06       0  4366.58    0.06    0.06       0
      524288        131072     float     sum      -1  4367.72    0.12    0.12       0  4366.74    0.12    0.12       0
     1048576        262144     float     sum      -1  4368.41    0.24    0.24       0  4368.67    0.24    0.24       0
     2097152        524288     float     sum      -1  4371.83    0.48    0.48       0  4371.28    0.48    0.48       0
     4194304       1048576     float     sum      -1  4377.53    0.96    0.96       0  4376.87    0.96    0.96       0
     8388608       2097152     float     sum      -1  4387.37    1.91    1.91       0  4596.30    1.83    1.83       0
    16777216       4194304     float     sum      -1  8478.84    1.98    1.98       0  8560.13    1.96    1.96       0
    33554432       8388608     float     sum      -1  15291.1    2.19    2.19       0  15345.3    2.19    2.19       0
    67108864      16777216     float     sum      -1  30667.8    2.19    2.19       0  30613.6    2.19    2.19       0
   134217728      33554432     float     sum      -1  62299.6    2.15    2.15       0  61807.6    2.17    2.17       0
# Out of bounds values : 0 OK
# Avg bus bandwidth    : 0.49199 
#
# Collective test concluded: all_reduce_perf_mpi
#
```

stderr:
```text
Authorization required, but no authorization protocol specified

Authorization required, but no authorization protocol specified

Authorization required, but no authorization protocol specified

Authorization required, but no authorization protocol specified
```

#### exp2-shm-allreduce-default-rep2

Command:
```bash
mpirun --oversubscribe -np 1 /usr/bin/env LD_LIBRARY_PATH=/home/yunwei37/workspace/nccl-eBPF/nccl/build/lib NCCL_TESTS_DEVICE=0 NCCL_P2P_DISABLE=1 NCCL_NET=Socket NCCL_HOSTID=exp2-shm-allreduce-default-rep2-rank0 /home/yunwei37/workspace/nccl-eBPF/nccl-tests/build/all_reduce_perf_mpi -b 8 -e 134217728 -f 2 -g 1 -n 50 -w 10 : -np 1 /usr/bin/env LD_LIBRARY_PATH=/home/yunwei37/workspace/nccl-eBPF/nccl/build/lib NCCL_TESTS_DEVICE=0 NCCL_P2P_DISABLE=1 NCCL_NET=Socket NCCL_HOSTID=exp2-shm-allreduce-default-rep2-rank1 /home/yunwei37/workspace/nccl-eBPF/nccl-tests/build/all_reduce_perf_mpi -b 8 -e 134217728 -f 2 -g 1 -n 50 -w 10
```

stdout:
```text
# nccl-tests version 2.18.0 nccl-headers=22907 nccl-library=22907
# Collective test starting: all_reduce_perf_mpi
# nThread 1 nGpus 1 minBytes 8 maxBytes 134217728 step: 2(factor) warmup iters: 10 iters: 50 agg iters: 1 validation: 1 graph: 0
#
# Using devices
#  Rank  0 Group  0 Pid 3041668 on        lab device  0 [0000:02:00] NVIDIA GeForce RTX 5090
#  Rank  1 Group  0 Pid 3041669 on        lab device  0 [0000:02:00] NVIDIA GeForce RTX 5090
#
#                                                              out-of-place                       in-place          
#       size         count      type   redop    root     time   algbw   busbw  #wrong     time   algbw   busbw  #wrong 
#        (B)    (elements)                               (us)  (GB/s)  (GB/s)             (us)  (GB/s)  (GB/s)         
           8             2     float     sum      -1  4355.08    0.00    0.00       0  4355.43    0.00    0.00       0
          16             4     float     sum      -1  4310.37    0.00    0.00       0  4354.84    0.00    0.00       0
          32             8     float     sum      -1  4355.19    0.00    0.00       0  4354.97    0.00    0.00       0
          64            16     float     sum      -1  4353.65    0.00    0.00       0  4353.91    0.00    0.00       0
         128            32     float     sum      -1  4355.79    0.00    0.00       0  4355.20    0.00    0.00       0
         256            64     float     sum      -1  4355.15    0.00    0.00       0  4355.06    0.00    0.00       0
         512           128     float     sum      -1  4354.87    0.00    0.00       0  4354.50    0.00    0.00       0
        1024           256     float     sum      -1  4356.95    0.00    0.00       0  4355.50    0.00    0.00       0
        2048           512     float     sum      -1  4356.39    0.00    0.00       0  4356.73    0.00    0.00       0
        4096          1024     float     sum      -1  4359.17    0.00    0.00       0  4358.83    0.00    0.00       0
        8192          2048     float     sum      -1  4363.02    0.00    0.00       0  4361.95    0.00    0.00       0
       16384          4096     float     sum      -1  4364.18    0.00    0.00       0  4363.76    0.00    0.00       0
       32768          8192     float     sum      -1  4371.55    0.01    0.01       0  4371.05    0.01    0.01       0
       65536         16384     float     sum      -1  4371.70    0.01    0.01       0  4370.48    0.01    0.01       0
      131072         32768     float     sum      -1  4363.67    0.03    0.03       0  4364.10    0.03    0.03       0
      262144         65536     float     sum      -1  4363.50    0.06    0.06       0  4363.89    0.06    0.06       0
      524288        131072     float     sum      -1  4363.33    0.12    0.12       0  4363.19    0.12    0.12       0
     1048576        262144     float     sum      -1  4366.47    0.24    0.24       0  4364.65    0.24    0.24       0
     2097152        524288     float     sum      -1  4369.38    0.48    0.48       0  4367.93    0.48    0.48       0
     4194304       1048576     float     sum      -1  4373.47    0.96    0.96       0  4373.56    0.96    0.96       0
     8388608       2097152     float     sum      -1  4429.85    1.89    1.89       0  4385.65    1.91    1.91       0
    16777216       4194304     float     sum      -1  7552.65    2.22    2.22       0  7648.65    2.19    2.19       0
    33554432       8388608     float     sum      -1  14864.1    2.26    2.26       0  14726.7    2.28    2.28       0
    67108864      16777216     float     sum      -1  29183.4    2.30    2.30       0  29464.3    2.28    2.28       0
   134217728      33554432     float     sum      -1  58068.5    2.31    2.31       0  58220.7    2.31    2.31       0
# Out of bounds values : 0 OK
# Avg bus bandwidth    : 0.515802 
#
# Collective test concluded: all_reduce_perf_mpi
#
```

stderr:
```text
Authorization required, but no authorization protocol specified

Authorization required, but no authorization protocol specified

Authorization required, but no authorization protocol specified

Authorization required, but no authorization protocol specified
```

#### exp2-shm-allreduce-simple-rep2

Command:
```bash
mpirun --oversubscribe -np 1 /usr/bin/env LD_LIBRARY_PATH=/home/yunwei37/workspace/nccl-eBPF/nccl/build/lib NCCL_TESTS_DEVICE=0 NCCL_P2P_DISABLE=1 NCCL_NET=Socket NCCL_PROTO=Simple NCCL_HOSTID=exp2-shm-allreduce-simple-rep2-rank0 /home/yunwei37/workspace/nccl-eBPF/nccl-tests/build/all_reduce_perf_mpi -b 8 -e 134217728 -f 2 -g 1 -n 50 -w 10 : -np 1 /usr/bin/env LD_LIBRARY_PATH=/home/yunwei37/workspace/nccl-eBPF/nccl/build/lib NCCL_TESTS_DEVICE=0 NCCL_P2P_DISABLE=1 NCCL_NET=Socket NCCL_PROTO=Simple NCCL_HOSTID=exp2-shm-allreduce-simple-rep2-rank1 /home/yunwei37/workspace/nccl-eBPF/nccl-tests/build/all_reduce_perf_mpi -b 8 -e 134217728 -f 2 -g 1 -n 50 -w 10
```

stdout:
```text
# nccl-tests version 2.18.0 nccl-headers=22907 nccl-library=22907
# Collective test starting: all_reduce_perf_mpi
# nThread 1 nGpus 1 minBytes 8 maxBytes 134217728 step: 2(factor) warmup iters: 10 iters: 50 agg iters: 1 validation: 1 graph: 0
#
# Using devices
#  Rank  0 Group  0 Pid 3041843 on        lab device  0 [0000:02:00] NVIDIA GeForce RTX 5090
#  Rank  1 Group  0 Pid 3041844 on        lab device  0 [0000:02:00] NVIDIA GeForce RTX 5090
#
#                                                              out-of-place                       in-place          
#       size         count      type   redop    root     time   algbw   busbw  #wrong     time   algbw   busbw  #wrong 
#        (B)    (elements)                               (us)  (GB/s)  (GB/s)             (us)  (GB/s)  (GB/s)         
           8             2     float     sum      -1  4354.81    0.00    0.00       0  4354.71    0.00    0.00       0
          16             4     float     sum      -1  4355.75    0.00    0.00       0  4354.86    0.00    0.00       0
          32             8     float     sum      -1  4355.95    0.00    0.00       0  4356.29    0.00    0.00       0
          64            16     float     sum      -1  4355.65    0.00    0.00       0  4355.70    0.00    0.00       0
         128            32     float     sum      -1  4443.17    0.00    0.00       0  4356.20    0.00    0.00       0
         256            64     float     sum      -1  4356.12    0.00    0.00       0  4355.56    0.00    0.00       0
         512           128     float     sum      -1  4316.93    0.00    0.00       0  4316.42    0.00    0.00       0
        1024           256     float     sum      -1  4316.37    0.00    0.00       0  4361.87    0.00    0.00       0
        2048           512     float     sum      -1  4316.02    0.00    0.00       0  4316.11    0.00    0.00       0
        4096          1024     float     sum      -1  4316.83    0.00    0.00       0  4361.33    0.00    0.00       0
        8192          2048     float     sum      -1  4316.34    0.00    0.00       0  4360.90    0.00    0.00       0
       16384          4096     float     sum      -1  4316.43    0.00    0.00       0  4316.32    0.00    0.00       0
       32768          8192     float     sum      -1  4360.17    0.01    0.01       0  4359.40    0.01    0.01       0
       65536         16384     float     sum      -1  4363.18    0.02    0.02       0  4361.67    0.02    0.02       0
      131072         32768     float     sum      -1  4362.47    0.03    0.03       0  4363.30    0.03    0.03       0
      262144         65536     float     sum      -1  4362.31    0.06    0.06       0  4361.61    0.06    0.06       0
      524288        131072     float     sum      -1  4364.51    0.12    0.12       0  4362.79    0.12    0.12       0
     1048576        262144     float     sum      -1  4365.73    0.24    0.24       0  4364.40    0.24    0.24       0
     2097152        524288     float     sum      -1  4369.83    0.48    0.48       0  4368.17    0.48    0.48       0
     4194304       1048576     float     sum      -1  4373.65    0.96    0.96       0  4375.15    0.96    0.96       0
     8388608       2097152     float     sum      -1  4485.98    1.87    1.87       0  4396.66    1.91    1.91       0
    16777216       4194304     float     sum      -1  7475.56    2.24    2.24       0  7565.58    2.22    2.22       0
    33554432       8388608     float     sum      -1  15035.2    2.23    2.23       0  14807.7    2.27    2.27       0
    67108864      16777216     float     sum      -1  29198.1    2.30    2.30       0  29094.4    2.31    2.31       0
   134217728      33554432     float     sum      -1  57885.7    2.32    2.32       0  57740.9    2.32    2.32       0
# Out of bounds values : 0 OK
# Avg bus bandwidth    : 0.516491 
#
# Collective test concluded: all_reduce_perf_mpi
#
```

stderr:
```text
Authorization required, but no authorization protocol specified

Authorization required, but no authorization protocol specified

Authorization required, but no authorization protocol specified

Authorization required, but no authorization protocol specified
```

#### exp2-shm-allreduce-tree-simple-rep2

Command:
```bash
mpirun --oversubscribe -np 1 /usr/bin/env LD_LIBRARY_PATH=/home/yunwei37/workspace/nccl-eBPF/nccl/build/lib NCCL_TESTS_DEVICE=0 NCCL_P2P_DISABLE=1 NCCL_NET=Socket NCCL_ALGO=Tree NCCL_PROTO=Simple NCCL_HOSTID=exp2-shm-allreduce-tree-simple-rep2-rank0 /home/yunwei37/workspace/nccl-eBPF/nccl-tests/build/all_reduce_perf_mpi -b 8 -e 134217728 -f 2 -g 1 -n 50 -w 10 : -np 1 /usr/bin/env LD_LIBRARY_PATH=/home/yunwei37/workspace/nccl-eBPF/nccl/build/lib NCCL_TESTS_DEVICE=0 NCCL_P2P_DISABLE=1 NCCL_NET=Socket NCCL_ALGO=Tree NCCL_PROTO=Simple NCCL_HOSTID=exp2-shm-allreduce-tree-simple-rep2-rank1 /home/yunwei37/workspace/nccl-eBPF/nccl-tests/build/all_reduce_perf_mpi -b 8 -e 134217728 -f 2 -g 1 -n 50 -w 10
```

stdout:
```text
# nccl-tests version 2.18.0 nccl-headers=22907 nccl-library=22907
# Collective test starting: all_reduce_perf_mpi
# nThread 1 nGpus 1 minBytes 8 maxBytes 134217728 step: 2(factor) warmup iters: 10 iters: 50 agg iters: 1 validation: 1 graph: 0
#
# Using devices
#  Rank  0 Group  0 Pid 3042120 on        lab device  0 [0000:02:00] NVIDIA GeForce RTX 5090
#  Rank  1 Group  0 Pid 3042121 on        lab device  0 [0000:02:00] NVIDIA GeForce RTX 5090
#
#                                                              out-of-place                       in-place          
#       size         count      type   redop    root     time   algbw   busbw  #wrong     time   algbw   busbw  #wrong 
#        (B)    (elements)                               (us)  (GB/s)  (GB/s)             (us)  (GB/s)  (GB/s)         
           8             2     float     sum      -1  4317.06    0.00    0.00       0  4360.92    0.00    0.00       0
          16             4     float     sum      -1  4316.32    0.00    0.00       0  4316.82    0.00    0.00       0
          32             8     float     sum      -1  4316.27    0.00    0.00       0  4316.21    0.00    0.00       0
          64            16     float     sum      -1  4316.28    0.00    0.00       0  4361.94    0.00    0.00       0
         128            32     float     sum      -1  4361.44    0.00    0.00       0  4360.64    0.00    0.00       0
         256            64     float     sum      -1  4361.40    0.00    0.00       0  4316.15    0.00    0.00       0
         512           128     float     sum      -1  4316.40    0.00    0.00       0  4316.20    0.00    0.00       0
        1024           256     float     sum      -1  4361.01    0.00    0.00       0  4316.12    0.00    0.00       0
        2048           512     float     sum      -1  4316.32    0.00    0.00       0  4316.09    0.00    0.00       0
        4096          1024     float     sum      -1  4316.42    0.00    0.00       0  4316.21    0.00    0.00       0
        8192          2048     float     sum      -1  4316.23    0.00    0.00       0  4316.39    0.00    0.00       0
       16384          4096     float     sum      -1  4361.13    0.00    0.00       0  4361.17    0.00    0.00       0
       32768          8192     float     sum      -1  4316.79    0.01    0.01       0  4317.57    0.01    0.01       0
       65536         16384     float     sum      -1  4317.52    0.02    0.02       0  4361.98    0.02    0.02       0
      131072         32768     float     sum      -1  4366.47    0.03    0.03       0  4365.63    0.03    0.03       0
      262144         65536     float     sum      -1  4366.27    0.06    0.06       0  4367.22    0.06    0.06       0
      524288        131072     float     sum      -1  4368.06    0.12    0.12       0  4368.03    0.12    0.12       0
     1048576        262144     float     sum      -1  4369.73    0.24    0.24       0  4368.67    0.24    0.24       0
     2097152        524288     float     sum      -1  4372.88    0.48    0.48       0  4372.66    0.48    0.48       0
     4194304       1048576     float     sum      -1  4378.19    0.96    0.96       0  4376.76    0.96    0.96       0
     8388608       2097152     float     sum      -1  4437.65    1.89    1.89       0  5210.02    1.61    1.61       0
    16777216       4194304     float     sum      -1  8306.50    2.02    2.02       0  8351.74    2.01    2.01       0
    33554432       8388608     float     sum      -1  15185.5    2.21    2.21       0  15200.3    2.21    2.21       0
    67108864      16777216     float     sum      -1  30321.0    2.21    2.21       0  29926.6    2.24    2.24       0
   134217728      33554432     float     sum      -1  60989.5    2.20    2.20       0  61002.2    2.20    2.20       0
# Out of bounds values : 0 OK
# Avg bus bandwidth    : 0.492777 
#
# Collective test concluded: all_reduce_perf_mpi
#
```

stderr:
```text
Authorization required, but no authorization protocol specified

Authorization required, but no authorization protocol specified

Authorization required, but no authorization protocol specified

Authorization required, but no authorization protocol specified
```

#### exp2-shm-allreduce-default-rep3

Command:
```bash
mpirun --oversubscribe -np 1 /usr/bin/env LD_LIBRARY_PATH=/home/yunwei37/workspace/nccl-eBPF/nccl/build/lib NCCL_TESTS_DEVICE=0 NCCL_P2P_DISABLE=1 NCCL_NET=Socket NCCL_HOSTID=exp2-shm-allreduce-default-rep3-rank0 /home/yunwei37/workspace/nccl-eBPF/nccl-tests/build/all_reduce_perf_mpi -b 8 -e 134217728 -f 2 -g 1 -n 50 -w 10 : -np 1 /usr/bin/env LD_LIBRARY_PATH=/home/yunwei37/workspace/nccl-eBPF/nccl/build/lib NCCL_TESTS_DEVICE=0 NCCL_P2P_DISABLE=1 NCCL_NET=Socket NCCL_HOSTID=exp2-shm-allreduce-default-rep3-rank1 /home/yunwei37/workspace/nccl-eBPF/nccl-tests/build/all_reduce_perf_mpi -b 8 -e 134217728 -f 2 -g 1 -n 50 -w 10
```

stdout:
```text
# nccl-tests version 2.18.0 nccl-headers=22907 nccl-library=22907
# Collective test starting: all_reduce_perf_mpi
# nThread 1 nGpus 1 minBytes 8 maxBytes 134217728 step: 2(factor) warmup iters: 10 iters: 50 agg iters: 1 validation: 1 graph: 0
#
# Using devices
#  Rank  0 Group  0 Pid 3042290 on        lab device  0 [0000:02:00] NVIDIA GeForce RTX 5090
#  Rank  1 Group  0 Pid 3042291 on        lab device  0 [0000:02:00] NVIDIA GeForce RTX 5090
#
#                                                              out-of-place                       in-place          
#       size         count      type   redop    root     time   algbw   busbw  #wrong     time   algbw   busbw  #wrong 
#        (B)    (elements)                               (us)  (GB/s)  (GB/s)             (us)  (GB/s)  (GB/s)         
           8             2     float     sum      -1  4355.15    0.00    0.00       0  4355.08    0.00    0.00       0
          16             4     float     sum      -1  4355.04    0.00    0.00       0  4355.33    0.00    0.00       0
          32             8     float     sum      -1  4353.90    0.00    0.00       0  4356.95    0.00    0.00       0
          64            16     float     sum      -1  4353.97    0.00    0.00       0  4354.14    0.00    0.00       0
         128            32     float     sum      -1  4354.27    0.00    0.00       0  4355.39    0.00    0.00       0
         256            64     float     sum      -1  4353.73    0.00    0.00       0  4354.07    0.00    0.00       0
         512           128     float     sum      -1  4356.02    0.00    0.00       0  4354.50    0.00    0.00       0
        1024           256     float     sum      -1  4356.11    0.00    0.00       0  4354.84    0.00    0.00       0
        2048           512     float     sum      -1  4356.82    0.00    0.00       0  4355.43    0.00    0.00       0
        4096          1024     float     sum      -1  4359.28    0.00    0.00       0  4358.79    0.00    0.00       0
        8192          2048     float     sum      -1  4363.16    0.00    0.00       0  4363.01    0.00    0.00       0
       16384          4096     float     sum      -1  4363.36    0.00    0.00       0  4362.50    0.00    0.00       0
       32768          8192     float     sum      -1  4371.72    0.01    0.01       0  4369.99    0.01    0.01       0
       65536         16384     float     sum      -1  4369.90    0.01    0.01       0  4370.20    0.01    0.01       0
      131072         32768     float     sum      -1  4363.41    0.03    0.03       0  4363.45    0.03    0.03       0
      262144         65536     float     sum      -1  4363.99    0.06    0.06       0  4363.69    0.06    0.06       0
      524288        131072     float     sum      -1  4365.92    0.12    0.12       0  4365.40    0.12    0.12       0
     1048576        262144     float     sum      -1  4367.51    0.24    0.24       0  4365.81    0.24    0.24       0
     2097152        524288     float     sum      -1  4372.39    0.48    0.48       0  4369.91    0.48    0.48       0
     4194304       1048576     float     sum      -1  4374.87    0.96    0.96       0  4373.57    0.96    0.96       0
     8388608       2097152     float     sum      -1  4441.87    1.89    1.89       0  4393.46    1.91    1.91       0
    16777216       4194304     float     sum      -1  7589.87    2.21    2.21       0  7723.17    2.17    2.17       0
    33554432       8388608     float     sum      -1  15029.0    2.23    2.23       0  15261.6    2.20    2.20       0
    67108864      16777216     float     sum      -1  29063.4    2.31    2.31       0  29221.5    2.30    2.30       0
   134217728      33554432     float     sum      -1  57722.4    2.33    2.33       0  57355.3    2.34    2.34       0
# Out of bounds values : 0 OK
# Avg bus bandwidth    : 0.514417 
#
# Collective test concluded: all_reduce_perf_mpi
#
```

stderr:
```text
Authorization required, but no authorization protocol specified

Authorization required, but no authorization protocol specified

Authorization required, but no authorization protocol specified

Authorization required, but no authorization protocol specified
```

#### exp2-shm-allreduce-simple-rep3

Command:
```bash
mpirun --oversubscribe -np 1 /usr/bin/env LD_LIBRARY_PATH=/home/yunwei37/workspace/nccl-eBPF/nccl/build/lib NCCL_TESTS_DEVICE=0 NCCL_P2P_DISABLE=1 NCCL_NET=Socket NCCL_PROTO=Simple NCCL_HOSTID=exp2-shm-allreduce-simple-rep3-rank0 /home/yunwei37/workspace/nccl-eBPF/nccl-tests/build/all_reduce_perf_mpi -b 8 -e 134217728 -f 2 -g 1 -n 50 -w 10 : -np 1 /usr/bin/env LD_LIBRARY_PATH=/home/yunwei37/workspace/nccl-eBPF/nccl/build/lib NCCL_TESTS_DEVICE=0 NCCL_P2P_DISABLE=1 NCCL_NET=Socket NCCL_PROTO=Simple NCCL_HOSTID=exp2-shm-allreduce-simple-rep3-rank1 /home/yunwei37/workspace/nccl-eBPF/nccl-tests/build/all_reduce_perf_mpi -b 8 -e 134217728 -f 2 -g 1 -n 50 -w 10
```

stdout:
```text
# nccl-tests version 2.18.0 nccl-headers=22907 nccl-library=22907
# Collective test starting: all_reduce_perf_mpi
# nThread 1 nGpus 1 minBytes 8 maxBytes 134217728 step: 2(factor) warmup iters: 10 iters: 50 agg iters: 1 validation: 1 graph: 0
#
# Using devices
#  Rank  0 Group  0 Pid 3042512 on        lab device  0 [0000:02:00] NVIDIA GeForce RTX 5090
#  Rank  1 Group  0 Pid 3042513 on        lab device  0 [0000:02:00] NVIDIA GeForce RTX 5090
#
#                                                              out-of-place                       in-place          
#       size         count      type   redop    root     time   algbw   busbw  #wrong     time   algbw   busbw  #wrong 
#        (B)    (elements)                               (us)  (GB/s)  (GB/s)             (us)  (GB/s)  (GB/s)         
           8             2     float     sum      -1  4355.90    0.00    0.00       0  4354.18    0.00    0.00       0
          16             4     float     sum      -1  4355.56    0.00    0.00       0  4355.74    0.00    0.00       0
          32             8     float     sum      -1  4355.92    0.00    0.00       0  4356.16    0.00    0.00       0
          64            16     float     sum      -1  4356.21    0.00    0.00       0  4355.01    0.00    0.00       0
         128            32     float     sum      -1  4356.04    0.00    0.00       0  4354.70    0.00    0.00       0
         256            64     float     sum      -1  4354.31    0.00    0.00       0  4354.45    0.00    0.00       0
         512           128     float     sum      -1  4316.43    0.00    0.00       0  4316.58    0.00    0.00       0
        1024           256     float     sum      -1  4361.74    0.00    0.00       0  4361.88    0.00    0.00       0
        2048           512     float     sum      -1  4316.43    0.00    0.00       0  4361.61    0.00    0.00       0
        4096          1024     float     sum      -1  4316.47    0.00    0.00       0  4361.55    0.00    0.00       0
        8192          2048     float     sum      -1  4316.44    0.00    0.00       0  4316.77    0.00    0.00       0
       16384          4096     float     sum      -1  4363.19    0.00    0.00       0  4361.07    0.00    0.00       0
       32768          8192     float     sum      -1  4360.21    0.01    0.01       0  4360.49    0.01    0.01       0
       65536         16384     float     sum      -1  4363.27    0.02    0.02       0  4363.45    0.02    0.02       0
      131072         32768     float     sum      -1  4363.63    0.03    0.03       0  4362.03    0.03    0.03       0
      262144         65536     float     sum      -1  4361.76    0.06    0.06       0  4361.90    0.06    0.06       0
      524288        131072     float     sum      -1  4364.18    0.12    0.12       0  4364.19    0.12    0.12       0
     1048576        262144     float     sum      -1  4365.93    0.24    0.24       0  4364.45    0.24    0.24       0
     2097152        524288     float     sum      -1  4369.74    0.48    0.48       0  4369.27    0.48    0.48       0
     4194304       1048576     float     sum      -1  4419.53    0.95    0.95       0  4372.04    0.96    0.96       0
     8388608       2097152     float     sum      -1  4387.03    1.91    1.91       0  4437.37    1.89    1.89       0
    16777216       4194304     float     sum      -1  7776.83    2.16    2.16       0  7852.48    2.14    2.14       0
    33554432       8388608     float     sum      -1  15563.6    2.16    2.16       0  14957.2    2.24    2.24       0
    67108864      16777216     float     sum      -1  29374.9    2.28    2.28       0  29376.2    2.28    2.28       0
   134217728      33554432     float     sum      -1  58470.0    2.30    2.30       0  58143.7    2.31    2.31       0
# Out of bounds values : 0 OK
# Avg bus bandwidth    : 0.509962 
#
# Collective test concluded: all_reduce_perf_mpi
#
```

stderr:
```text
Authorization required, but no authorization protocol specified

Authorization required, but no authorization protocol specified

Authorization required, but no authorization protocol specified

Authorization required, but no authorization protocol specified
```

#### exp2-shm-allreduce-tree-simple-rep3

Command:
```bash
mpirun --oversubscribe -np 1 /usr/bin/env LD_LIBRARY_PATH=/home/yunwei37/workspace/nccl-eBPF/nccl/build/lib NCCL_TESTS_DEVICE=0 NCCL_P2P_DISABLE=1 NCCL_NET=Socket NCCL_ALGO=Tree NCCL_PROTO=Simple NCCL_HOSTID=exp2-shm-allreduce-tree-simple-rep3-rank0 /home/yunwei37/workspace/nccl-eBPF/nccl-tests/build/all_reduce_perf_mpi -b 8 -e 134217728 -f 2 -g 1 -n 50 -w 10 : -np 1 /usr/bin/env LD_LIBRARY_PATH=/home/yunwei37/workspace/nccl-eBPF/nccl/build/lib NCCL_TESTS_DEVICE=0 NCCL_P2P_DISABLE=1 NCCL_NET=Socket NCCL_ALGO=Tree NCCL_PROTO=Simple NCCL_HOSTID=exp2-shm-allreduce-tree-simple-rep3-rank1 /home/yunwei37/workspace/nccl-eBPF/nccl-tests/build/all_reduce_perf_mpi -b 8 -e 134217728 -f 2 -g 1 -n 50 -w 10
```

stdout:
```text
# nccl-tests version 2.18.0 nccl-headers=22907 nccl-library=22907
# Collective test starting: all_reduce_perf_mpi
# nThread 1 nGpus 1 minBytes 8 maxBytes 134217728 step: 2(factor) warmup iters: 10 iters: 50 agg iters: 1 validation: 1 graph: 0
#
# Using devices
#  Rank  0 Group  0 Pid 3042750 on        lab device  0 [0000:02:00] NVIDIA GeForce RTX 5090
#  Rank  1 Group  0 Pid 3042751 on        lab device  0 [0000:02:00] NVIDIA GeForce RTX 5090
#
#                                                              out-of-place                       in-place          
#       size         count      type   redop    root     time   algbw   busbw  #wrong     time   algbw   busbw  #wrong 
#        (B)    (elements)                               (us)  (GB/s)  (GB/s)             (us)  (GB/s)  (GB/s)         
           8             2     float     sum      -1  4360.89    0.00    0.00       0  4361.09    0.00    0.00       0
          16             4     float     sum      -1  4316.31    0.00    0.00       0  4361.13    0.00    0.00       0
          32             8     float     sum      -1  4318.20    0.00    0.00       0  4361.35    0.00    0.00       0
          64            16     float     sum      -1  4361.19    0.00    0.00       0  4316.93    0.00    0.00       0
         128            32     float     sum      -1  4361.08    0.00    0.00       0  4360.79    0.00    0.00       0
         256            64     float     sum      -1  4316.69    0.00    0.00       0  4361.11    0.00    0.00       0
         512           128     float     sum      -1  4316.44    0.00    0.00       0  4360.69    0.00    0.00       0
        1024           256     float     sum      -1  4361.62    0.00    0.00       0  4360.90    0.00    0.00       0
        2048           512     float     sum      -1  4360.83    0.00    0.00       0  4360.84    0.00    0.00       0
        4096          1024     float     sum      -1  4361.20    0.00    0.00       0  4361.39    0.00    0.00       0
        8192          2048     float     sum      -1  4361.56    0.00    0.00       0  4361.09    0.00    0.00       0
       16384          4096     float     sum      -1  4361.01    0.00    0.00       0  4361.05    0.00    0.00       0
       32768          8192     float     sum      -1  4361.12    0.01    0.01       0  4360.59    0.01    0.01       0
       65536         16384     float     sum      -1  4363.04    0.02    0.02       0  4362.54    0.02    0.02       0
      131072         32768     float     sum      -1  4364.86    0.03    0.03       0  4366.26    0.03    0.03       0
      262144         65536     float     sum      -1  4366.97    0.06    0.06       0  4366.45    0.06    0.06       0
      524288        131072     float     sum      -1  4366.25    0.12    0.12       0  4366.93    0.12    0.12       0
     1048576        262144     float     sum      -1  4367.74    0.24    0.24       0  4369.97    0.24    0.24       0
     2097152        524288     float     sum      -1  4371.65    0.48    0.48       0  4375.10    0.48    0.48       0
     4194304       1048576     float     sum      -1  4377.90    0.96    0.96       0  4381.16    0.96    0.96       0
     8388608       2097152     float     sum      -1  4434.40    1.89    1.89       0  4432.21    1.89    1.89       0
    16777216       4194304     float     sum      -1  8405.74    2.00    2.00       0  8343.60    2.01    2.01       0
    33554432       8388608     float     sum      -1  15291.1    2.19    2.19       0  15264.1    2.20    2.20       0
    67108864      16777216     float     sum      -1  30318.1    2.21    2.21       0  30503.9    2.20    2.20       0
   134217728      33554432     float     sum      -1  63462.0    2.11    2.11       0  60908.7    2.20    2.20       0
# Out of bounds values : 0 OK
# Avg bus bandwidth    : 0.495011 
#
# Collective test concluded: all_reduce_perf_mpi
#
```

stderr:
```text
Authorization required, but no authorization protocol specified

Authorization required, but no authorization protocol specified

Authorization required, but no authorization protocol specified

Authorization required, but no authorization protocol specified
```


## Experiment 3: Fine-grained LL→Simple Transition Sweep

Primary comparison metric for verdicts: out-of-place time (us). Lower is better. Reported busbw remains as a secondary view.

- The literal user command with `-f 1.25` did not produce a fine-grained sweep in `nccl-tests 2.18.0`; it was parsed as a byte step and only executed 32KB.
- The summary tables and verdict below therefore use a corrected explicit-size sweep over the intended 1.25 progression: 32KB, 40KB, 50KB, 62.50KB, 78.12KB, 97.66KB, 122.07KB, 152.59KB, 190.73KB, 238.42KB, 298.02KB, 372.53KB, 465.66KB.

### Summary Table: out-of-place time

| Size | DEFAULT time (us) | Simple time (us) | Simple vs DEFAULT | Simple >2σ? | DEFAULT genuinely slower? |
| --- | --- | --- | --- | --- | --- |
| 32KB | 5917.55±2680.01 | 5829.51±2547.55 | -1.49% | No | No |
| 40KB | 5850.32±2564.21 | 4640.86±488.89 | -20.67% | No | No |
| 50KB | 4738.20±637.69 | 4358.95±0.11 | -8.00% | No | No |
| 62.50KB | 7594.53±2834.71 | 4366.08±12.69 | -42.51% | No | No |
| 78.12KB | 7508.85±2722.62 | 5906.84±2679.66 | -21.33% | No | No |
| 97.66KB | 7319.15±2560.43 | 6079.61±2974.02 | -16.94% | No | No |
| 122.07KB | 4831.28±412.03 | 5842.12±2564.19 | +20.92% | No | No |
| 152.59KB | 5903.51±2669.36 | 4435.32±124.71 | -24.87% | No | No |
| 190.73KB | 5833.05±2547.05 | 6121.96±3047.41 | +4.95% | No | No |
| 238.42KB | 5863.45±2600.76 | 6036.74±2898.60 | +2.96% | No | No |
| 298.02KB | 4768.19±702.16 | 5840.97±2560.76 | +22.50% | No | No |
| 372.53KB | 6042.29±2908.77 | 4479.06±202.40 | -25.87% | No | No |
| 465.66KB | 6112.64±3030.57 | 5907.58±2676.14 | -3.35% | No | No |

### Secondary Table: out-of-place busbw

| Size | DEFAULT busbw (GB/s) | Simple busbw (GB/s) | Simple vs DEFAULT |
| --- | --- | --- | --- |
| 32KB | 0.007±0.006 | 0.007±0.006 | +0.00% |
| 40KB | 0.007±0.006 | 0.010±0.000 | +50.00% |
| 50KB | 0.010±0.000 | 0.010±0.000 | +0.00% |
| 62.50KB | 0.010±0.000 | 0.010±0.000 | +0.00% |
| 78.12KB | 0.013±0.006 | 0.017±0.006 | +25.00% |
| 97.66KB | 0.013±0.006 | 0.017±0.006 | +25.00% |
| 122.07KB | 0.027±0.006 | 0.023±0.012 | -12.50% |
| 152.59KB | 0.033±0.012 | 0.037±0.006 | +10.00% |
| 190.73KB | 0.033±0.012 | 0.033±0.012 | +0.00% |
| 238.42KB | 0.050±0.017 | 0.050±0.017 | +0.00% |
| 298.02KB | 0.063±0.012 | 0.057±0.023 | -10.53% |
| 372.53KB | 0.073±0.029 | 0.087±0.006 | +18.18% |
| 465.66KB | 0.090±0.035 | 0.090±0.035 | +0.00% |

### Verdict

- No size in this experiment met the bar for DEFAULT being genuinely slower than an alternative.

### Raw Output

### Raw Output: Literal `-f 1.25` command

#### exp3-fine-transition-literal-default-rep1

Command:
```bash
mpirun --oversubscribe -np 1 /usr/bin/env LD_LIBRARY_PATH=/home/yunwei37/workspace/nccl-eBPF/nccl/build/lib NCCL_TESTS_DEVICE=0 NCCL_P2P_DISABLE=1 NCCL_NET=Socket NCCL_SHM_DISABLE=1 NCCL_HOSTID=exp3-fine-transition-literal-default-rep1-rank0 /home/yunwei37/workspace/nccl-eBPF/nccl-tests/build/all_reduce_perf_mpi -b 32768 -e 524288 -f 1.25 -g 1 -n 100 -w 20 : -np 1 /usr/bin/env LD_LIBRARY_PATH=/home/yunwei37/workspace/nccl-eBPF/nccl/build/lib NCCL_TESTS_DEVICE=0 NCCL_P2P_DISABLE=1 NCCL_NET=Socket NCCL_SHM_DISABLE=1 NCCL_HOSTID=exp3-fine-transition-literal-default-rep1-rank1 /home/yunwei37/workspace/nccl-eBPF/nccl-tests/build/all_reduce_perf_mpi -b 32768 -e 524288 -f 1.25 -g 1 -n 100 -w 20
```

stdout:
```text
# nccl-tests version 2.18.0 nccl-headers=22907 nccl-library=22907
# Collective test starting: all_reduce_perf_mpi
# nThread 1 nGpus 1 minBytes 32768 maxBytes 524288 step: 1048576(bytes) warmup iters: 20 iters: 100 agg iters: 1 validation: 1 graph: 0
#
# Using devices
#  Rank  0 Group  0 Pid 3046883 on        lab device  0 [0000:02:00] NVIDIA GeForce RTX 5090
#  Rank  1 Group  0 Pid 3046884 on        lab device  0 [0000:02:00] NVIDIA GeForce RTX 5090
#
#                                                              out-of-place                       in-place          
#       size         count      type   redop    root     time   algbw   busbw  #wrong     time   algbw   busbw  #wrong 
#        (B)    (elements)                               (us)  (GB/s)  (GB/s)             (us)  (GB/s)  (GB/s)         
       32768          8192     float     sum      -1  4370.69    0.01    0.01       0  4369.20    0.01    0.01       0
# Out of bounds values : 0 OK
# Avg bus bandwidth    : 0.00749849 
#
# Collective test concluded: all_reduce_perf_mpi
#
```

stderr:
```text
Authorization required, but no authorization protocol specified

Authorization required, but no authorization protocol specified

Authorization required, but no authorization protocol specified

Authorization required, but no authorization protocol specified
```

#### exp3-fine-transition-literal-simple-rep1

Command:
```bash
mpirun --oversubscribe -np 1 /usr/bin/env LD_LIBRARY_PATH=/home/yunwei37/workspace/nccl-eBPF/nccl/build/lib NCCL_TESTS_DEVICE=0 NCCL_P2P_DISABLE=1 NCCL_NET=Socket NCCL_PROTO=Simple NCCL_SHM_DISABLE=1 NCCL_HOSTID=exp3-fine-transition-literal-simple-rep1-rank0 /home/yunwei37/workspace/nccl-eBPF/nccl-tests/build/all_reduce_perf_mpi -b 32768 -e 524288 -f 1.25 -g 1 -n 100 -w 20 : -np 1 /usr/bin/env LD_LIBRARY_PATH=/home/yunwei37/workspace/nccl-eBPF/nccl/build/lib NCCL_TESTS_DEVICE=0 NCCL_P2P_DISABLE=1 NCCL_NET=Socket NCCL_PROTO=Simple NCCL_SHM_DISABLE=1 NCCL_HOSTID=exp3-fine-transition-literal-simple-rep1-rank1 /home/yunwei37/workspace/nccl-eBPF/nccl-tests/build/all_reduce_perf_mpi -b 32768 -e 524288 -f 1.25 -g 1 -n 100 -w 20
```

stdout:
```text
# nccl-tests version 2.18.0 nccl-headers=22907 nccl-library=22907
# Collective test starting: all_reduce_perf_mpi
# nThread 1 nGpus 1 minBytes 32768 maxBytes 524288 step: 1048576(bytes) warmup iters: 20 iters: 100 agg iters: 1 validation: 1 graph: 0
#
# Using devices
#  Rank  0 Group  0 Pid 3047044 on        lab device  0 [0000:02:00] NVIDIA GeForce RTX 5090
#  Rank  1 Group  0 Pid 3047045 on        lab device  0 [0000:02:00] NVIDIA GeForce RTX 5090
#
#                                                              out-of-place                       in-place          
#       size         count      type   redop    root     time   algbw   busbw  #wrong     time   algbw   busbw  #wrong 
#        (B)    (elements)                               (us)  (GB/s)  (GB/s)             (us)  (GB/s)  (GB/s)         
       32768          8192     float     sum      -1  4359.00    0.01    0.01       0  4358.58    0.01    0.01       0
# Out of bounds values : 0 OK
# Avg bus bandwidth    : 0.00751768 
#
# Collective test concluded: all_reduce_perf_mpi
#
```

stderr:
```text
Authorization required, but no authorization protocol specified

Authorization required, but no authorization protocol specified

Authorization required, but no authorization protocol specified

Authorization required, but no authorization protocol specified
```

#### exp3-fine-transition-literal-default-rep2

Command:
```bash
mpirun --oversubscribe -np 1 /usr/bin/env LD_LIBRARY_PATH=/home/yunwei37/workspace/nccl-eBPF/nccl/build/lib NCCL_TESTS_DEVICE=0 NCCL_P2P_DISABLE=1 NCCL_NET=Socket NCCL_SHM_DISABLE=1 NCCL_HOSTID=exp3-fine-transition-literal-default-rep2-rank0 /home/yunwei37/workspace/nccl-eBPF/nccl-tests/build/all_reduce_perf_mpi -b 32768 -e 524288 -f 1.25 -g 1 -n 100 -w 20 : -np 1 /usr/bin/env LD_LIBRARY_PATH=/home/yunwei37/workspace/nccl-eBPF/nccl/build/lib NCCL_TESTS_DEVICE=0 NCCL_P2P_DISABLE=1 NCCL_NET=Socket NCCL_SHM_DISABLE=1 NCCL_HOSTID=exp3-fine-transition-literal-default-rep2-rank1 /home/yunwei37/workspace/nccl-eBPF/nccl-tests/build/all_reduce_perf_mpi -b 32768 -e 524288 -f 1.25 -g 1 -n 100 -w 20
```

stdout:
```text
# nccl-tests version 2.18.0 nccl-headers=22907 nccl-library=22907
# Collective test starting: all_reduce_perf_mpi
# nThread 1 nGpus 1 minBytes 32768 maxBytes 524288 step: 1048576(bytes) warmup iters: 20 iters: 100 agg iters: 1 validation: 1 graph: 0
#
# Using devices
#  Rank  0 Group  0 Pid 3047140 on        lab device  0 [0000:02:00] NVIDIA GeForce RTX 5090
#  Rank  1 Group  0 Pid 3047141 on        lab device  0 [0000:02:00] NVIDIA GeForce RTX 5090
#
#                                                              out-of-place                       in-place          
#       size         count      type   redop    root     time   algbw   busbw  #wrong     time   algbw   busbw  #wrong 
#        (B)    (elements)                               (us)  (GB/s)  (GB/s)             (us)  (GB/s)  (GB/s)         
       32768          8192     float     sum      -1  4369.99    0.01    0.01       0  4369.34    0.01    0.01       0
# Out of bounds values : 0 OK
# Avg bus bandwidth    : 0.00749898 
#
# Collective test concluded: all_reduce_perf_mpi
#
```

stderr:
```text
Authorization required, but no authorization protocol specified

Authorization required, but no authorization protocol specified

Authorization required, but no authorization protocol specified

Authorization required, but no authorization protocol specified
```

#### exp3-fine-transition-literal-simple-rep2

Command:
```bash
mpirun --oversubscribe -np 1 /usr/bin/env LD_LIBRARY_PATH=/home/yunwei37/workspace/nccl-eBPF/nccl/build/lib NCCL_TESTS_DEVICE=0 NCCL_P2P_DISABLE=1 NCCL_NET=Socket NCCL_PROTO=Simple NCCL_SHM_DISABLE=1 NCCL_HOSTID=exp3-fine-transition-literal-simple-rep2-rank0 /home/yunwei37/workspace/nccl-eBPF/nccl-tests/build/all_reduce_perf_mpi -b 32768 -e 524288 -f 1.25 -g 1 -n 100 -w 20 : -np 1 /usr/bin/env LD_LIBRARY_PATH=/home/yunwei37/workspace/nccl-eBPF/nccl/build/lib NCCL_TESTS_DEVICE=0 NCCL_P2P_DISABLE=1 NCCL_NET=Socket NCCL_PROTO=Simple NCCL_SHM_DISABLE=1 NCCL_HOSTID=exp3-fine-transition-literal-simple-rep2-rank1 /home/yunwei37/workspace/nccl-eBPF/nccl-tests/build/all_reduce_perf_mpi -b 32768 -e 524288 -f 1.25 -g 1 -n 100 -w 20
```

stdout:
```text
# nccl-tests version 2.18.0 nccl-headers=22907 nccl-library=22907
# Collective test starting: all_reduce_perf_mpi
# nThread 1 nGpus 1 minBytes 32768 maxBytes 524288 step: 1048576(bytes) warmup iters: 20 iters: 100 agg iters: 1 validation: 1 graph: 0
#
# Using devices
#  Rank  0 Group  0 Pid 3047247 on        lab device  0 [0000:02:00] NVIDIA GeForce RTX 5090
#  Rank  1 Group  0 Pid 3047248 on        lab device  0 [0000:02:00] NVIDIA GeForce RTX 5090
#
#                                                              out-of-place                       in-place          
#       size         count      type   redop    root     time   algbw   busbw  #wrong     time   algbw   busbw  #wrong 
#        (B)    (elements)                               (us)  (GB/s)  (GB/s)             (us)  (GB/s)  (GB/s)         
       32768          8192     float     sum      -1  4359.32    0.01    0.01       0  4358.43    0.01    0.01       0
# Out of bounds values : 0 OK
# Avg bus bandwidth    : 0.00751754 
#
# Collective test concluded: all_reduce_perf_mpi
#
```

stderr:
```text
Authorization required, but no authorization protocol specified

Authorization required, but no authorization protocol specified

Authorization required, but no authorization protocol specified

Authorization required, but no authorization protocol specified
```

#### exp3-fine-transition-literal-default-rep3

Command:
```bash
mpirun --oversubscribe -np 1 /usr/bin/env LD_LIBRARY_PATH=/home/yunwei37/workspace/nccl-eBPF/nccl/build/lib NCCL_TESTS_DEVICE=0 NCCL_P2P_DISABLE=1 NCCL_NET=Socket NCCL_SHM_DISABLE=1 NCCL_HOSTID=exp3-fine-transition-literal-default-rep3-rank0 /home/yunwei37/workspace/nccl-eBPF/nccl-tests/build/all_reduce_perf_mpi -b 32768 -e 524288 -f 1.25 -g 1 -n 100 -w 20 : -np 1 /usr/bin/env LD_LIBRARY_PATH=/home/yunwei37/workspace/nccl-eBPF/nccl/build/lib NCCL_TESTS_DEVICE=0 NCCL_P2P_DISABLE=1 NCCL_NET=Socket NCCL_SHM_DISABLE=1 NCCL_HOSTID=exp3-fine-transition-literal-default-rep3-rank1 /home/yunwei37/workspace/nccl-eBPF/nccl-tests/build/all_reduce_perf_mpi -b 32768 -e 524288 -f 1.25 -g 1 -n 100 -w 20
```

stdout:
```text
# nccl-tests version 2.18.0 nccl-headers=22907 nccl-library=22907
# Collective test starting: all_reduce_perf_mpi
# nThread 1 nGpus 1 minBytes 32768 maxBytes 524288 step: 1048576(bytes) warmup iters: 20 iters: 100 agg iters: 1 validation: 1 graph: 0
#
# Using devices
#  Rank  0 Group  0 Pid 3047320 on        lab device  0 [0000:02:00] NVIDIA GeForce RTX 5090
#  Rank  1 Group  0 Pid 3047321 on        lab device  0 [0000:02:00] NVIDIA GeForce RTX 5090
#
#                                                              out-of-place                       in-place          
#       size         count      type   redop    root     time   algbw   busbw  #wrong     time   algbw   busbw  #wrong 
#        (B)    (elements)                               (us)  (GB/s)  (GB/s)             (us)  (GB/s)  (GB/s)         
       32768          8192     float     sum      -1  4370.25    0.01    0.01       0  4370.04    0.01    0.01       0
# Out of bounds values : 0 OK
# Avg bus bandwidth    : 0.00749815 
#
# Collective test concluded: all_reduce_perf_mpi
#
```

stderr:
```text
Authorization required, but no authorization protocol specified

Authorization required, but no authorization protocol specified

Authorization required, but no authorization protocol specified

Authorization required, but no authorization protocol specified
```

#### exp3-fine-transition-literal-simple-rep3

Command:
```bash
mpirun --oversubscribe -np 1 /usr/bin/env LD_LIBRARY_PATH=/home/yunwei37/workspace/nccl-eBPF/nccl/build/lib NCCL_TESTS_DEVICE=0 NCCL_P2P_DISABLE=1 NCCL_NET=Socket NCCL_PROTO=Simple NCCL_SHM_DISABLE=1 NCCL_HOSTID=exp3-fine-transition-literal-simple-rep3-rank0 /home/yunwei37/workspace/nccl-eBPF/nccl-tests/build/all_reduce_perf_mpi -b 32768 -e 524288 -f 1.25 -g 1 -n 100 -w 20 : -np 1 /usr/bin/env LD_LIBRARY_PATH=/home/yunwei37/workspace/nccl-eBPF/nccl/build/lib NCCL_TESTS_DEVICE=0 NCCL_P2P_DISABLE=1 NCCL_NET=Socket NCCL_PROTO=Simple NCCL_SHM_DISABLE=1 NCCL_HOSTID=exp3-fine-transition-literal-simple-rep3-rank1 /home/yunwei37/workspace/nccl-eBPF/nccl-tests/build/all_reduce_perf_mpi -b 32768 -e 524288 -f 1.25 -g 1 -n 100 -w 20
```

stdout:
```text
# nccl-tests version 2.18.0 nccl-headers=22907 nccl-library=22907
# Collective test starting: all_reduce_perf_mpi
# nThread 1 nGpus 1 minBytes 32768 maxBytes 524288 step: 1048576(bytes) warmup iters: 20 iters: 100 agg iters: 1 validation: 1 graph: 0
#
# Using devices
#  Rank  0 Group  0 Pid 3047429 on        lab device  0 [0000:02:00] NVIDIA GeForce RTX 5090
#  Rank  1 Group  0 Pid 3047430 on        lab device  0 [0000:02:00] NVIDIA GeForce RTX 5090
#
#                                                              out-of-place                       in-place          
#       size         count      type   redop    root     time   algbw   busbw  #wrong     time   algbw   busbw  #wrong 
#        (B)    (elements)                               (us)  (GB/s)  (GB/s)             (us)  (GB/s)  (GB/s)         
       32768          8192     float     sum      -1  4358.35    0.01    0.01       0  4358.55    0.01    0.01       0
# Out of bounds values : 0 OK
# Avg bus bandwidth    : 0.00751827 
#
# Collective test concluded: all_reduce_perf_mpi
#
```

stderr:
```text
Authorization required, but no authorization protocol specified

Authorization required, but no authorization protocol specified

Authorization required, but no authorization protocol specified

Authorization required, but no authorization protocol specified
```

### Raw Output: Corrected explicit-size sweep

#### exp3-fine-transition-corrected-default-rep1-32768

Command:
```bash
mpirun --oversubscribe -np 1 /usr/bin/env LD_LIBRARY_PATH=/home/yunwei37/workspace/nccl-eBPF/nccl/build/lib NCCL_TESTS_DEVICE=0 NCCL_P2P_DISABLE=1 NCCL_NET=Socket NCCL_SHM_DISABLE=1 NCCL_HOSTID=exp3-fine-transition-corrected-default-rep1-32768-rank0 /home/yunwei37/workspace/nccl-eBPF/nccl-tests/build/all_reduce_perf_mpi -b 32768 -e 32768 -g 1 -n 100 -w 20 : -np 1 /usr/bin/env LD_LIBRARY_PATH=/home/yunwei37/workspace/nccl-eBPF/nccl/build/lib NCCL_TESTS_DEVICE=0 NCCL_P2P_DISABLE=1 NCCL_NET=Socket NCCL_SHM_DISABLE=1 NCCL_HOSTID=exp3-fine-transition-corrected-default-rep1-32768-rank1 /home/yunwei37/workspace/nccl-eBPF/nccl-tests/build/all_reduce_perf_mpi -b 32768 -e 32768 -g 1 -n 100 -w 20
```

stdout:
```text
# nccl-tests version 2.18.0 nccl-headers=22907 nccl-library=22907
# Collective test starting: all_reduce_perf_mpi
# nThread 1 nGpus 1 minBytes 32768 maxBytes 32768 step: 1048576(bytes) warmup iters: 20 iters: 100 agg iters: 1 validation: 1 graph: 0
#
# Using devices
#  Rank  0 Group  0 Pid 3047500 on        lab device  0 [0000:02:00] NVIDIA GeForce RTX 5090
#  Rank  1 Group  0 Pid 3047501 on        lab device  0 [0000:02:00] NVIDIA GeForce RTX 5090
#
#                                                              out-of-place                       in-place          
#       size         count      type   redop    root     time   algbw   busbw  #wrong     time   algbw   busbw  #wrong 
#        (B)    (elements)                               (us)  (GB/s)  (GB/s)             (us)  (GB/s)  (GB/s)         
       32768          8192     float     sum      -1  4370.30    0.01    0.01       0  4370.08    0.01    0.01       0
# Out of bounds values : 0 OK
# Avg bus bandwidth    : 0.00749807 
#
# Collective test concluded: all_reduce_perf_mpi
#
```

stderr:
```text
Authorization required, but no authorization protocol specified

Authorization required, but no authorization protocol specified

Authorization required, but no authorization protocol specified

Authorization required, but no authorization protocol specified
```

#### exp3-fine-transition-corrected-default-rep1-40960

Command:
```bash
mpirun --oversubscribe -np 1 /usr/bin/env LD_LIBRARY_PATH=/home/yunwei37/workspace/nccl-eBPF/nccl/build/lib NCCL_TESTS_DEVICE=0 NCCL_P2P_DISABLE=1 NCCL_NET=Socket NCCL_SHM_DISABLE=1 NCCL_HOSTID=exp3-fine-transition-corrected-default-rep1-40960-rank0 /home/yunwei37/workspace/nccl-eBPF/nccl-tests/build/all_reduce_perf_mpi -b 40960 -e 40960 -g 1 -n 100 -w 20 : -np 1 /usr/bin/env LD_LIBRARY_PATH=/home/yunwei37/workspace/nccl-eBPF/nccl/build/lib NCCL_TESTS_DEVICE=0 NCCL_P2P_DISABLE=1 NCCL_NET=Socket NCCL_SHM_DISABLE=1 NCCL_HOSTID=exp3-fine-transition-corrected-default-rep1-40960-rank1 /home/yunwei37/workspace/nccl-eBPF/nccl-tests/build/all_reduce_perf_mpi -b 40960 -e 40960 -g 1 -n 100 -w 20
```

stdout:
```text
# nccl-tests version 2.18.0 nccl-headers=22907 nccl-library=22907
# Collective test starting: all_reduce_perf_mpi
# nThread 1 nGpus 1 minBytes 40960 maxBytes 40960 step: 1048576(bytes) warmup iters: 20 iters: 100 agg iters: 1 validation: 1 graph: 0
#
# Using devices
#  Rank  0 Group  0 Pid 3047702 on        lab device  0 [0000:02:00] NVIDIA GeForce RTX 5090
#  Rank  1 Group  0 Pid 3047703 on        lab device  0 [0000:02:00] NVIDIA GeForce RTX 5090
#
#                                                              out-of-place                       in-place          
#       size         count      type   redop    root     time   algbw   busbw  #wrong     time   algbw   busbw  #wrong 
#        (B)    (elements)                               (us)  (GB/s)  (GB/s)             (us)  (GB/s)  (GB/s)         
       40960         10240     float     sum      -1  4370.02    0.01    0.01       0  4370.23    0.01    0.01       0
# Out of bounds values : 0 OK
# Avg bus bandwidth    : 0.00937273 
#
# Collective test concluded: all_reduce_perf_mpi
#
```

stderr:
```text
Authorization required, but no authorization protocol specified

Authorization required, but no authorization protocol specified

Authorization required, but no authorization protocol specified

Authorization required, but no authorization protocol specified
```

#### exp3-fine-transition-corrected-default-rep1-51200

Command:
```bash
mpirun --oversubscribe -np 1 /usr/bin/env LD_LIBRARY_PATH=/home/yunwei37/workspace/nccl-eBPF/nccl/build/lib NCCL_TESTS_DEVICE=0 NCCL_P2P_DISABLE=1 NCCL_NET=Socket NCCL_SHM_DISABLE=1 NCCL_HOSTID=exp3-fine-transition-corrected-default-rep1-51200-rank0 /home/yunwei37/workspace/nccl-eBPF/nccl-tests/build/all_reduce_perf_mpi -b 51200 -e 51200 -g 1 -n 100 -w 20 : -np 1 /usr/bin/env LD_LIBRARY_PATH=/home/yunwei37/workspace/nccl-eBPF/nccl/build/lib NCCL_TESTS_DEVICE=0 NCCL_P2P_DISABLE=1 NCCL_NET=Socket NCCL_SHM_DISABLE=1 NCCL_HOSTID=exp3-fine-transition-corrected-default-rep1-51200-rank1 /home/yunwei37/workspace/nccl-eBPF/nccl-tests/build/all_reduce_perf_mpi -b 51200 -e 51200 -g 1 -n 100 -w 20
```

stdout:
```text
# nccl-tests version 2.18.0 nccl-headers=22907 nccl-library=22907
# Collective test starting: all_reduce_perf_mpi
# nThread 1 nGpus 1 minBytes 51200 maxBytes 51200 step: 1048576(bytes) warmup iters: 20 iters: 100 agg iters: 1 validation: 1 graph: 0
#
# Using devices
#  Rank  0 Group  0 Pid 3047787 on        lab device  0 [0000:02:00] NVIDIA GeForce RTX 5090
#  Rank  1 Group  0 Pid 3047788 on        lab device  0 [0000:02:00] NVIDIA GeForce RTX 5090
#
#                                                              out-of-place                       in-place          
#       size         count      type   redop    root     time   algbw   busbw  #wrong     time   algbw   busbw  #wrong 
#        (B)    (elements)                               (us)  (GB/s)  (GB/s)             (us)  (GB/s)  (GB/s)         
       51200         12800     float     sum      -1  4369.85    0.01    0.01       0  4369.45    0.01    0.01       0
# Out of bounds values : 0 OK
# Avg bus bandwidth    : 0.0117172 
#
# Collective test concluded: all_reduce_perf_mpi
#
```

stderr:
```text
Authorization required, but no authorization protocol specified

Authorization required, but no authorization protocol specified

Authorization required, but no authorization protocol specified

Authorization required, but no authorization protocol specified
```

#### exp3-fine-transition-corrected-default-rep1-64000

Command:
```bash
mpirun --oversubscribe -np 1 /usr/bin/env LD_LIBRARY_PATH=/home/yunwei37/workspace/nccl-eBPF/nccl/build/lib NCCL_TESTS_DEVICE=0 NCCL_P2P_DISABLE=1 NCCL_NET=Socket NCCL_SHM_DISABLE=1 NCCL_HOSTID=exp3-fine-transition-corrected-default-rep1-64000-rank0 /home/yunwei37/workspace/nccl-eBPF/nccl-tests/build/all_reduce_perf_mpi -b 64000 -e 64000 -g 1 -n 100 -w 20 : -np 1 /usr/bin/env LD_LIBRARY_PATH=/home/yunwei37/workspace/nccl-eBPF/nccl/build/lib NCCL_TESTS_DEVICE=0 NCCL_P2P_DISABLE=1 NCCL_NET=Socket NCCL_SHM_DISABLE=1 NCCL_HOSTID=exp3-fine-transition-corrected-default-rep1-64000-rank1 /home/yunwei37/workspace/nccl-eBPF/nccl-tests/build/all_reduce_perf_mpi -b 64000 -e 64000 -g 1 -n 100 -w 20
```

stdout:
```text
# nccl-tests version 2.18.0 nccl-headers=22907 nccl-library=22907
# Collective test starting: all_reduce_perf_mpi
# nThread 1 nGpus 1 minBytes 64000 maxBytes 64000 step: 1048576(bytes) warmup iters: 20 iters: 100 agg iters: 1 validation: 1 graph: 0
#
# Using devices
#  Rank  0 Group  0 Pid 3047835 on        lab device  0 [0000:02:00] NVIDIA GeForce RTX 5090
#  Rank  1 Group  0 Pid 3047836 on        lab device  0 [0000:02:00] NVIDIA GeForce RTX 5090
#
#                                                              out-of-place                       in-place          
#       size         count      type   redop    root     time   algbw   busbw  #wrong     time   algbw   busbw  #wrong 
#        (B)    (elements)                               (us)  (GB/s)  (GB/s)             (us)  (GB/s)  (GB/s)         
       64000         16000     float     sum      -1  4370.44    0.01    0.01       0  4369.90    0.01    0.01       0
# Out of bounds values : 0 OK
# Avg bus bandwidth    : 0.0146447 
#
# Collective test concluded: all_reduce_perf_mpi
#
```

stderr:
```text
Authorization required, but no authorization protocol specified

Authorization required, but no authorization protocol specified

Authorization required, but no authorization protocol specified

Authorization required, but no authorization protocol specified
```

#### exp3-fine-transition-corrected-default-rep1-80000

Command:
```bash
mpirun --oversubscribe -np 1 /usr/bin/env LD_LIBRARY_PATH=/home/yunwei37/workspace/nccl-eBPF/nccl/build/lib NCCL_TESTS_DEVICE=0 NCCL_P2P_DISABLE=1 NCCL_NET=Socket NCCL_SHM_DISABLE=1 NCCL_HOSTID=exp3-fine-transition-corrected-default-rep1-80000-rank0 /home/yunwei37/workspace/nccl-eBPF/nccl-tests/build/all_reduce_perf_mpi -b 80000 -e 80000 -g 1 -n 100 -w 20 : -np 1 /usr/bin/env LD_LIBRARY_PATH=/home/yunwei37/workspace/nccl-eBPF/nccl/build/lib NCCL_TESTS_DEVICE=0 NCCL_P2P_DISABLE=1 NCCL_NET=Socket NCCL_SHM_DISABLE=1 NCCL_HOSTID=exp3-fine-transition-corrected-default-rep1-80000-rank1 /home/yunwei37/workspace/nccl-eBPF/nccl-tests/build/all_reduce_perf_mpi -b 80000 -e 80000 -g 1 -n 100 -w 20
```

stdout:
```text
# nccl-tests version 2.18.0 nccl-headers=22907 nccl-library=22907
# Collective test starting: all_reduce_perf_mpi
# nThread 1 nGpus 1 minBytes 80000 maxBytes 80000 step: 1048576(bytes) warmup iters: 20 iters: 100 agg iters: 1 validation: 1 graph: 0
#
# Using devices
#  Rank  0 Group  0 Pid 3047882 on        lab device  0 [0000:02:00] NVIDIA GeForce RTX 5090
#  Rank  1 Group  0 Pid 3047883 on        lab device  0 [0000:02:00] NVIDIA GeForce RTX 5090
#
#                                                              out-of-place                       in-place          
#       size         count      type   redop    root     time   algbw   busbw  #wrong     time   algbw   busbw  #wrong 
#        (B)    (elements)                               (us)  (GB/s)  (GB/s)             (us)  (GB/s)  (GB/s)         
       80000         20000     float     sum      -1  4370.24    0.02    0.02       0  4369.64    0.02    0.02       0
# Out of bounds values : 0 OK
# Avg bus bandwidth    : 0.0183069 
#
# Collective test concluded: all_reduce_perf_mpi
#
```

stderr:
```text
Authorization required, but no authorization protocol specified

Authorization required, but no authorization protocol specified

Authorization required, but no authorization protocol specified

Authorization required, but no authorization protocol specified
```

#### exp3-fine-transition-corrected-default-rep1-100000

Command:
```bash
mpirun --oversubscribe -np 1 /usr/bin/env LD_LIBRARY_PATH=/home/yunwei37/workspace/nccl-eBPF/nccl/build/lib NCCL_TESTS_DEVICE=0 NCCL_P2P_DISABLE=1 NCCL_NET=Socket NCCL_SHM_DISABLE=1 NCCL_HOSTID=exp3-fine-transition-corrected-default-rep1-100000-rank0 /home/yunwei37/workspace/nccl-eBPF/nccl-tests/build/all_reduce_perf_mpi -b 100000 -e 100000 -g 1 -n 100 -w 20 : -np 1 /usr/bin/env LD_LIBRARY_PATH=/home/yunwei37/workspace/nccl-eBPF/nccl/build/lib NCCL_TESTS_DEVICE=0 NCCL_P2P_DISABLE=1 NCCL_NET=Socket NCCL_SHM_DISABLE=1 NCCL_HOSTID=exp3-fine-transition-corrected-default-rep1-100000-rank1 /home/yunwei37/workspace/nccl-eBPF/nccl-tests/build/all_reduce_perf_mpi -b 100000 -e 100000 -g 1 -n 100 -w 20
```

stdout:
```text
# nccl-tests version 2.18.0 nccl-headers=22907 nccl-library=22907
# Collective test starting: all_reduce_perf_mpi
# nThread 1 nGpus 1 minBytes 100000 maxBytes 100000 step: 1048576(bytes) warmup iters: 20 iters: 100 agg iters: 1 validation: 1 graph: 0
#
# Using devices
#  Rank  0 Group  0 Pid 3047927 on        lab device  0 [0000:02:00] NVIDIA GeForce RTX 5090
#  Rank  1 Group  0 Pid 3047928 on        lab device  0 [0000:02:00] NVIDIA GeForce RTX 5090
#
#                                                              out-of-place                       in-place          
#       size         count      type   redop    root     time   algbw   busbw  #wrong     time   algbw   busbw  #wrong 
#        (B)    (elements)                               (us)  (GB/s)  (GB/s)             (us)  (GB/s)  (GB/s)         
      100000         25000     float     sum      -1  4362.73    0.02    0.02       0  4362.16    0.02    0.02       0
# Out of bounds values : 0 OK
# Avg bus bandwidth    : 0.0229229 
#
# Collective test concluded: all_reduce_perf_mpi
#
```

stderr:
```text
Authorization required, but no authorization protocol specified

Authorization required, but no authorization protocol specified

Authorization required, but no authorization protocol specified

Authorization required, but no authorization protocol specified
```

#### exp3-fine-transition-corrected-default-rep1-125000

Command:
```bash
mpirun --oversubscribe -np 1 /usr/bin/env LD_LIBRARY_PATH=/home/yunwei37/workspace/nccl-eBPF/nccl/build/lib NCCL_TESTS_DEVICE=0 NCCL_P2P_DISABLE=1 NCCL_NET=Socket NCCL_SHM_DISABLE=1 NCCL_HOSTID=exp3-fine-transition-corrected-default-rep1-125000-rank0 /home/yunwei37/workspace/nccl-eBPF/nccl-tests/build/all_reduce_perf_mpi -b 125000 -e 125000 -g 1 -n 100 -w 20 : -np 1 /usr/bin/env LD_LIBRARY_PATH=/home/yunwei37/workspace/nccl-eBPF/nccl/build/lib NCCL_TESTS_DEVICE=0 NCCL_P2P_DISABLE=1 NCCL_NET=Socket NCCL_SHM_DISABLE=1 NCCL_HOSTID=exp3-fine-transition-corrected-default-rep1-125000-rank1 /home/yunwei37/workspace/nccl-eBPF/nccl-tests/build/all_reduce_perf_mpi -b 125000 -e 125000 -g 1 -n 100 -w 20
```

stdout:
```text
# nccl-tests version 2.18.0 nccl-headers=22907 nccl-library=22907
# Collective test starting: all_reduce_perf_mpi
# nThread 1 nGpus 1 minBytes 125000 maxBytes 125000 step: 1048576(bytes) warmup iters: 20 iters: 100 agg iters: 1 validation: 1 graph: 0
#
# Using devices
#  Rank  0 Group  0 Pid 3048021 on        lab device  0 [0000:02:00] NVIDIA GeForce RTX 5090
#  Rank  1 Group  0 Pid 3048022 on        lab device  0 [0000:02:00] NVIDIA GeForce RTX 5090
#
#                                                              out-of-place                       in-place          
#       size         count      type   redop    root     time   algbw   busbw  #wrong     time   algbw   busbw  #wrong 
#        (B)    (elements)                               (us)  (GB/s)  (GB/s)             (us)  (GB/s)  (GB/s)         
      125000         31250     float     sum      -1  4362.75    0.03    0.03       0  4362.14    0.03    0.03       0
# Out of bounds values : 0 OK
# Avg bus bandwidth    : 0.0286537 
#
# Collective test concluded: all_reduce_perf_mpi
#
```

stderr:
```text
Authorization required, but no authorization protocol specified

Authorization required, but no authorization protocol specified

Authorization required, but no authorization protocol specified

Authorization required, but no authorization protocol specified
```

#### exp3-fine-transition-corrected-default-rep1-156250

Command:
```bash
mpirun --oversubscribe -np 1 /usr/bin/env LD_LIBRARY_PATH=/home/yunwei37/workspace/nccl-eBPF/nccl/build/lib NCCL_TESTS_DEVICE=0 NCCL_P2P_DISABLE=1 NCCL_NET=Socket NCCL_SHM_DISABLE=1 NCCL_HOSTID=exp3-fine-transition-corrected-default-rep1-156250-rank0 /home/yunwei37/workspace/nccl-eBPF/nccl-tests/build/all_reduce_perf_mpi -b 156250 -e 156250 -g 1 -n 100 -w 20 : -np 1 /usr/bin/env LD_LIBRARY_PATH=/home/yunwei37/workspace/nccl-eBPF/nccl/build/lib NCCL_TESTS_DEVICE=0 NCCL_P2P_DISABLE=1 NCCL_NET=Socket NCCL_SHM_DISABLE=1 NCCL_HOSTID=exp3-fine-transition-corrected-default-rep1-156250-rank1 /home/yunwei37/workspace/nccl-eBPF/nccl-tests/build/all_reduce_perf_mpi -b 156250 -e 156250 -g 1 -n 100 -w 20
```

stdout:
```text
# nccl-tests version 2.18.0 nccl-headers=22907 nccl-library=22907
# Collective test starting: all_reduce_perf_mpi
# nThread 1 nGpus 1 minBytes 156250 maxBytes 156250 step: 1048576(bytes) warmup iters: 20 iters: 100 agg iters: 1 validation: 1 graph: 0
#
# Using devices
#  Rank  0 Group  0 Pid 3048062 on        lab device  0 [0000:02:00] NVIDIA GeForce RTX 5090
#  Rank  1 Group  0 Pid 3048063 on        lab device  0 [0000:02:00] NVIDIA GeForce RTX 5090
#
#                                                              out-of-place                       in-place          
#       size         count      type   redop    root     time   algbw   busbw  #wrong     time   algbw   busbw  #wrong 
#        (B)    (elements)                               (us)  (GB/s)  (GB/s)             (us)  (GB/s)  (GB/s)         
      156248         39062     float     sum      -1  4362.62    0.04    0.04       0  4362.16    0.04    0.04       0
# Out of bounds values : 0 OK
# Avg bus bandwidth    : 0.0358171 
#
# Collective test concluded: all_reduce_perf_mpi
#
```

stderr:
```text
Authorization required, but no authorization protocol specified

Authorization required, but no authorization protocol specified

Authorization required, but no authorization protocol specified

Authorization required, but no authorization protocol specified
```

#### exp3-fine-transition-corrected-default-rep1-195312

Command:
```bash
mpirun --oversubscribe -np 1 /usr/bin/env LD_LIBRARY_PATH=/home/yunwei37/workspace/nccl-eBPF/nccl/build/lib NCCL_TESTS_DEVICE=0 NCCL_P2P_DISABLE=1 NCCL_NET=Socket NCCL_SHM_DISABLE=1 NCCL_HOSTID=exp3-fine-transition-corrected-default-rep1-195312-rank0 /home/yunwei37/workspace/nccl-eBPF/nccl-tests/build/all_reduce_perf_mpi -b 195312 -e 195312 -g 1 -n 100 -w 20 : -np 1 /usr/bin/env LD_LIBRARY_PATH=/home/yunwei37/workspace/nccl-eBPF/nccl/build/lib NCCL_TESTS_DEVICE=0 NCCL_P2P_DISABLE=1 NCCL_NET=Socket NCCL_SHM_DISABLE=1 NCCL_HOSTID=exp3-fine-transition-corrected-default-rep1-195312-rank1 /home/yunwei37/workspace/nccl-eBPF/nccl-tests/build/all_reduce_perf_mpi -b 195312 -e 195312 -g 1 -n 100 -w 20
```

stdout:
```text
# nccl-tests version 2.18.0 nccl-headers=22907 nccl-library=22907
# Collective test starting: all_reduce_perf_mpi
# nThread 1 nGpus 1 minBytes 195312 maxBytes 195312 step: 1048576(bytes) warmup iters: 20 iters: 100 agg iters: 1 validation: 1 graph: 0
#
# Using devices
#  Rank  0 Group  0 Pid 3048156 on        lab device  0 [0000:02:00] NVIDIA GeForce RTX 5090
#  Rank  1 Group  0 Pid 3048158 on        lab device  0 [0000:02:00] NVIDIA GeForce RTX 5090
#
#                                                              out-of-place                       in-place          
#       size         count      type   redop    root     time   algbw   busbw  #wrong     time   algbw   busbw  #wrong 
#        (B)    (elements)                               (us)  (GB/s)  (GB/s)             (us)  (GB/s)  (GB/s)         
      195312         48828     float     sum      -1  4362.50    0.04    0.04       0  4362.56    0.04    0.04       0
# Out of bounds values : 0 OK
# Avg bus bandwidth    : 0.0447704 
#
# Collective test concluded: all_reduce_perf_mpi
#
```

stderr:
```text
Authorization required, but no authorization protocol specified

Authorization required, but no authorization protocol specified

Authorization required, but no authorization protocol specified

Authorization required, but no authorization protocol specified
```

#### exp3-fine-transition-corrected-default-rep1-244140

Command:
```bash
mpirun --oversubscribe -np 1 /usr/bin/env LD_LIBRARY_PATH=/home/yunwei37/workspace/nccl-eBPF/nccl/build/lib NCCL_TESTS_DEVICE=0 NCCL_P2P_DISABLE=1 NCCL_NET=Socket NCCL_SHM_DISABLE=1 NCCL_HOSTID=exp3-fine-transition-corrected-default-rep1-244140-rank0 /home/yunwei37/workspace/nccl-eBPF/nccl-tests/build/all_reduce_perf_mpi -b 244140 -e 244140 -g 1 -n 100 -w 20 : -np 1 /usr/bin/env LD_LIBRARY_PATH=/home/yunwei37/workspace/nccl-eBPF/nccl/build/lib NCCL_TESTS_DEVICE=0 NCCL_P2P_DISABLE=1 NCCL_NET=Socket NCCL_SHM_DISABLE=1 NCCL_HOSTID=exp3-fine-transition-corrected-default-rep1-244140-rank1 /home/yunwei37/workspace/nccl-eBPF/nccl-tests/build/all_reduce_perf_mpi -b 244140 -e 244140 -g 1 -n 100 -w 20
```

stdout:
```text
# nccl-tests version 2.18.0 nccl-headers=22907 nccl-library=22907
# Collective test starting: all_reduce_perf_mpi
# nThread 1 nGpus 1 minBytes 244140 maxBytes 244140 step: 1048576(bytes) warmup iters: 20 iters: 100 agg iters: 1 validation: 1 graph: 0
#
# Using devices
#  Rank  0 Group  0 Pid 3048251 on        lab device  0 [0000:02:00] NVIDIA GeForce RTX 5090
#  Rank  1 Group  0 Pid 3048252 on        lab device  0 [0000:02:00] NVIDIA GeForce RTX 5090
#
#                                                              out-of-place                       in-place          
#       size         count      type   redop    root     time   algbw   busbw  #wrong     time   algbw   busbw  #wrong 
#        (B)    (elements)                               (us)  (GB/s)  (GB/s)             (us)  (GB/s)  (GB/s)         
      244140         61035     float     sum      -1  4361.57    0.06    0.06       0  4361.33    0.06    0.06       0
# Out of bounds values : 0 OK
# Avg bus bandwidth    : 0.0559769 
#
# Collective test concluded: all_reduce_perf_mpi
#
```

stderr:
```text
Authorization required, but no authorization protocol specified

Authorization required, but no authorization protocol specified

Authorization required, but no authorization protocol specified

Authorization required, but no authorization protocol specified
```

#### exp3-fine-transition-corrected-default-rep1-305175

Command:
```bash
mpirun --oversubscribe -np 1 /usr/bin/env LD_LIBRARY_PATH=/home/yunwei37/workspace/nccl-eBPF/nccl/build/lib NCCL_TESTS_DEVICE=0 NCCL_P2P_DISABLE=1 NCCL_NET=Socket NCCL_SHM_DISABLE=1 NCCL_HOSTID=exp3-fine-transition-corrected-default-rep1-305175-rank0 /home/yunwei37/workspace/nccl-eBPF/nccl-tests/build/all_reduce_perf_mpi -b 305175 -e 305175 -g 1 -n 100 -w 20 : -np 1 /usr/bin/env LD_LIBRARY_PATH=/home/yunwei37/workspace/nccl-eBPF/nccl/build/lib NCCL_TESTS_DEVICE=0 NCCL_P2P_DISABLE=1 NCCL_NET=Socket NCCL_SHM_DISABLE=1 NCCL_HOSTID=exp3-fine-transition-corrected-default-rep1-305175-rank1 /home/yunwei37/workspace/nccl-eBPF/nccl-tests/build/all_reduce_perf_mpi -b 305175 -e 305175 -g 1 -n 100 -w 20
```

stdout:
```text
# nccl-tests version 2.18.0 nccl-headers=22907 nccl-library=22907
# Collective test starting: all_reduce_perf_mpi
# nThread 1 nGpus 1 minBytes 305175 maxBytes 305175 step: 1048576(bytes) warmup iters: 20 iters: 100 agg iters: 1 validation: 1 graph: 0
#
# Using devices
#  Rank  0 Group  0 Pid 3048323 on        lab device  0 [0000:02:00] NVIDIA GeForce RTX 5090
#  Rank  1 Group  0 Pid 3048324 on        lab device  0 [0000:02:00] NVIDIA GeForce RTX 5090
#
#                                                              out-of-place                       in-place          
#       size         count      type   redop    root     time   algbw   busbw  #wrong     time   algbw   busbw  #wrong 
#        (B)    (elements)                               (us)  (GB/s)  (GB/s)             (us)  (GB/s)  (GB/s)         
      305172         76293     float     sum      -1  4363.24    0.07    0.07       0  4363.32    0.07    0.07       0
# Out of bounds values : 0 OK
# Avg bus bandwidth    : 0.0699409 
#
# Collective test concluded: all_reduce_perf_mpi
#
```

stderr:
```text
Authorization required, but no authorization protocol specified

Authorization required, but no authorization protocol specified

Authorization required, but no authorization protocol specified

Authorization required, but no authorization protocol specified
```

#### exp3-fine-transition-corrected-default-rep1-381469

Command:
```bash
mpirun --oversubscribe -np 1 /usr/bin/env LD_LIBRARY_PATH=/home/yunwei37/workspace/nccl-eBPF/nccl/build/lib NCCL_TESTS_DEVICE=0 NCCL_P2P_DISABLE=1 NCCL_NET=Socket NCCL_SHM_DISABLE=1 NCCL_HOSTID=exp3-fine-transition-corrected-default-rep1-381469-rank0 /home/yunwei37/workspace/nccl-eBPF/nccl-tests/build/all_reduce_perf_mpi -b 381469 -e 381469 -g 1 -n 100 -w 20 : -np 1 /usr/bin/env LD_LIBRARY_PATH=/home/yunwei37/workspace/nccl-eBPF/nccl/build/lib NCCL_TESTS_DEVICE=0 NCCL_P2P_DISABLE=1 NCCL_NET=Socket NCCL_SHM_DISABLE=1 NCCL_HOSTID=exp3-fine-transition-corrected-default-rep1-381469-rank1 /home/yunwei37/workspace/nccl-eBPF/nccl-tests/build/all_reduce_perf_mpi -b 381469 -e 381469 -g 1 -n 100 -w 20
```

stdout:
```text
# nccl-tests version 2.18.0 nccl-headers=22907 nccl-library=22907
# Collective test starting: all_reduce_perf_mpi
# nThread 1 nGpus 1 minBytes 381469 maxBytes 381469 step: 1048576(bytes) warmup iters: 20 iters: 100 agg iters: 1 validation: 1 graph: 0
#
# Using devices
#  Rank  0 Group  0 Pid 3048385 on        lab device  0 [0000:02:00] NVIDIA GeForce RTX 5090
#  Rank  1 Group  0 Pid 3048386 on        lab device  0 [0000:02:00] NVIDIA GeForce RTX 5090
#
#                                                              out-of-place                       in-place          
#       size         count      type   redop    root     time   algbw   busbw  #wrong     time   algbw   busbw  #wrong 
#        (B)    (elements)                               (us)  (GB/s)  (GB/s)             (us)  (GB/s)  (GB/s)         
      381468         95367     float     sum      -1  4362.93    0.09    0.09       0  4362.89    0.09    0.09       0
# Out of bounds values : 0 OK
# Avg bus bandwidth    : 0.0874343 
#
# Collective test concluded: all_reduce_perf_mpi
#
```

stderr:
```text
Authorization required, but no authorization protocol specified

Authorization required, but no authorization protocol specified

Authorization required, but no authorization protocol specified

Authorization required, but no authorization protocol specified
```

#### exp3-fine-transition-corrected-default-rep1-476836

Command:
```bash
mpirun --oversubscribe -np 1 /usr/bin/env LD_LIBRARY_PATH=/home/yunwei37/workspace/nccl-eBPF/nccl/build/lib NCCL_TESTS_DEVICE=0 NCCL_P2P_DISABLE=1 NCCL_NET=Socket NCCL_SHM_DISABLE=1 NCCL_HOSTID=exp3-fine-transition-corrected-default-rep1-476836-rank0 /home/yunwei37/workspace/nccl-eBPF/nccl-tests/build/all_reduce_perf_mpi -b 476836 -e 476836 -g 1 -n 100 -w 20 : -np 1 /usr/bin/env LD_LIBRARY_PATH=/home/yunwei37/workspace/nccl-eBPF/nccl/build/lib NCCL_TESTS_DEVICE=0 NCCL_P2P_DISABLE=1 NCCL_NET=Socket NCCL_SHM_DISABLE=1 NCCL_HOSTID=exp3-fine-transition-corrected-default-rep1-476836-rank1 /home/yunwei37/workspace/nccl-eBPF/nccl-tests/build/all_reduce_perf_mpi -b 476836 -e 476836 -g 1 -n 100 -w 20
```

stdout:
```text
# nccl-tests version 2.18.0 nccl-headers=22907 nccl-library=22907
# Collective test starting: all_reduce_perf_mpi
# nThread 1 nGpus 1 minBytes 476836 maxBytes 476836 step: 1048576(bytes) warmup iters: 20 iters: 100 agg iters: 1 validation: 1 graph: 0
#
# Using devices
#  Rank  0 Group  0 Pid 3048500 on        lab device  0 [0000:02:00] NVIDIA GeForce RTX 5090
#  Rank  1 Group  0 Pid 3048501 on        lab device  0 [0000:02:00] NVIDIA GeForce RTX 5090
#
#                                                              out-of-place                       in-place          
#       size         count      type   redop    root     time   algbw   busbw  #wrong     time   algbw   busbw  #wrong 
#        (B)    (elements)                               (us)  (GB/s)  (GB/s)             (us)  (GB/s)  (GB/s)         
      476836        119209     float     sum      -1  4363.66    0.11    0.11       0  4363.26    0.11    0.11       0
# Out of bounds values : 0 OK
# Avg bus bandwidth    : 0.109279 
#
# Collective test concluded: all_reduce_perf_mpi
#
```

stderr:
```text
Authorization required, but no authorization protocol specified

Authorization required, but no authorization protocol specified

Authorization required, but no authorization protocol specified

Authorization required, but no authorization protocol specified
```

#### exp3-fine-transition-corrected-simple-rep1-32768

Command:
```bash
mpirun --oversubscribe -np 1 /usr/bin/env LD_LIBRARY_PATH=/home/yunwei37/workspace/nccl-eBPF/nccl/build/lib NCCL_TESTS_DEVICE=0 NCCL_P2P_DISABLE=1 NCCL_NET=Socket NCCL_PROTO=Simple NCCL_SHM_DISABLE=1 NCCL_HOSTID=exp3-fine-transition-corrected-simple-rep1-32768-rank0 /home/yunwei37/workspace/nccl-eBPF/nccl-tests/build/all_reduce_perf_mpi -b 32768 -e 32768 -g 1 -n 100 -w 20 : -np 1 /usr/bin/env LD_LIBRARY_PATH=/home/yunwei37/workspace/nccl-eBPF/nccl/build/lib NCCL_TESTS_DEVICE=0 NCCL_P2P_DISABLE=1 NCCL_NET=Socket NCCL_PROTO=Simple NCCL_SHM_DISABLE=1 NCCL_HOSTID=exp3-fine-transition-corrected-simple-rep1-32768-rank1 /home/yunwei37/workspace/nccl-eBPF/nccl-tests/build/all_reduce_perf_mpi -b 32768 -e 32768 -g 1 -n 100 -w 20
```

stdout:
```text
# nccl-tests version 2.18.0 nccl-headers=22907 nccl-library=22907
# Collective test starting: all_reduce_perf_mpi
# nThread 1 nGpus 1 minBytes 32768 maxBytes 32768 step: 1048576(bytes) warmup iters: 20 iters: 100 agg iters: 1 validation: 1 graph: 0
#
# Using devices
#  Rank  0 Group  0 Pid 3048589 on        lab device  0 [0000:02:00] NVIDIA GeForce RTX 5090
#  Rank  1 Group  0 Pid 3048590 on        lab device  0 [0000:02:00] NVIDIA GeForce RTX 5090
#
#                                                              out-of-place                       in-place          
#       size         count      type   redop    root     time   algbw   busbw  #wrong     time   algbw   busbw  #wrong 
#        (B)    (elements)                               (us)  (GB/s)  (GB/s)             (us)  (GB/s)  (GB/s)         
       32768          8192     float     sum      -1  4358.63    0.01    0.01       0  4358.64    0.01    0.01       0
# Out of bounds values : 0 OK
# Avg bus bandwidth    : 0.00751795 
#
# Collective test concluded: all_reduce_perf_mpi
#
```

stderr:
```text
Authorization required, but no authorization protocol specified

Authorization required, but no authorization protocol specified

Authorization required, but no authorization protocol specified

Authorization required, but no authorization protocol specified
```

#### exp3-fine-transition-corrected-simple-rep1-40960

Command:
```bash
mpirun --oversubscribe -np 1 /usr/bin/env LD_LIBRARY_PATH=/home/yunwei37/workspace/nccl-eBPF/nccl/build/lib NCCL_TESTS_DEVICE=0 NCCL_P2P_DISABLE=1 NCCL_NET=Socket NCCL_PROTO=Simple NCCL_SHM_DISABLE=1 NCCL_HOSTID=exp3-fine-transition-corrected-simple-rep1-40960-rank0 /home/yunwei37/workspace/nccl-eBPF/nccl-tests/build/all_reduce_perf_mpi -b 40960 -e 40960 -g 1 -n 100 -w 20 : -np 1 /usr/bin/env LD_LIBRARY_PATH=/home/yunwei37/workspace/nccl-eBPF/nccl/build/lib NCCL_TESTS_DEVICE=0 NCCL_P2P_DISABLE=1 NCCL_NET=Socket NCCL_PROTO=Simple NCCL_SHM_DISABLE=1 NCCL_HOSTID=exp3-fine-transition-corrected-simple-rep1-40960-rank1 /home/yunwei37/workspace/nccl-eBPF/nccl-tests/build/all_reduce_perf_mpi -b 40960 -e 40960 -g 1 -n 100 -w 20
```

stdout:
```text
# nccl-tests version 2.18.0 nccl-headers=22907 nccl-library=22907
# Collective test starting: all_reduce_perf_mpi
# nThread 1 nGpus 1 minBytes 40960 maxBytes 40960 step: 1048576(bytes) warmup iters: 20 iters: 100 agg iters: 1 validation: 1 graph: 0
#
# Using devices
#  Rank  0 Group  0 Pid 3048725 on        lab device  0 [0000:02:00] NVIDIA GeForce RTX 5090
#  Rank  1 Group  0 Pid 3048726 on        lab device  0 [0000:02:00] NVIDIA GeForce RTX 5090
#
#                                                              out-of-place                       in-place          
#       size         count      type   redop    root     time   algbw   busbw  #wrong     time   algbw   busbw  #wrong 
#        (B)    (elements)                               (us)  (GB/s)  (GB/s)             (us)  (GB/s)  (GB/s)         
       40960         10240     float     sum      -1  4358.55    0.01    0.01       0  4358.31    0.01    0.01       0
# Out of bounds values : 0 OK
# Avg bus bandwidth    : 0.00939788 
#
# Collective test concluded: all_reduce_perf_mpi
#
```

stderr:
```text
Authorization required, but no authorization protocol specified

Authorization required, but no authorization protocol specified

Authorization required, but no authorization protocol specified

Authorization required, but no authorization protocol specified
```

#### exp3-fine-transition-corrected-simple-rep1-51200

Command:
```bash
mpirun --oversubscribe -np 1 /usr/bin/env LD_LIBRARY_PATH=/home/yunwei37/workspace/nccl-eBPF/nccl/build/lib NCCL_TESTS_DEVICE=0 NCCL_P2P_DISABLE=1 NCCL_NET=Socket NCCL_PROTO=Simple NCCL_SHM_DISABLE=1 NCCL_HOSTID=exp3-fine-transition-corrected-simple-rep1-51200-rank0 /home/yunwei37/workspace/nccl-eBPF/nccl-tests/build/all_reduce_perf_mpi -b 51200 -e 51200 -g 1 -n 100 -w 20 : -np 1 /usr/bin/env LD_LIBRARY_PATH=/home/yunwei37/workspace/nccl-eBPF/nccl/build/lib NCCL_TESTS_DEVICE=0 NCCL_P2P_DISABLE=1 NCCL_NET=Socket NCCL_PROTO=Simple NCCL_SHM_DISABLE=1 NCCL_HOSTID=exp3-fine-transition-corrected-simple-rep1-51200-rank1 /home/yunwei37/workspace/nccl-eBPF/nccl-tests/build/all_reduce_perf_mpi -b 51200 -e 51200 -g 1 -n 100 -w 20
```

stdout:
```text
# nccl-tests version 2.18.0 nccl-headers=22907 nccl-library=22907
# Collective test starting: all_reduce_perf_mpi
# nThread 1 nGpus 1 minBytes 51200 maxBytes 51200 step: 1048576(bytes) warmup iters: 20 iters: 100 agg iters: 1 validation: 1 graph: 0
#
# Using devices
#  Rank  0 Group  0 Pid 3048792 on        lab device  0 [0000:02:00] NVIDIA GeForce RTX 5090
#  Rank  1 Group  0 Pid 3048793 on        lab device  0 [0000:02:00] NVIDIA GeForce RTX 5090
#
#                                                              out-of-place                       in-place          
#       size         count      type   redop    root     time   algbw   busbw  #wrong     time   algbw   busbw  #wrong 
#        (B)    (elements)                               (us)  (GB/s)  (GB/s)             (us)  (GB/s)  (GB/s)         
       51200         12800     float     sum      -1  4358.92    0.01    0.01       0  4358.98    0.01    0.01       0
# Out of bounds values : 0 OK
# Avg bus bandwidth    : 0.0117459 
#
# Collective test concluded: all_reduce_perf_mpi
#
```

stderr:
```text
Authorization required, but no authorization protocol specified

Authorization required, but no authorization protocol specified

Authorization required, but no authorization protocol specified

Authorization required, but no authorization protocol specified
```

#### exp3-fine-transition-corrected-simple-rep1-64000

Command:
```bash
mpirun --oversubscribe -np 1 /usr/bin/env LD_LIBRARY_PATH=/home/yunwei37/workspace/nccl-eBPF/nccl/build/lib NCCL_TESTS_DEVICE=0 NCCL_P2P_DISABLE=1 NCCL_NET=Socket NCCL_PROTO=Simple NCCL_SHM_DISABLE=1 NCCL_HOSTID=exp3-fine-transition-corrected-simple-rep1-64000-rank0 /home/yunwei37/workspace/nccl-eBPF/nccl-tests/build/all_reduce_perf_mpi -b 64000 -e 64000 -g 1 -n 100 -w 20 : -np 1 /usr/bin/env LD_LIBRARY_PATH=/home/yunwei37/workspace/nccl-eBPF/nccl/build/lib NCCL_TESTS_DEVICE=0 NCCL_P2P_DISABLE=1 NCCL_NET=Socket NCCL_PROTO=Simple NCCL_SHM_DISABLE=1 NCCL_HOSTID=exp3-fine-transition-corrected-simple-rep1-64000-rank1 /home/yunwei37/workspace/nccl-eBPF/nccl-tests/build/all_reduce_perf_mpi -b 64000 -e 64000 -g 1 -n 100 -w 20
```

stdout:
```text
# nccl-tests version 2.18.0 nccl-headers=22907 nccl-library=22907
# Collective test starting: all_reduce_perf_mpi
# nThread 1 nGpus 1 minBytes 64000 maxBytes 64000 step: 1048576(bytes) warmup iters: 20 iters: 100 agg iters: 1 validation: 1 graph: 0
#
# Using devices
#  Rank  0 Group  0 Pid 3049189 on        lab device  0 [0000:02:00] NVIDIA GeForce RTX 5090
#  Rank  1 Group  0 Pid 3049190 on        lab device  0 [0000:02:00] NVIDIA GeForce RTX 5090
#
#                                                              out-of-place                       in-place          
#       size         count      type   redop    root     time   algbw   busbw  #wrong     time   algbw   busbw  #wrong 
#        (B)    (elements)                               (us)  (GB/s)  (GB/s)             (us)  (GB/s)  (GB/s)         
       64000         16000     float     sum      -1  4358.33    0.01    0.01       0  4358.52    0.01    0.01       0
# Out of bounds values : 0 OK
# Avg bus bandwidth    : 0.0146842 
#
# Collective test concluded: all_reduce_perf_mpi
#
```

stderr:
```text
Authorization required, but no authorization protocol specified

Authorization required, but no authorization protocol specified

Authorization required, but no authorization protocol specified

Authorization required, but no authorization protocol specified
```

#### exp3-fine-transition-corrected-simple-rep1-80000

Command:
```bash
mpirun --oversubscribe -np 1 /usr/bin/env LD_LIBRARY_PATH=/home/yunwei37/workspace/nccl-eBPF/nccl/build/lib NCCL_TESTS_DEVICE=0 NCCL_P2P_DISABLE=1 NCCL_NET=Socket NCCL_PROTO=Simple NCCL_SHM_DISABLE=1 NCCL_HOSTID=exp3-fine-transition-corrected-simple-rep1-80000-rank0 /home/yunwei37/workspace/nccl-eBPF/nccl-tests/build/all_reduce_perf_mpi -b 80000 -e 80000 -g 1 -n 100 -w 20 : -np 1 /usr/bin/env LD_LIBRARY_PATH=/home/yunwei37/workspace/nccl-eBPF/nccl/build/lib NCCL_TESTS_DEVICE=0 NCCL_P2P_DISABLE=1 NCCL_NET=Socket NCCL_PROTO=Simple NCCL_SHM_DISABLE=1 NCCL_HOSTID=exp3-fine-transition-corrected-simple-rep1-80000-rank1 /home/yunwei37/workspace/nccl-eBPF/nccl-tests/build/all_reduce_perf_mpi -b 80000 -e 80000 -g 1 -n 100 -w 20
```

stdout:
```text
# nccl-tests version 2.18.0 nccl-headers=22907 nccl-library=22907
# Collective test starting: all_reduce_perf_mpi
# nThread 1 nGpus 1 minBytes 80000 maxBytes 80000 step: 1048576(bytes) warmup iters: 20 iters: 100 agg iters: 1 validation: 1 graph: 0
#
# Using devices
#  Rank  0 Group  0 Pid 3049239 on        lab device  0 [0000:02:00] NVIDIA GeForce RTX 5090
#  Rank  1 Group  0 Pid 3049240 on        lab device  0 [0000:02:00] NVIDIA GeForce RTX 5090
#
#                                                              out-of-place                       in-place          
#       size         count      type   redop    root     time   algbw   busbw  #wrong     time   algbw   busbw  #wrong 
#        (B)    (elements)                               (us)  (GB/s)  (GB/s)             (us)  (GB/s)  (GB/s)         
       80000         20000     float     sum      -1  4359.83    0.02    0.02       0  4360.04    0.02    0.02       0
# Out of bounds values : 0 OK
# Avg bus bandwidth    : 0.0183489 
#
# Collective test concluded: all_reduce_perf_mpi
#
```

stderr:
```text
Authorization required, but no authorization protocol specified

Authorization required, but no authorization protocol specified

Authorization required, but no authorization protocol specified

Authorization required, but no authorization protocol specified
```

#### exp3-fine-transition-corrected-simple-rep1-100000

Command:
```bash
mpirun --oversubscribe -np 1 /usr/bin/env LD_LIBRARY_PATH=/home/yunwei37/workspace/nccl-eBPF/nccl/build/lib NCCL_TESTS_DEVICE=0 NCCL_P2P_DISABLE=1 NCCL_NET=Socket NCCL_PROTO=Simple NCCL_SHM_DISABLE=1 NCCL_HOSTID=exp3-fine-transition-corrected-simple-rep1-100000-rank0 /home/yunwei37/workspace/nccl-eBPF/nccl-tests/build/all_reduce_perf_mpi -b 100000 -e 100000 -g 1 -n 100 -w 20 : -np 1 /usr/bin/env LD_LIBRARY_PATH=/home/yunwei37/workspace/nccl-eBPF/nccl/build/lib NCCL_TESTS_DEVICE=0 NCCL_P2P_DISABLE=1 NCCL_NET=Socket NCCL_PROTO=Simple NCCL_SHM_DISABLE=1 NCCL_HOSTID=exp3-fine-transition-corrected-simple-rep1-100000-rank1 /home/yunwei37/workspace/nccl-eBPF/nccl-tests/build/all_reduce_perf_mpi -b 100000 -e 100000 -g 1 -n 100 -w 20
```

stdout:
```text
# nccl-tests version 2.18.0 nccl-headers=22907 nccl-library=22907
# Collective test starting: all_reduce_perf_mpi
# nThread 1 nGpus 1 minBytes 100000 maxBytes 100000 step: 1048576(bytes) warmup iters: 20 iters: 100 agg iters: 1 validation: 1 graph: 0
#
# Using devices
#  Rank  0 Group  0 Pid 3049310 on        lab device  0 [0000:02:00] NVIDIA GeForce RTX 5090
#  Rank  1 Group  0 Pid 3049311 on        lab device  0 [0000:02:00] NVIDIA GeForce RTX 5090
#
#                                                              out-of-place                       in-place          
#       size         count      type   redop    root     time   algbw   busbw  #wrong     time   algbw   busbw  #wrong 
#        (B)    (elements)                               (us)  (GB/s)  (GB/s)             (us)  (GB/s)  (GB/s)         
      100000         25000     float     sum      -1  4363.12    0.02    0.02       0  4362.39    0.02    0.02       0
# Out of bounds values : 0 OK
# Avg bus bandwidth    : 0.0229213 
#
# Collective test concluded: all_reduce_perf_mpi
#
```

stderr:
```text
Authorization required, but no authorization protocol specified

Authorization required, but no authorization protocol specified

Authorization required, but no authorization protocol specified

Authorization required, but no authorization protocol specified
```

#### exp3-fine-transition-corrected-simple-rep1-125000

Command:
```bash
mpirun --oversubscribe -np 1 /usr/bin/env LD_LIBRARY_PATH=/home/yunwei37/workspace/nccl-eBPF/nccl/build/lib NCCL_TESTS_DEVICE=0 NCCL_P2P_DISABLE=1 NCCL_NET=Socket NCCL_PROTO=Simple NCCL_SHM_DISABLE=1 NCCL_HOSTID=exp3-fine-transition-corrected-simple-rep1-125000-rank0 /home/yunwei37/workspace/nccl-eBPF/nccl-tests/build/all_reduce_perf_mpi -b 125000 -e 125000 -g 1 -n 100 -w 20 : -np 1 /usr/bin/env LD_LIBRARY_PATH=/home/yunwei37/workspace/nccl-eBPF/nccl/build/lib NCCL_TESTS_DEVICE=0 NCCL_P2P_DISABLE=1 NCCL_NET=Socket NCCL_PROTO=Simple NCCL_SHM_DISABLE=1 NCCL_HOSTID=exp3-fine-transition-corrected-simple-rep1-125000-rank1 /home/yunwei37/workspace/nccl-eBPF/nccl-tests/build/all_reduce_perf_mpi -b 125000 -e 125000 -g 1 -n 100 -w 20
```

stdout:
```text
# nccl-tests version 2.18.0 nccl-headers=22907 nccl-library=22907
# Collective test starting: all_reduce_perf_mpi
# nThread 1 nGpus 1 minBytes 125000 maxBytes 125000 step: 1048576(bytes) warmup iters: 20 iters: 100 agg iters: 1 validation: 1 graph: 0
#
# Using devices
#  Rank  0 Group  0 Pid 3049362 on        lab device  0 [0000:02:00] NVIDIA GeForce RTX 5090
#  Rank  1 Group  0 Pid 3049363 on        lab device  0 [0000:02:00] NVIDIA GeForce RTX 5090
#
#                                                              out-of-place                       in-place          
#       size         count      type   redop    root     time   algbw   busbw  #wrong     time   algbw   busbw  #wrong 
#        (B)    (elements)                               (us)  (GB/s)  (GB/s)             (us)  (GB/s)  (GB/s)         
      125000         31250     float     sum      -1  4361.54    0.03    0.03       0  4361.52    0.03    0.03       0
# Out of bounds values : 0 OK
# Avg bus bandwidth    : 0.0286597 
#
# Collective test concluded: all_reduce_perf_mpi
#
```

stderr:
```text
Authorization required, but no authorization protocol specified

Authorization required, but no authorization protocol specified

Authorization required, but no authorization protocol specified

Authorization required, but no authorization protocol specified
```

#### exp3-fine-transition-corrected-simple-rep1-156250

Command:
```bash
mpirun --oversubscribe -np 1 /usr/bin/env LD_LIBRARY_PATH=/home/yunwei37/workspace/nccl-eBPF/nccl/build/lib NCCL_TESTS_DEVICE=0 NCCL_P2P_DISABLE=1 NCCL_NET=Socket NCCL_PROTO=Simple NCCL_SHM_DISABLE=1 NCCL_HOSTID=exp3-fine-transition-corrected-simple-rep1-156250-rank0 /home/yunwei37/workspace/nccl-eBPF/nccl-tests/build/all_reduce_perf_mpi -b 156250 -e 156250 -g 1 -n 100 -w 20 : -np 1 /usr/bin/env LD_LIBRARY_PATH=/home/yunwei37/workspace/nccl-eBPF/nccl/build/lib NCCL_TESTS_DEVICE=0 NCCL_P2P_DISABLE=1 NCCL_NET=Socket NCCL_PROTO=Simple NCCL_SHM_DISABLE=1 NCCL_HOSTID=exp3-fine-transition-corrected-simple-rep1-156250-rank1 /home/yunwei37/workspace/nccl-eBPF/nccl-tests/build/all_reduce_perf_mpi -b 156250 -e 156250 -g 1 -n 100 -w 20
```

stdout:
```text
# nccl-tests version 2.18.0 nccl-headers=22907 nccl-library=22907
# Collective test starting: all_reduce_perf_mpi
# nThread 1 nGpus 1 minBytes 156250 maxBytes 156250 step: 1048576(bytes) warmup iters: 20 iters: 100 agg iters: 1 validation: 1 graph: 0
#
# Using devices
#  Rank  0 Group  0 Pid 3049414 on        lab device  0 [0000:02:00] NVIDIA GeForce RTX 5090
#  Rank  1 Group  0 Pid 3049415 on        lab device  0 [0000:02:00] NVIDIA GeForce RTX 5090
#
#                                                              out-of-place                       in-place          
#       size         count      type   redop    root     time   algbw   busbw  #wrong     time   algbw   busbw  #wrong 
#        (B)    (elements)                               (us)  (GB/s)  (GB/s)             (us)  (GB/s)  (GB/s)         
      156248         39062     float     sum      -1  4363.23    0.04    0.04       0  4363.08    0.04    0.04       0
# Out of bounds values : 0 OK
# Avg bus bandwidth    : 0.0358108 
#
# Collective test concluded: all_reduce_perf_mpi
#
```

stderr:
```text
Authorization required, but no authorization protocol specified

Authorization required, but no authorization protocol specified

Authorization required, but no authorization protocol specified

Authorization required, but no authorization protocol specified
```

#### exp3-fine-transition-corrected-simple-rep1-195312

Command:
```bash
mpirun --oversubscribe -np 1 /usr/bin/env LD_LIBRARY_PATH=/home/yunwei37/workspace/nccl-eBPF/nccl/build/lib NCCL_TESTS_DEVICE=0 NCCL_P2P_DISABLE=1 NCCL_NET=Socket NCCL_PROTO=Simple NCCL_SHM_DISABLE=1 NCCL_HOSTID=exp3-fine-transition-corrected-simple-rep1-195312-rank0 /home/yunwei37/workspace/nccl-eBPF/nccl-tests/build/all_reduce_perf_mpi -b 195312 -e 195312 -g 1 -n 100 -w 20 : -np 1 /usr/bin/env LD_LIBRARY_PATH=/home/yunwei37/workspace/nccl-eBPF/nccl/build/lib NCCL_TESTS_DEVICE=0 NCCL_P2P_DISABLE=1 NCCL_NET=Socket NCCL_PROTO=Simple NCCL_SHM_DISABLE=1 NCCL_HOSTID=exp3-fine-transition-corrected-simple-rep1-195312-rank1 /home/yunwei37/workspace/nccl-eBPF/nccl-tests/build/all_reduce_perf_mpi -b 195312 -e 195312 -g 1 -n 100 -w 20
```

stdout:
```text
# nccl-tests version 2.18.0 nccl-headers=22907 nccl-library=22907
# Collective test starting: all_reduce_perf_mpi
# nThread 1 nGpus 1 minBytes 195312 maxBytes 195312 step: 1048576(bytes) warmup iters: 20 iters: 100 agg iters: 1 validation: 1 graph: 0
#
# Using devices
#  Rank  0 Group  0 Pid 3049494 on        lab device  0 [0000:02:00] NVIDIA GeForce RTX 5090
#  Rank  1 Group  0 Pid 3049495 on        lab device  0 [0000:02:00] NVIDIA GeForce RTX 5090
#
#                                                              out-of-place                       in-place          
#       size         count      type   redop    root     time   algbw   busbw  #wrong     time   algbw   busbw  #wrong 
#        (B)    (elements)                               (us)  (GB/s)  (GB/s)             (us)  (GB/s)  (GB/s)         
      195312         48828     float     sum      -1  4362.37    0.04    0.04       0  4363.19    0.04    0.04       0
# Out of bounds values : 0 OK
# Avg bus bandwidth    : 0.0447678 
#
# Collective test concluded: all_reduce_perf_mpi
#
```

stderr:
```text
Authorization required, but no authorization protocol specified

Authorization required, but no authorization protocol specified

Authorization required, but no authorization protocol specified

Authorization required, but no authorization protocol specified
```

#### exp3-fine-transition-corrected-simple-rep1-244140

Command:
```bash
mpirun --oversubscribe -np 1 /usr/bin/env LD_LIBRARY_PATH=/home/yunwei37/workspace/nccl-eBPF/nccl/build/lib NCCL_TESTS_DEVICE=0 NCCL_P2P_DISABLE=1 NCCL_NET=Socket NCCL_PROTO=Simple NCCL_SHM_DISABLE=1 NCCL_HOSTID=exp3-fine-transition-corrected-simple-rep1-244140-rank0 /home/yunwei37/workspace/nccl-eBPF/nccl-tests/build/all_reduce_perf_mpi -b 244140 -e 244140 -g 1 -n 100 -w 20 : -np 1 /usr/bin/env LD_LIBRARY_PATH=/home/yunwei37/workspace/nccl-eBPF/nccl/build/lib NCCL_TESTS_DEVICE=0 NCCL_P2P_DISABLE=1 NCCL_NET=Socket NCCL_PROTO=Simple NCCL_SHM_DISABLE=1 NCCL_HOSTID=exp3-fine-transition-corrected-simple-rep1-244140-rank1 /home/yunwei37/workspace/nccl-eBPF/nccl-tests/build/all_reduce_perf_mpi -b 244140 -e 244140 -g 1 -n 100 -w 20
```

stdout:
```text
# nccl-tests version 2.18.0 nccl-headers=22907 nccl-library=22907
# Collective test starting: all_reduce_perf_mpi
# nThread 1 nGpus 1 minBytes 244140 maxBytes 244140 step: 1048576(bytes) warmup iters: 20 iters: 100 agg iters: 1 validation: 1 graph: 0
#
# Using devices
#  Rank  0 Group  0 Pid 3049561 on        lab device  0 [0000:02:00] NVIDIA GeForce RTX 5090
#  Rank  1 Group  0 Pid 3049562 on        lab device  0 [0000:02:00] NVIDIA GeForce RTX 5090
#
#                                                              out-of-place                       in-place          
#       size         count      type   redop    root     time   algbw   busbw  #wrong     time   algbw   busbw  #wrong 
#        (B)    (elements)                               (us)  (GB/s)  (GB/s)             (us)  (GB/s)  (GB/s)         
      244140         61035     float     sum      -1  4362.71    0.06    0.06       0  4363.04    0.06    0.06       0
# Out of bounds values : 0 OK
# Avg bus bandwidth    : 0.0559585 
#
# Collective test concluded: all_reduce_perf_mpi
#
```

stderr:
```text
Authorization required, but no authorization protocol specified

Authorization required, but no authorization protocol specified

Authorization required, but no authorization protocol specified

Authorization required, but no authorization protocol specified
```

#### exp3-fine-transition-corrected-simple-rep1-305175

Command:
```bash
mpirun --oversubscribe -np 1 /usr/bin/env LD_LIBRARY_PATH=/home/yunwei37/workspace/nccl-eBPF/nccl/build/lib NCCL_TESTS_DEVICE=0 NCCL_P2P_DISABLE=1 NCCL_NET=Socket NCCL_PROTO=Simple NCCL_SHM_DISABLE=1 NCCL_HOSTID=exp3-fine-transition-corrected-simple-rep1-305175-rank0 /home/yunwei37/workspace/nccl-eBPF/nccl-tests/build/all_reduce_perf_mpi -b 305175 -e 305175 -g 1 -n 100 -w 20 : -np 1 /usr/bin/env LD_LIBRARY_PATH=/home/yunwei37/workspace/nccl-eBPF/nccl/build/lib NCCL_TESTS_DEVICE=0 NCCL_P2P_DISABLE=1 NCCL_NET=Socket NCCL_PROTO=Simple NCCL_SHM_DISABLE=1 NCCL_HOSTID=exp3-fine-transition-corrected-simple-rep1-305175-rank1 /home/yunwei37/workspace/nccl-eBPF/nccl-tests/build/all_reduce_perf_mpi -b 305175 -e 305175 -g 1 -n 100 -w 20
```

stdout:
```text
# nccl-tests version 2.18.0 nccl-headers=22907 nccl-library=22907
# Collective test starting: all_reduce_perf_mpi
# nThread 1 nGpus 1 minBytes 305175 maxBytes 305175 step: 1048576(bytes) warmup iters: 20 iters: 100 agg iters: 1 validation: 1 graph: 0
#
# Using devices
#  Rank  0 Group  0 Pid 3049642 on        lab device  0 [0000:02:00] NVIDIA GeForce RTX 5090
#  Rank  1 Group  0 Pid 3049643 on        lab device  0 [0000:02:00] NVIDIA GeForce RTX 5090
#
#                                                              out-of-place                       in-place          
#       size         count      type   redop    root     time   algbw   busbw  #wrong     time   algbw   busbw  #wrong 
#        (B)    (elements)                               (us)  (GB/s)  (GB/s)             (us)  (GB/s)  (GB/s)         
      305172         76293     float     sum      -1  4362.36    0.07    0.07       0  4362.03    0.07    0.07       0
# Out of bounds values : 0 OK
# Avg bus bandwidth    : 0.0699584 
#
# Collective test concluded: all_reduce_perf_mpi
#
```

stderr:
```text
Authorization required, but no authorization protocol specified

Authorization required, but no authorization protocol specified

Authorization required, but no authorization protocol specified

Authorization required, but no authorization protocol specified
```

#### exp3-fine-transition-corrected-simple-rep1-381469

Command:
```bash
mpirun --oversubscribe -np 1 /usr/bin/env LD_LIBRARY_PATH=/home/yunwei37/workspace/nccl-eBPF/nccl/build/lib NCCL_TESTS_DEVICE=0 NCCL_P2P_DISABLE=1 NCCL_NET=Socket NCCL_PROTO=Simple NCCL_SHM_DISABLE=1 NCCL_HOSTID=exp3-fine-transition-corrected-simple-rep1-381469-rank0 /home/yunwei37/workspace/nccl-eBPF/nccl-tests/build/all_reduce_perf_mpi -b 381469 -e 381469 -g 1 -n 100 -w 20 : -np 1 /usr/bin/env LD_LIBRARY_PATH=/home/yunwei37/workspace/nccl-eBPF/nccl/build/lib NCCL_TESTS_DEVICE=0 NCCL_P2P_DISABLE=1 NCCL_NET=Socket NCCL_PROTO=Simple NCCL_SHM_DISABLE=1 NCCL_HOSTID=exp3-fine-transition-corrected-simple-rep1-381469-rank1 /home/yunwei37/workspace/nccl-eBPF/nccl-tests/build/all_reduce_perf_mpi -b 381469 -e 381469 -g 1 -n 100 -w 20
```

stdout:
```text
# nccl-tests version 2.18.0 nccl-headers=22907 nccl-library=22907
# Collective test starting: all_reduce_perf_mpi
# nThread 1 nGpus 1 minBytes 381469 maxBytes 381469 step: 1048576(bytes) warmup iters: 20 iters: 100 agg iters: 1 validation: 1 graph: 0
#
# Using devices
#  Rank  0 Group  0 Pid 3049696 on        lab device  0 [0000:02:00] NVIDIA GeForce RTX 5090
#  Rank  1 Group  0 Pid 3049697 on        lab device  0 [0000:02:00] NVIDIA GeForce RTX 5090
#
#                                                              out-of-place                       in-place          
#       size         count      type   redop    root     time   algbw   busbw  #wrong     time   algbw   busbw  #wrong 
#        (B)    (elements)                               (us)  (GB/s)  (GB/s)             (us)  (GB/s)  (GB/s)         
      381468         95367     float     sum      -1  4362.58    0.09    0.09       0  4362.81    0.09    0.09       0
# Out of bounds values : 0 OK
# Avg bus bandwidth    : 0.0874386 
#
# Collective test concluded: all_reduce_perf_mpi
#
```

stderr:
```text
Authorization required, but no authorization protocol specified

Authorization required, but no authorization protocol specified

Authorization required, but no authorization protocol specified

Authorization required, but no authorization protocol specified
```

#### exp3-fine-transition-corrected-simple-rep1-476836

Command:
```bash
mpirun --oversubscribe -np 1 /usr/bin/env LD_LIBRARY_PATH=/home/yunwei37/workspace/nccl-eBPF/nccl/build/lib NCCL_TESTS_DEVICE=0 NCCL_P2P_DISABLE=1 NCCL_NET=Socket NCCL_PROTO=Simple NCCL_SHM_DISABLE=1 NCCL_HOSTID=exp3-fine-transition-corrected-simple-rep1-476836-rank0 /home/yunwei37/workspace/nccl-eBPF/nccl-tests/build/all_reduce_perf_mpi -b 476836 -e 476836 -g 1 -n 100 -w 20 : -np 1 /usr/bin/env LD_LIBRARY_PATH=/home/yunwei37/workspace/nccl-eBPF/nccl/build/lib NCCL_TESTS_DEVICE=0 NCCL_P2P_DISABLE=1 NCCL_NET=Socket NCCL_PROTO=Simple NCCL_SHM_DISABLE=1 NCCL_HOSTID=exp3-fine-transition-corrected-simple-rep1-476836-rank1 /home/yunwei37/workspace/nccl-eBPF/nccl-tests/build/all_reduce_perf_mpi -b 476836 -e 476836 -g 1 -n 100 -w 20
```

stdout:
```text
# nccl-tests version 2.18.0 nccl-headers=22907 nccl-library=22907
# Collective test starting: all_reduce_perf_mpi
# nThread 1 nGpus 1 minBytes 476836 maxBytes 476836 step: 1048576(bytes) warmup iters: 20 iters: 100 agg iters: 1 validation: 1 graph: 0
#
# Using devices
#  Rank  0 Group  0 Pid 3049783 on        lab device  0 [0000:02:00] NVIDIA GeForce RTX 5090
#  Rank  1 Group  0 Pid 3049784 on        lab device  0 [0000:02:00] NVIDIA GeForce RTX 5090
#
#                                                              out-of-place                       in-place          
#       size         count      type   redop    root     time   algbw   busbw  #wrong     time   algbw   busbw  #wrong 
#        (B)    (elements)                               (us)  (GB/s)  (GB/s)             (us)  (GB/s)  (GB/s)         
      476836        119209     float     sum      -1  4362.93    0.11    0.11       0  4363.17    0.11    0.11       0
# Out of bounds values : 0 OK
# Avg bus bandwidth    : 0.10929 
#
# Collective test concluded: all_reduce_perf_mpi
#
```

stderr:
```text
Authorization required, but no authorization protocol specified

Authorization required, but no authorization protocol specified

Authorization required, but no authorization protocol specified

Authorization required, but no authorization protocol specified
```

#### exp3-fine-transition-corrected-default-rep2-32768

Command:
```bash
mpirun --oversubscribe -np 1 /usr/bin/env LD_LIBRARY_PATH=/home/yunwei37/workspace/nccl-eBPF/nccl/build/lib NCCL_TESTS_DEVICE=0 NCCL_P2P_DISABLE=1 NCCL_NET=Socket NCCL_SHM_DISABLE=1 NCCL_HOSTID=exp3-fine-transition-corrected-default-rep2-32768-rank0 /home/yunwei37/workspace/nccl-eBPF/nccl-tests/build/all_reduce_perf_mpi -b 32768 -e 32768 -g 1 -n 100 -w 20 : -np 1 /usr/bin/env LD_LIBRARY_PATH=/home/yunwei37/workspace/nccl-eBPF/nccl/build/lib NCCL_TESTS_DEVICE=0 NCCL_P2P_DISABLE=1 NCCL_NET=Socket NCCL_SHM_DISABLE=1 NCCL_HOSTID=exp3-fine-transition-corrected-default-rep2-32768-rank1 /home/yunwei37/workspace/nccl-eBPF/nccl-tests/build/all_reduce_perf_mpi -b 32768 -e 32768 -g 1 -n 100 -w 20
```

stdout:
```text
# nccl-tests version 2.18.0 nccl-headers=22907 nccl-library=22907
# Collective test starting: all_reduce_perf_mpi
# nThread 1 nGpus 1 minBytes 32768 maxBytes 32768 step: 1048576(bytes) warmup iters: 20 iters: 100 agg iters: 1 validation: 1 graph: 0
#
# Using devices
#  Rank  0 Group  0 Pid 3049807 on        lab device  0 [0000:02:00] NVIDIA GeForce RTX 5090
#  Rank  1 Group  0 Pid 3049808 on        lab device  0 [0000:02:00] NVIDIA GeForce RTX 5090
#
#                                                              out-of-place                       in-place          
#       size         count      type   redop    root     time   algbw   busbw  #wrong     time   algbw   busbw  #wrong 
#        (B)    (elements)                               (us)  (GB/s)  (GB/s)             (us)  (GB/s)  (GB/s)         
       32768          8192     float     sum      -1  4370.19    0.01    0.01       0  4369.57    0.01    0.01       0
# Out of bounds values : 0 OK
# Avg bus bandwidth    : 0.00749861 
#
# Collective test concluded: all_reduce_perf_mpi
#
```

stderr:
```text
Authorization required, but no authorization protocol specified

Authorization required, but no authorization protocol specified

Authorization required, but no authorization protocol specified

Authorization required, but no authorization protocol specified
```

#### exp3-fine-transition-corrected-default-rep2-40960

Command:
```bash
mpirun --oversubscribe -np 1 /usr/bin/env LD_LIBRARY_PATH=/home/yunwei37/workspace/nccl-eBPF/nccl/build/lib NCCL_TESTS_DEVICE=0 NCCL_P2P_DISABLE=1 NCCL_NET=Socket NCCL_SHM_DISABLE=1 NCCL_HOSTID=exp3-fine-transition-corrected-default-rep2-40960-rank0 /home/yunwei37/workspace/nccl-eBPF/nccl-tests/build/all_reduce_perf_mpi -b 40960 -e 40960 -g 1 -n 100 -w 20 : -np 1 /usr/bin/env LD_LIBRARY_PATH=/home/yunwei37/workspace/nccl-eBPF/nccl/build/lib NCCL_TESTS_DEVICE=0 NCCL_P2P_DISABLE=1 NCCL_NET=Socket NCCL_SHM_DISABLE=1 NCCL_HOSTID=exp3-fine-transition-corrected-default-rep2-40960-rank1 /home/yunwei37/workspace/nccl-eBPF/nccl-tests/build/all_reduce_perf_mpi -b 40960 -e 40960 -g 1 -n 100 -w 20
```

stdout:
```text
# nccl-tests version 2.18.0 nccl-headers=22907 nccl-library=22907
# Collective test starting: all_reduce_perf_mpi
# nThread 1 nGpus 1 minBytes 40960 maxBytes 40960 step: 1048576(bytes) warmup iters: 20 iters: 100 agg iters: 1 validation: 1 graph: 0
#
# Using devices
#  Rank  0 Group  0 Pid 3049916 on        lab device  0 [0000:02:00] NVIDIA GeForce RTX 5090
#  Rank  1 Group  0 Pid 3049917 on        lab device  0 [0000:02:00] NVIDIA GeForce RTX 5090
#
#                                                              out-of-place                       in-place          
#       size         count      type   redop    root     time   algbw   busbw  #wrong     time   algbw   busbw  #wrong 
#        (B)    (elements)                               (us)  (GB/s)  (GB/s)             (us)  (GB/s)  (GB/s)         
       40960         10240     float     sum      -1  4369.72    0.01    0.01       0  4369.67    0.01    0.01       0
# Out of bounds values : 0 OK
# Avg bus bandwidth    : 0.00937365 
#
# Collective test concluded: all_reduce_perf_mpi
#
```

stderr:
```text
Authorization required, but no authorization protocol specified

Authorization required, but no authorization protocol specified

Authorization required, but no authorization protocol specified

Authorization required, but no authorization protocol specified
```

#### exp3-fine-transition-corrected-default-rep2-51200

Command:
```bash
mpirun --oversubscribe -np 1 /usr/bin/env LD_LIBRARY_PATH=/home/yunwei37/workspace/nccl-eBPF/nccl/build/lib NCCL_TESTS_DEVICE=0 NCCL_P2P_DISABLE=1 NCCL_NET=Socket NCCL_SHM_DISABLE=1 NCCL_HOSTID=exp3-fine-transition-corrected-default-rep2-51200-rank0 /home/yunwei37/workspace/nccl-eBPF/nccl-tests/build/all_reduce_perf_mpi -b 51200 -e 51200 -g 1 -n 100 -w 20 : -np 1 /usr/bin/env LD_LIBRARY_PATH=/home/yunwei37/workspace/nccl-eBPF/nccl/build/lib NCCL_TESTS_DEVICE=0 NCCL_P2P_DISABLE=1 NCCL_NET=Socket NCCL_SHM_DISABLE=1 NCCL_HOSTID=exp3-fine-transition-corrected-default-rep2-51200-rank1 /home/yunwei37/workspace/nccl-eBPF/nccl-tests/build/all_reduce_perf_mpi -b 51200 -e 51200 -g 1 -n 100 -w 20
```

stdout:
```text
# nccl-tests version 2.18.0 nccl-headers=22907 nccl-library=22907
# Collective test starting: all_reduce_perf_mpi
# nThread 1 nGpus 1 minBytes 51200 maxBytes 51200 step: 1048576(bytes) warmup iters: 20 iters: 100 agg iters: 1 validation: 1 graph: 0
#
# Using devices
#  Rank  0 Group  0 Pid 3049939 on        lab device  0 [0000:02:00] NVIDIA GeForce RTX 5090
#  Rank  1 Group  0 Pid 3049940 on        lab device  0 [0000:02:00] NVIDIA GeForce RTX 5090
#
#                                                              out-of-place                       in-place          
#       size         count      type   redop    root     time   algbw   busbw  #wrong     time   algbw   busbw  #wrong 
#        (B)    (elements)                               (us)  (GB/s)  (GB/s)             (us)  (GB/s)  (GB/s)         
       51200         12800     float     sum      -1  4370.20    0.01    0.01       0  4369.25    0.01    0.01       0
# Out of bounds values : 0 OK
# Avg bus bandwidth    : 0.011717 
#
# Collective test concluded: all_reduce_perf_mpi
#
```

stderr:
```text
Authorization required, but no authorization protocol specified

Authorization required, but no authorization protocol specified

Authorization required, but no authorization protocol specified

Authorization required, but no authorization protocol specified
```

#### exp3-fine-transition-corrected-default-rep2-64000

Command:
```bash
mpirun --oversubscribe -np 1 /usr/bin/env LD_LIBRARY_PATH=/home/yunwei37/workspace/nccl-eBPF/nccl/build/lib NCCL_TESTS_DEVICE=0 NCCL_P2P_DISABLE=1 NCCL_NET=Socket NCCL_SHM_DISABLE=1 NCCL_HOSTID=exp3-fine-transition-corrected-default-rep2-64000-rank0 /home/yunwei37/workspace/nccl-eBPF/nccl-tests/build/all_reduce_perf_mpi -b 64000 -e 64000 -g 1 -n 100 -w 20 : -np 1 /usr/bin/env LD_LIBRARY_PATH=/home/yunwei37/workspace/nccl-eBPF/nccl/build/lib NCCL_TESTS_DEVICE=0 NCCL_P2P_DISABLE=1 NCCL_NET=Socket NCCL_SHM_DISABLE=1 NCCL_HOSTID=exp3-fine-transition-corrected-default-rep2-64000-rank1 /home/yunwei37/workspace/nccl-eBPF/nccl-tests/build/all_reduce_perf_mpi -b 64000 -e 64000 -g 1 -n 100 -w 20
```

stdout:
```text
# nccl-tests version 2.18.0 nccl-headers=22907 nccl-library=22907
# Collective test starting: all_reduce_perf_mpi
# nThread 1 nGpus 1 minBytes 64000 maxBytes 64000 step: 1048576(bytes) warmup iters: 20 iters: 100 agg iters: 1 validation: 1 graph: 0
#
# Using devices
#  Rank  0 Group  0 Pid 3050061 on        lab device  0 [0000:02:00] NVIDIA GeForce RTX 5090
#  Rank  1 Group  0 Pid 3050062 on        lab device  0 [0000:02:00] NVIDIA GeForce RTX 5090
#
#                                                              out-of-place                       in-place          
#       size         count      type   redop    root     time   algbw   busbw  #wrong     time   algbw   busbw  #wrong 
#        (B)    (elements)                               (us)  (GB/s)  (GB/s)             (us)  (GB/s)  (GB/s)         
       64000         16000     float     sum      -1  9695.95    0.01    0.01       0  8897.82    0.01    0.01       0
# Out of bounds values : 0 OK
# Avg bus bandwidth    : 0.00689673 
#
# Collective test concluded: all_reduce_perf_mpi
#
```

stderr:
```text
Authorization required, but no authorization protocol specified

Authorization required, but no authorization protocol specified

Authorization required, but no authorization protocol specified

Authorization required, but no authorization protocol specified
```

#### exp3-fine-transition-corrected-default-rep2-80000

Command:
```bash
mpirun --oversubscribe -np 1 /usr/bin/env LD_LIBRARY_PATH=/home/yunwei37/workspace/nccl-eBPF/nccl/build/lib NCCL_TESTS_DEVICE=0 NCCL_P2P_DISABLE=1 NCCL_NET=Socket NCCL_SHM_DISABLE=1 NCCL_HOSTID=exp3-fine-transition-corrected-default-rep2-80000-rank0 /home/yunwei37/workspace/nccl-eBPF/nccl-tests/build/all_reduce_perf_mpi -b 80000 -e 80000 -g 1 -n 100 -w 20 : -np 1 /usr/bin/env LD_LIBRARY_PATH=/home/yunwei37/workspace/nccl-eBPF/nccl/build/lib NCCL_TESTS_DEVICE=0 NCCL_P2P_DISABLE=1 NCCL_NET=Socket NCCL_SHM_DISABLE=1 NCCL_HOSTID=exp3-fine-transition-corrected-default-rep2-80000-rank1 /home/yunwei37/workspace/nccl-eBPF/nccl-tests/build/all_reduce_perf_mpi -b 80000 -e 80000 -g 1 -n 100 -w 20
```

stdout:
```text
# nccl-tests version 2.18.0 nccl-headers=22907 nccl-library=22907
# Collective test starting: all_reduce_perf_mpi
# nThread 1 nGpus 1 minBytes 80000 maxBytes 80000 step: 1048576(bytes) warmup iters: 20 iters: 100 agg iters: 1 validation: 1 graph: 0
#
# Using devices
#  Rank  0 Group  0 Pid 3050321 on        lab device  0 [0000:02:00] NVIDIA GeForce RTX 5090
#  Rank  1 Group  0 Pid 3050322 on        lab device  0 [0000:02:00] NVIDIA GeForce RTX 5090
#
#                                                              out-of-place                       in-place          
#       size         count      type   redop    root     time   algbw   busbw  #wrong     time   algbw   busbw  #wrong 
#        (B)    (elements)                               (us)  (GB/s)  (GB/s)             (us)  (GB/s)  (GB/s)         
       80000         20000     float     sum      -1  9234.60    0.01    0.01       0  9002.48    0.01    0.01       0
# Out of bounds values : 0 OK
# Avg bus bandwidth    : 0.00877475 
#
# Collective test concluded: all_reduce_perf_mpi
#
```

stderr:
```text
Authorization required, but no authorization protocol specified

Authorization required, but no authorization protocol specified

Authorization required, but no authorization protocol specified

Authorization required, but no authorization protocol specified
```

#### exp3-fine-transition-corrected-default-rep2-100000

Command:
```bash
mpirun --oversubscribe -np 1 /usr/bin/env LD_LIBRARY_PATH=/home/yunwei37/workspace/nccl-eBPF/nccl/build/lib NCCL_TESTS_DEVICE=0 NCCL_P2P_DISABLE=1 NCCL_NET=Socket NCCL_SHM_DISABLE=1 NCCL_HOSTID=exp3-fine-transition-corrected-default-rep2-100000-rank0 /home/yunwei37/workspace/nccl-eBPF/nccl-tests/build/all_reduce_perf_mpi -b 100000 -e 100000 -g 1 -n 100 -w 20 : -np 1 /usr/bin/env LD_LIBRARY_PATH=/home/yunwei37/workspace/nccl-eBPF/nccl/build/lib NCCL_TESTS_DEVICE=0 NCCL_P2P_DISABLE=1 NCCL_NET=Socket NCCL_SHM_DISABLE=1 NCCL_HOSTID=exp3-fine-transition-corrected-default-rep2-100000-rank1 /home/yunwei37/workspace/nccl-eBPF/nccl-tests/build/all_reduce_perf_mpi -b 100000 -e 100000 -g 1 -n 100 -w 20
```

stdout:
```text
# nccl-tests version 2.18.0 nccl-headers=22907 nccl-library=22907
# Collective test starting: all_reduce_perf_mpi
# nThread 1 nGpus 1 minBytes 100000 maxBytes 100000 step: 1048576(bytes) warmup iters: 20 iters: 100 agg iters: 1 validation: 1 graph: 0
#
# Using devices
#  Rank  0 Group  0 Pid 3050445 on        lab device  0 [0000:02:00] NVIDIA GeForce RTX 5090
#  Rank  1 Group  0 Pid 3050446 on        lab device  0 [0000:02:00] NVIDIA GeForce RTX 5090
#
#                                                              out-of-place                       in-place          
#       size         count      type   redop    root     time   algbw   busbw  #wrong     time   algbw   busbw  #wrong 
#        (B)    (elements)                               (us)  (GB/s)  (GB/s)             (us)  (GB/s)  (GB/s)         
      100000         25000     float     sum      -1  8819.55    0.01    0.01       0  8820.43    0.01    0.01       0
# Out of bounds values : 0 OK
# Avg bus bandwidth    : 0.0113379 
#
# Collective test concluded: all_reduce_perf_mpi
#
```

stderr:
```text
Authorization required, but no authorization protocol specified

Authorization required, but no authorization protocol specified

Authorization required, but no authorization protocol specified

Authorization required, but no authorization protocol specified
```

#### exp3-fine-transition-corrected-default-rep2-125000

Command:
```bash
mpirun --oversubscribe -np 1 /usr/bin/env LD_LIBRARY_PATH=/home/yunwei37/workspace/nccl-eBPF/nccl/build/lib NCCL_TESTS_DEVICE=0 NCCL_P2P_DISABLE=1 NCCL_NET=Socket NCCL_SHM_DISABLE=1 NCCL_HOSTID=exp3-fine-transition-corrected-default-rep2-125000-rank0 /home/yunwei37/workspace/nccl-eBPF/nccl-tests/build/all_reduce_perf_mpi -b 125000 -e 125000 -g 1 -n 100 -w 20 : -np 1 /usr/bin/env LD_LIBRARY_PATH=/home/yunwei37/workspace/nccl-eBPF/nccl/build/lib NCCL_TESTS_DEVICE=0 NCCL_P2P_DISABLE=1 NCCL_NET=Socket NCCL_SHM_DISABLE=1 NCCL_HOSTID=exp3-fine-transition-corrected-default-rep2-125000-rank1 /home/yunwei37/workspace/nccl-eBPF/nccl-tests/build/all_reduce_perf_mpi -b 125000 -e 125000 -g 1 -n 100 -w 20
```

stdout:
```text
# nccl-tests version 2.18.0 nccl-headers=22907 nccl-library=22907
# Collective test starting: all_reduce_perf_mpi
# nThread 1 nGpus 1 minBytes 125000 maxBytes 125000 step: 1048576(bytes) warmup iters: 20 iters: 100 agg iters: 1 validation: 1 graph: 0
#
# Using devices
#  Rank  0 Group  0 Pid 3050587 on        lab device  0 [0000:02:00] NVIDIA GeForce RTX 5090
#  Rank  1 Group  0 Pid 3050588 on        lab device  0 [0000:02:00] NVIDIA GeForce RTX 5090
#
#                                                              out-of-place                       in-place          
#       size         count      type   redop    root     time   algbw   busbw  #wrong     time   algbw   busbw  #wrong 
#        (B)    (elements)                               (us)  (GB/s)  (GB/s)             (us)  (GB/s)  (GB/s)         
      125000         31250     float     sum      -1  5137.15    0.02    0.02       0  4581.55    0.03    0.03       0
# Out of bounds values : 0 OK
# Avg bus bandwidth    : 0.025808 
#
# Collective test concluded: all_reduce_perf_mpi
#
```

stderr:
```text
Authorization required, but no authorization protocol specified

Authorization required, but no authorization protocol specified

Authorization required, but no authorization protocol specified

Authorization required, but no authorization protocol specified
```

#### exp3-fine-transition-corrected-default-rep2-156250

Command:
```bash
mpirun --oversubscribe -np 1 /usr/bin/env LD_LIBRARY_PATH=/home/yunwei37/workspace/nccl-eBPF/nccl/build/lib NCCL_TESTS_DEVICE=0 NCCL_P2P_DISABLE=1 NCCL_NET=Socket NCCL_SHM_DISABLE=1 NCCL_HOSTID=exp3-fine-transition-corrected-default-rep2-156250-rank0 /home/yunwei37/workspace/nccl-eBPF/nccl-tests/build/all_reduce_perf_mpi -b 156250 -e 156250 -g 1 -n 100 -w 20 : -np 1 /usr/bin/env LD_LIBRARY_PATH=/home/yunwei37/workspace/nccl-eBPF/nccl/build/lib NCCL_TESTS_DEVICE=0 NCCL_P2P_DISABLE=1 NCCL_NET=Socket NCCL_SHM_DISABLE=1 NCCL_HOSTID=exp3-fine-transition-corrected-default-rep2-156250-rank1 /home/yunwei37/workspace/nccl-eBPF/nccl-tests/build/all_reduce_perf_mpi -b 156250 -e 156250 -g 1 -n 100 -w 20
```

stdout:
```text
# nccl-tests version 2.18.0 nccl-headers=22907 nccl-library=22907
# Collective test starting: all_reduce_perf_mpi
# nThread 1 nGpus 1 minBytes 156250 maxBytes 156250 step: 1048576(bytes) warmup iters: 20 iters: 100 agg iters: 1 validation: 1 graph: 0
#
# Using devices
#  Rank  0 Group  0 Pid 3050677 on        lab device  0 [0000:02:00] NVIDIA GeForce RTX 5090
#  Rank  1 Group  0 Pid 3050678 on        lab device  0 [0000:02:00] NVIDIA GeForce RTX 5090
#
#                                                              out-of-place                       in-place          
#       size         count      type   redop    root     time   algbw   busbw  #wrong     time   algbw   busbw  #wrong 
#        (B)    (elements)                               (us)  (GB/s)  (GB/s)             (us)  (GB/s)  (GB/s)         
      156248         39062     float     sum      -1  4362.08    0.04    0.04       0  4362.05    0.04    0.04       0
# Out of bounds values : 0 OK
# Avg bus bandwidth    : 0.0358197 
#
# Collective test concluded: all_reduce_perf_mpi
#
```

stderr:
```text
Authorization required, but no authorization protocol specified

Authorization required, but no authorization protocol specified

Authorization required, but no authorization protocol specified

Authorization required, but no authorization protocol specified
```

#### exp3-fine-transition-corrected-default-rep2-195312

Command:
```bash
mpirun --oversubscribe -np 1 /usr/bin/env LD_LIBRARY_PATH=/home/yunwei37/workspace/nccl-eBPF/nccl/build/lib NCCL_TESTS_DEVICE=0 NCCL_P2P_DISABLE=1 NCCL_NET=Socket NCCL_SHM_DISABLE=1 NCCL_HOSTID=exp3-fine-transition-corrected-default-rep2-195312-rank0 /home/yunwei37/workspace/nccl-eBPF/nccl-tests/build/all_reduce_perf_mpi -b 195312 -e 195312 -g 1 -n 100 -w 20 : -np 1 /usr/bin/env LD_LIBRARY_PATH=/home/yunwei37/workspace/nccl-eBPF/nccl/build/lib NCCL_TESTS_DEVICE=0 NCCL_P2P_DISABLE=1 NCCL_NET=Socket NCCL_SHM_DISABLE=1 NCCL_HOSTID=exp3-fine-transition-corrected-default-rep2-195312-rank1 /home/yunwei37/workspace/nccl-eBPF/nccl-tests/build/all_reduce_perf_mpi -b 195312 -e 195312 -g 1 -n 100 -w 20
```

stdout:
```text
# nccl-tests version 2.18.0 nccl-headers=22907 nccl-library=22907
# Collective test starting: all_reduce_perf_mpi
# nThread 1 nGpus 1 minBytes 195312 maxBytes 195312 step: 1048576(bytes) warmup iters: 20 iters: 100 agg iters: 1 validation: 1 graph: 0
#
# Using devices
#  Rank  0 Group  0 Pid 3050754 on        lab device  0 [0000:02:00] NVIDIA GeForce RTX 5090
#  Rank  1 Group  0 Pid 3050755 on        lab device  0 [0000:02:00] NVIDIA GeForce RTX 5090
#
#                                                              out-of-place                       in-place          
#       size         count      type   redop    root     time   algbw   busbw  #wrong     time   algbw   busbw  #wrong 
#        (B)    (elements)                               (us)  (GB/s)  (GB/s)             (us)  (GB/s)  (GB/s)         
      195312         48828     float     sum      -1  4362.52    0.04    0.04       0  4361.79    0.04    0.04       0
# Out of bounds values : 0 OK
# Avg bus bandwidth    : 0.0447742 
#
# Collective test concluded: all_reduce_perf_mpi
#
```

stderr:
```text
Authorization required, but no authorization protocol specified

Authorization required, but no authorization protocol specified

Authorization required, but no authorization protocol specified

Authorization required, but no authorization protocol specified
```

#### exp3-fine-transition-corrected-default-rep2-244140

Command:
```bash
mpirun --oversubscribe -np 1 /usr/bin/env LD_LIBRARY_PATH=/home/yunwei37/workspace/nccl-eBPF/nccl/build/lib NCCL_TESTS_DEVICE=0 NCCL_P2P_DISABLE=1 NCCL_NET=Socket NCCL_SHM_DISABLE=1 NCCL_HOSTID=exp3-fine-transition-corrected-default-rep2-244140-rank0 /home/yunwei37/workspace/nccl-eBPF/nccl-tests/build/all_reduce_perf_mpi -b 244140 -e 244140 -g 1 -n 100 -w 20 : -np 1 /usr/bin/env LD_LIBRARY_PATH=/home/yunwei37/workspace/nccl-eBPF/nccl/build/lib NCCL_TESTS_DEVICE=0 NCCL_P2P_DISABLE=1 NCCL_NET=Socket NCCL_SHM_DISABLE=1 NCCL_HOSTID=exp3-fine-transition-corrected-default-rep2-244140-rank1 /home/yunwei37/workspace/nccl-eBPF/nccl-tests/build/all_reduce_perf_mpi -b 244140 -e 244140 -g 1 -n 100 -w 20
```

stdout:
```text
# nccl-tests version 2.18.0 nccl-headers=22907 nccl-library=22907
# Collective test starting: all_reduce_perf_mpi
# nThread 1 nGpus 1 minBytes 244140 maxBytes 244140 step: 1048576(bytes) warmup iters: 20 iters: 100 agg iters: 1 validation: 1 graph: 0
#
# Using devices
#  Rank  0 Group  0 Pid 3050837 on        lab device  0 [0000:02:00] NVIDIA GeForce RTX 5090
#  Rank  1 Group  0 Pid 3050838 on        lab device  0 [0000:02:00] NVIDIA GeForce RTX 5090
#
#                                                              out-of-place                       in-place          
#       size         count      type   redop    root     time   algbw   busbw  #wrong     time   algbw   busbw  #wrong 
#        (B)    (elements)                               (us)  (GB/s)  (GB/s)             (us)  (GB/s)  (GB/s)         
      244140         61035     float     sum      -1  4362.23    0.06    0.06       0  4362.49    0.06    0.06       0
# Out of bounds values : 0 OK
# Avg bus bandwidth    : 0.0559651 
#
# Collective test concluded: all_reduce_perf_mpi
#
```

stderr:
```text
Authorization required, but no authorization protocol specified

Authorization required, but no authorization protocol specified

Authorization required, but no authorization protocol specified

Authorization required, but no authorization protocol specified
```

#### exp3-fine-transition-corrected-default-rep2-305175

Command:
```bash
mpirun --oversubscribe -np 1 /usr/bin/env LD_LIBRARY_PATH=/home/yunwei37/workspace/nccl-eBPF/nccl/build/lib NCCL_TESTS_DEVICE=0 NCCL_P2P_DISABLE=1 NCCL_NET=Socket NCCL_SHM_DISABLE=1 NCCL_HOSTID=exp3-fine-transition-corrected-default-rep2-305175-rank0 /home/yunwei37/workspace/nccl-eBPF/nccl-tests/build/all_reduce_perf_mpi -b 305175 -e 305175 -g 1 -n 100 -w 20 : -np 1 /usr/bin/env LD_LIBRARY_PATH=/home/yunwei37/workspace/nccl-eBPF/nccl/build/lib NCCL_TESTS_DEVICE=0 NCCL_P2P_DISABLE=1 NCCL_NET=Socket NCCL_SHM_DISABLE=1 NCCL_HOSTID=exp3-fine-transition-corrected-default-rep2-305175-rank1 /home/yunwei37/workspace/nccl-eBPF/nccl-tests/build/all_reduce_perf_mpi -b 305175 -e 305175 -g 1 -n 100 -w 20
```

stdout:
```text
# nccl-tests version 2.18.0 nccl-headers=22907 nccl-library=22907
# Collective test starting: all_reduce_perf_mpi
# nThread 1 nGpus 1 minBytes 305175 maxBytes 305175 step: 1048576(bytes) warmup iters: 20 iters: 100 agg iters: 1 validation: 1 graph: 0
#
# Using devices
#  Rank  0 Group  0 Pid 3050911 on        lab device  0 [0000:02:00] NVIDIA GeForce RTX 5090
#  Rank  1 Group  0 Pid 3050912 on        lab device  0 [0000:02:00] NVIDIA GeForce RTX 5090
#
#                                                              out-of-place                       in-place          
#       size         count      type   redop    root     time   algbw   busbw  #wrong     time   algbw   busbw  #wrong 
#        (B)    (elements)                               (us)  (GB/s)  (GB/s)             (us)  (GB/s)  (GB/s)         
      305172         76293     float     sum      -1  4362.36    0.07    0.07       0  4362.64    0.07    0.07       0
# Out of bounds values : 0 OK
# Avg bus bandwidth    : 0.0699535 
#
# Collective test concluded: all_reduce_perf_mpi
#
```

stderr:
```text
Authorization required, but no authorization protocol specified

Authorization required, but no authorization protocol specified

Authorization required, but no authorization protocol specified

Authorization required, but no authorization protocol specified
```

#### exp3-fine-transition-corrected-default-rep2-381469

Command:
```bash
mpirun --oversubscribe -np 1 /usr/bin/env LD_LIBRARY_PATH=/home/yunwei37/workspace/nccl-eBPF/nccl/build/lib NCCL_TESTS_DEVICE=0 NCCL_P2P_DISABLE=1 NCCL_NET=Socket NCCL_SHM_DISABLE=1 NCCL_HOSTID=exp3-fine-transition-corrected-default-rep2-381469-rank0 /home/yunwei37/workspace/nccl-eBPF/nccl-tests/build/all_reduce_perf_mpi -b 381469 -e 381469 -g 1 -n 100 -w 20 : -np 1 /usr/bin/env LD_LIBRARY_PATH=/home/yunwei37/workspace/nccl-eBPF/nccl/build/lib NCCL_TESTS_DEVICE=0 NCCL_P2P_DISABLE=1 NCCL_NET=Socket NCCL_SHM_DISABLE=1 NCCL_HOSTID=exp3-fine-transition-corrected-default-rep2-381469-rank1 /home/yunwei37/workspace/nccl-eBPF/nccl-tests/build/all_reduce_perf_mpi -b 381469 -e 381469 -g 1 -n 100 -w 20
```

stdout:
```text
# nccl-tests version 2.18.0 nccl-headers=22907 nccl-library=22907
# Collective test starting: all_reduce_perf_mpi
# nThread 1 nGpus 1 minBytes 381469 maxBytes 381469 step: 1048576(bytes) warmup iters: 20 iters: 100 agg iters: 1 validation: 1 graph: 0
#
# Using devices
#  Rank  0 Group  0 Pid 3050992 on        lab device  0 [0000:02:00] NVIDIA GeForce RTX 5090
#  Rank  1 Group  0 Pid 3050993 on        lab device  0 [0000:02:00] NVIDIA GeForce RTX 5090
#
#                                                              out-of-place                       in-place          
#       size         count      type   redop    root     time   algbw   busbw  #wrong     time   algbw   busbw  #wrong 
#        (B)    (elements)                               (us)  (GB/s)  (GB/s)             (us)  (GB/s)  (GB/s)         
      381468         95367     float     sum      -1  4362.89    0.09    0.09       0  4447.79    0.09    0.09       0
# Out of bounds values : 0 OK
# Avg bus bandwidth    : 0.0866002 
#
# Collective test concluded: all_reduce_perf_mpi
#
```

stderr:
```text
Authorization required, but no authorization protocol specified

Authorization required, but no authorization protocol specified

Authorization required, but no authorization protocol specified

Authorization required, but no authorization protocol specified
```

#### exp3-fine-transition-corrected-default-rep2-476836

Command:
```bash
mpirun --oversubscribe -np 1 /usr/bin/env LD_LIBRARY_PATH=/home/yunwei37/workspace/nccl-eBPF/nccl/build/lib NCCL_TESTS_DEVICE=0 NCCL_P2P_DISABLE=1 NCCL_NET=Socket NCCL_SHM_DISABLE=1 NCCL_HOSTID=exp3-fine-transition-corrected-default-rep2-476836-rank0 /home/yunwei37/workspace/nccl-eBPF/nccl-tests/build/all_reduce_perf_mpi -b 476836 -e 476836 -g 1 -n 100 -w 20 : -np 1 /usr/bin/env LD_LIBRARY_PATH=/home/yunwei37/workspace/nccl-eBPF/nccl/build/lib NCCL_TESTS_DEVICE=0 NCCL_P2P_DISABLE=1 NCCL_NET=Socket NCCL_SHM_DISABLE=1 NCCL_HOSTID=exp3-fine-transition-corrected-default-rep2-476836-rank1 /home/yunwei37/workspace/nccl-eBPF/nccl-tests/build/all_reduce_perf_mpi -b 476836 -e 476836 -g 1 -n 100 -w 20
```

stdout:
```text
# nccl-tests version 2.18.0 nccl-headers=22907 nccl-library=22907
# Collective test starting: all_reduce_perf_mpi
# nThread 1 nGpus 1 minBytes 476836 maxBytes 476836 step: 1048576(bytes) warmup iters: 20 iters: 100 agg iters: 1 validation: 1 graph: 0
#
# Using devices
#  Rank  0 Group  0 Pid 3051129 on        lab device  0 [0000:02:00] NVIDIA GeForce RTX 5090
#  Rank  1 Group  0 Pid 3051130 on        lab device  0 [0000:02:00] NVIDIA GeForce RTX 5090
#
#                                                              out-of-place                       in-place          
#       size         count      type   redop    root     time   algbw   busbw  #wrong     time   algbw   busbw  #wrong 
#        (B)    (elements)                               (us)  (GB/s)  (GB/s)             (us)  (GB/s)  (GB/s)         
      476836        119209     float     sum      -1  4362.23    0.11    0.11       0  4361.48    0.11    0.11       0
# Out of bounds values : 0 OK
# Avg bus bandwidth    : 0.109319 
#
# Collective test concluded: all_reduce_perf_mpi
#
```

stderr:
```text
Authorization required, but no authorization protocol specified

Authorization required, but no authorization protocol specified

Authorization required, but no authorization protocol specified

Authorization required, but no authorization protocol specified
```

#### exp3-fine-transition-corrected-simple-rep2-32768

Command:
```bash
mpirun --oversubscribe -np 1 /usr/bin/env LD_LIBRARY_PATH=/home/yunwei37/workspace/nccl-eBPF/nccl/build/lib NCCL_TESTS_DEVICE=0 NCCL_P2P_DISABLE=1 NCCL_NET=Socket NCCL_PROTO=Simple NCCL_SHM_DISABLE=1 NCCL_HOSTID=exp3-fine-transition-corrected-simple-rep2-32768-rank0 /home/yunwei37/workspace/nccl-eBPF/nccl-tests/build/all_reduce_perf_mpi -b 32768 -e 32768 -g 1 -n 100 -w 20 : -np 1 /usr/bin/env LD_LIBRARY_PATH=/home/yunwei37/workspace/nccl-eBPF/nccl/build/lib NCCL_TESTS_DEVICE=0 NCCL_P2P_DISABLE=1 NCCL_NET=Socket NCCL_PROTO=Simple NCCL_SHM_DISABLE=1 NCCL_HOSTID=exp3-fine-transition-corrected-simple-rep2-32768-rank1 /home/yunwei37/workspace/nccl-eBPF/nccl-tests/build/all_reduce_perf_mpi -b 32768 -e 32768 -g 1 -n 100 -w 20
```

stdout:
```text
# nccl-tests version 2.18.0 nccl-headers=22907 nccl-library=22907
# Collective test starting: all_reduce_perf_mpi
# nThread 1 nGpus 1 minBytes 32768 maxBytes 32768 step: 1048576(bytes) warmup iters: 20 iters: 100 agg iters: 1 validation: 1 graph: 0
#
# Using devices
#  Rank  0 Group  0 Pid 3051194 on        lab device  0 [0000:02:00] NVIDIA GeForce RTX 5090
#  Rank  1 Group  0 Pid 3051195 on        lab device  0 [0000:02:00] NVIDIA GeForce RTX 5090
#
#                                                              out-of-place                       in-place          
#       size         count      type   redop    root     time   algbw   busbw  #wrong     time   algbw   busbw  #wrong 
#        (B)    (elements)                               (us)  (GB/s)  (GB/s)             (us)  (GB/s)  (GB/s)         
       32768          8192     float     sum      -1  4358.74    0.01    0.01       0  4358.00    0.01    0.01       0
# Out of bounds values : 0 OK
# Avg bus bandwidth    : 0.00751841 
#
# Collective test concluded: all_reduce_perf_mpi
#
```

stderr:
```text
Authorization required, but no authorization protocol specified

Authorization required, but no authorization protocol specified

Authorization required, but no authorization protocol specified

Authorization required, but no authorization protocol specified
```

#### exp3-fine-transition-corrected-simple-rep2-40960

Command:
```bash
mpirun --oversubscribe -np 1 /usr/bin/env LD_LIBRARY_PATH=/home/yunwei37/workspace/nccl-eBPF/nccl/build/lib NCCL_TESTS_DEVICE=0 NCCL_P2P_DISABLE=1 NCCL_NET=Socket NCCL_PROTO=Simple NCCL_SHM_DISABLE=1 NCCL_HOSTID=exp3-fine-transition-corrected-simple-rep2-40960-rank0 /home/yunwei37/workspace/nccl-eBPF/nccl-tests/build/all_reduce_perf_mpi -b 40960 -e 40960 -g 1 -n 100 -w 20 : -np 1 /usr/bin/env LD_LIBRARY_PATH=/home/yunwei37/workspace/nccl-eBPF/nccl/build/lib NCCL_TESTS_DEVICE=0 NCCL_P2P_DISABLE=1 NCCL_NET=Socket NCCL_PROTO=Simple NCCL_SHM_DISABLE=1 NCCL_HOSTID=exp3-fine-transition-corrected-simple-rep2-40960-rank1 /home/yunwei37/workspace/nccl-eBPF/nccl-tests/build/all_reduce_perf_mpi -b 40960 -e 40960 -g 1 -n 100 -w 20
```

stdout:
```text
# nccl-tests version 2.18.0 nccl-headers=22907 nccl-library=22907
# Collective test starting: all_reduce_perf_mpi
# nThread 1 nGpus 1 minBytes 40960 maxBytes 40960 step: 1048576(bytes) warmup iters: 20 iters: 100 agg iters: 1 validation: 1 graph: 0
#
# Using devices
#  Rank  0 Group  0 Pid 3051270 on        lab device  0 [0000:02:00] NVIDIA GeForce RTX 5090
#  Rank  1 Group  0 Pid 3051271 on        lab device  0 [0000:02:00] NVIDIA GeForce RTX 5090
#
#                                                              out-of-place                       in-place          
#       size         count      type   redop    root     time   algbw   busbw  #wrong     time   algbw   busbw  #wrong 
#        (B)    (elements)                               (us)  (GB/s)  (GB/s)             (us)  (GB/s)  (GB/s)         
       40960         10240     float     sum      -1  4358.65    0.01    0.01       0  4358.21    0.01    0.01       0
# Out of bounds values : 0 OK
# Avg bus bandwidth    : 0.00939789 
#
# Collective test concluded: all_reduce_perf_mpi
#
```

stderr:
```text
Authorization required, but no authorization protocol specified

Authorization required, but no authorization protocol specified

Authorization required, but no authorization protocol specified

Authorization required, but no authorization protocol specified
```

#### exp3-fine-transition-corrected-simple-rep2-51200

Command:
```bash
mpirun --oversubscribe -np 1 /usr/bin/env LD_LIBRARY_PATH=/home/yunwei37/workspace/nccl-eBPF/nccl/build/lib NCCL_TESTS_DEVICE=0 NCCL_P2P_DISABLE=1 NCCL_NET=Socket NCCL_PROTO=Simple NCCL_SHM_DISABLE=1 NCCL_HOSTID=exp3-fine-transition-corrected-simple-rep2-51200-rank0 /home/yunwei37/workspace/nccl-eBPF/nccl-tests/build/all_reduce_perf_mpi -b 51200 -e 51200 -g 1 -n 100 -w 20 : -np 1 /usr/bin/env LD_LIBRARY_PATH=/home/yunwei37/workspace/nccl-eBPF/nccl/build/lib NCCL_TESTS_DEVICE=0 NCCL_P2P_DISABLE=1 NCCL_NET=Socket NCCL_PROTO=Simple NCCL_SHM_DISABLE=1 NCCL_HOSTID=exp3-fine-transition-corrected-simple-rep2-51200-rank1 /home/yunwei37/workspace/nccl-eBPF/nccl-tests/build/all_reduce_perf_mpi -b 51200 -e 51200 -g 1 -n 100 -w 20
```

stdout:
```text
# nccl-tests version 2.18.0 nccl-headers=22907 nccl-library=22907
# Collective test starting: all_reduce_perf_mpi
# nThread 1 nGpus 1 minBytes 51200 maxBytes 51200 step: 1048576(bytes) warmup iters: 20 iters: 100 agg iters: 1 validation: 1 graph: 0
#
# Using devices
#  Rank  0 Group  0 Pid 3051323 on        lab device  0 [0000:02:00] NVIDIA GeForce RTX 5090
#  Rank  1 Group  0 Pid 3051324 on        lab device  0 [0000:02:00] NVIDIA GeForce RTX 5090
#
#                                                              out-of-place                       in-place          
#       size         count      type   redop    root     time   algbw   busbw  #wrong     time   algbw   busbw  #wrong 
#        (B)    (elements)                               (us)  (GB/s)  (GB/s)             (us)  (GB/s)  (GB/s)         
       51200         12800     float     sum      -1  4358.85    0.01    0.01       0  4358.28    0.01    0.01       0
# Out of bounds values : 0 OK
# Avg bus bandwidth    : 0.011747 
#
# Collective test concluded: all_reduce_perf_mpi
#
```

stderr:
```text
Authorization required, but no authorization protocol specified

Authorization required, but no authorization protocol specified

Authorization required, but no authorization protocol specified

Authorization required, but no authorization protocol specified
```

#### exp3-fine-transition-corrected-simple-rep2-64000

Command:
```bash
mpirun --oversubscribe -np 1 /usr/bin/env LD_LIBRARY_PATH=/home/yunwei37/workspace/nccl-eBPF/nccl/build/lib NCCL_TESTS_DEVICE=0 NCCL_P2P_DISABLE=1 NCCL_NET=Socket NCCL_PROTO=Simple NCCL_SHM_DISABLE=1 NCCL_HOSTID=exp3-fine-transition-corrected-simple-rep2-64000-rank0 /home/yunwei37/workspace/nccl-eBPF/nccl-tests/build/all_reduce_perf_mpi -b 64000 -e 64000 -g 1 -n 100 -w 20 : -np 1 /usr/bin/env LD_LIBRARY_PATH=/home/yunwei37/workspace/nccl-eBPF/nccl/build/lib NCCL_TESTS_DEVICE=0 NCCL_P2P_DISABLE=1 NCCL_NET=Socket NCCL_PROTO=Simple NCCL_SHM_DISABLE=1 NCCL_HOSTID=exp3-fine-transition-corrected-simple-rep2-64000-rank1 /home/yunwei37/workspace/nccl-eBPF/nccl-tests/build/all_reduce_perf_mpi -b 64000 -e 64000 -g 1 -n 100 -w 20
```

stdout:
```text
# nccl-tests version 2.18.0 nccl-headers=22907 nccl-library=22907
# Collective test starting: all_reduce_perf_mpi
# nThread 1 nGpus 1 minBytes 64000 maxBytes 64000 step: 1048576(bytes) warmup iters: 20 iters: 100 agg iters: 1 validation: 1 graph: 0
#
# Using devices
#  Rank  0 Group  0 Pid 3051400 on        lab device  0 [0000:02:00] NVIDIA GeForce RTX 5090
#  Rank  1 Group  0 Pid 3051401 on        lab device  0 [0000:02:00] NVIDIA GeForce RTX 5090
#
#                                                              out-of-place                       in-place          
#       size         count      type   redop    root     time   algbw   busbw  #wrong     time   algbw   busbw  #wrong 
#        (B)    (elements)                               (us)  (GB/s)  (GB/s)             (us)  (GB/s)  (GB/s)         
       64000         16000     float     sum      -1  4380.72    0.01    0.01       0  4729.92    0.01    0.01       0
# Out of bounds values : 0 OK
# Avg bus bandwidth    : 0.0140702 
#
# Collective test concluded: all_reduce_perf_mpi
#
```

stderr:
```text
Authorization required, but no authorization protocol specified

Authorization required, but no authorization protocol specified

Authorization required, but no authorization protocol specified

Authorization required, but no authorization protocol specified
```

#### exp3-fine-transition-corrected-simple-rep2-80000

Command:
```bash
mpirun --oversubscribe -np 1 /usr/bin/env LD_LIBRARY_PATH=/home/yunwei37/workspace/nccl-eBPF/nccl/build/lib NCCL_TESTS_DEVICE=0 NCCL_P2P_DISABLE=1 NCCL_NET=Socket NCCL_PROTO=Simple NCCL_SHM_DISABLE=1 NCCL_HOSTID=exp3-fine-transition-corrected-simple-rep2-80000-rank0 /home/yunwei37/workspace/nccl-eBPF/nccl-tests/build/all_reduce_perf_mpi -b 80000 -e 80000 -g 1 -n 100 -w 20 : -np 1 /usr/bin/env LD_LIBRARY_PATH=/home/yunwei37/workspace/nccl-eBPF/nccl/build/lib NCCL_TESTS_DEVICE=0 NCCL_P2P_DISABLE=1 NCCL_NET=Socket NCCL_PROTO=Simple NCCL_SHM_DISABLE=1 NCCL_HOSTID=exp3-fine-transition-corrected-simple-rep2-80000-rank1 /home/yunwei37/workspace/nccl-eBPF/nccl-tests/build/all_reduce_perf_mpi -b 80000 -e 80000 -g 1 -n 100 -w 20
```

stdout:
```text
# nccl-tests version 2.18.0 nccl-headers=22907 nccl-library=22907
# Collective test starting: all_reduce_perf_mpi
# nThread 1 nGpus 1 minBytes 80000 maxBytes 80000 step: 1048576(bytes) warmup iters: 20 iters: 100 agg iters: 1 validation: 1 graph: 0
#
# Using devices
#  Rank  0 Group  0 Pid 3051522 on        lab device  0 [0000:02:00] NVIDIA GeForce RTX 5090
#  Rank  1 Group  0 Pid 3051523 on        lab device  0 [0000:02:00] NVIDIA GeForce RTX 5090
#
#                                                              out-of-place                       in-place          
#       size         count      type   redop    root     time   algbw   busbw  #wrong     time   algbw   busbw  #wrong 
#        (B)    (elements)                               (us)  (GB/s)  (GB/s)             (us)  (GB/s)  (GB/s)         
       80000         20000     float     sum      -1  9001.05    0.01    0.01       0  8693.54    0.01    0.01       0
# Out of bounds values : 0 OK
# Avg bus bandwidth    : 0.00904504 
#
# Collective test concluded: all_reduce_perf_mpi
#
```

stderr:
```text
Authorization required, but no authorization protocol specified

Authorization required, but no authorization protocol specified

Authorization required, but no authorization protocol specified

Authorization required, but no authorization protocol specified
```

#### exp3-fine-transition-corrected-simple-rep2-100000

Command:
```bash
mpirun --oversubscribe -np 1 /usr/bin/env LD_LIBRARY_PATH=/home/yunwei37/workspace/nccl-eBPF/nccl/build/lib NCCL_TESTS_DEVICE=0 NCCL_P2P_DISABLE=1 NCCL_NET=Socket NCCL_PROTO=Simple NCCL_SHM_DISABLE=1 NCCL_HOSTID=exp3-fine-transition-corrected-simple-rep2-100000-rank0 /home/yunwei37/workspace/nccl-eBPF/nccl-tests/build/all_reduce_perf_mpi -b 100000 -e 100000 -g 1 -n 100 -w 20 : -np 1 /usr/bin/env LD_LIBRARY_PATH=/home/yunwei37/workspace/nccl-eBPF/nccl/build/lib NCCL_TESTS_DEVICE=0 NCCL_P2P_DISABLE=1 NCCL_NET=Socket NCCL_PROTO=Simple NCCL_SHM_DISABLE=1 NCCL_HOSTID=exp3-fine-transition-corrected-simple-rep2-100000-rank1 /home/yunwei37/workspace/nccl-eBPF/nccl-tests/build/all_reduce_perf_mpi -b 100000 -e 100000 -g 1 -n 100 -w 20
```

stdout:
```text
# nccl-tests version 2.18.0 nccl-headers=22907 nccl-library=22907
# Collective test starting: all_reduce_perf_mpi
# nThread 1 nGpus 1 minBytes 100000 maxBytes 100000 step: 1048576(bytes) warmup iters: 20 iters: 100 agg iters: 1 validation: 1 graph: 0
#
# Using devices
#  Rank  0 Group  0 Pid 3051782 on        lab device  0 [0000:02:00] NVIDIA GeForce RTX 5090
#  Rank  1 Group  0 Pid 3051783 on        lab device  0 [0000:02:00] NVIDIA GeForce RTX 5090
#
#                                                              out-of-place                       in-place          
#       size         count      type   redop    root     time   algbw   busbw  #wrong     time   algbw   busbw  #wrong 
#        (B)    (elements)                               (us)  (GB/s)  (GB/s)             (us)  (GB/s)  (GB/s)         
      100000         25000     float     sum      -1  9513.71    0.01    0.01       0  9238.48    0.01    0.01       0
# Out of bounds values : 0 OK
# Avg bus bandwidth    : 0.0106677 
#
# Collective test concluded: all_reduce_perf_mpi
#
```

stderr:
```text
Authorization required, but no authorization protocol specified

Authorization required, but no authorization protocol specified

Authorization required, but no authorization protocol specified

Authorization required, but no authorization protocol specified
```

#### exp3-fine-transition-corrected-simple-rep2-125000

Command:
```bash
mpirun --oversubscribe -np 1 /usr/bin/env LD_LIBRARY_PATH=/home/yunwei37/workspace/nccl-eBPF/nccl/build/lib NCCL_TESTS_DEVICE=0 NCCL_P2P_DISABLE=1 NCCL_NET=Socket NCCL_PROTO=Simple NCCL_SHM_DISABLE=1 NCCL_HOSTID=exp3-fine-transition-corrected-simple-rep2-125000-rank0 /home/yunwei37/workspace/nccl-eBPF/nccl-tests/build/all_reduce_perf_mpi -b 125000 -e 125000 -g 1 -n 100 -w 20 : -np 1 /usr/bin/env LD_LIBRARY_PATH=/home/yunwei37/workspace/nccl-eBPF/nccl/build/lib NCCL_TESTS_DEVICE=0 NCCL_P2P_DISABLE=1 NCCL_NET=Socket NCCL_PROTO=Simple NCCL_SHM_DISABLE=1 NCCL_HOSTID=exp3-fine-transition-corrected-simple-rep2-125000-rank1 /home/yunwei37/workspace/nccl-eBPF/nccl-tests/build/all_reduce_perf_mpi -b 125000 -e 125000 -g 1 -n 100 -w 20
```

stdout:
```text
# nccl-tests version 2.18.0 nccl-headers=22907 nccl-library=22907
# Collective test starting: all_reduce_perf_mpi
# nThread 1 nGpus 1 minBytes 125000 maxBytes 125000 step: 1048576(bytes) warmup iters: 20 iters: 100 agg iters: 1 validation: 1 graph: 0
#
# Using devices
#  Rank  0 Group  0 Pid 3052016 on        lab device  0 [0000:02:00] NVIDIA GeForce RTX 5090
#  Rank  1 Group  0 Pid 3052017 on        lab device  0 [0000:02:00] NVIDIA GeForce RTX 5090
#
#                                                              out-of-place                       in-place          
#       size         count      type   redop    root     time   algbw   busbw  #wrong     time   algbw   busbw  #wrong 
#        (B)    (elements)                               (us)  (GB/s)  (GB/s)             (us)  (GB/s)  (GB/s)         
      125000         31250     float     sum      -1  8802.99    0.01    0.01       0  8822.26    0.01    0.01       0
# Out of bounds values : 0 OK
# Avg bus bandwidth    : 0.0141842 
#
# Collective test concluded: all_reduce_perf_mpi
#
```

stderr:
```text
Authorization required, but no authorization protocol specified

Authorization required, but no authorization protocol specified

Authorization required, but no authorization protocol specified

Authorization required, but no authorization protocol specified
```

#### exp3-fine-transition-corrected-simple-rep2-156250

Command:
```bash
mpirun --oversubscribe -np 1 /usr/bin/env LD_LIBRARY_PATH=/home/yunwei37/workspace/nccl-eBPF/nccl/build/lib NCCL_TESTS_DEVICE=0 NCCL_P2P_DISABLE=1 NCCL_NET=Socket NCCL_PROTO=Simple NCCL_SHM_DISABLE=1 NCCL_HOSTID=exp3-fine-transition-corrected-simple-rep2-156250-rank0 /home/yunwei37/workspace/nccl-eBPF/nccl-tests/build/all_reduce_perf_mpi -b 156250 -e 156250 -g 1 -n 100 -w 20 : -np 1 /usr/bin/env LD_LIBRARY_PATH=/home/yunwei37/workspace/nccl-eBPF/nccl/build/lib NCCL_TESTS_DEVICE=0 NCCL_P2P_DISABLE=1 NCCL_NET=Socket NCCL_PROTO=Simple NCCL_SHM_DISABLE=1 NCCL_HOSTID=exp3-fine-transition-corrected-simple-rep2-156250-rank1 /home/yunwei37/workspace/nccl-eBPF/nccl-tests/build/all_reduce_perf_mpi -b 156250 -e 156250 -g 1 -n 100 -w 20
```

stdout:
```text
# nccl-tests version 2.18.0 nccl-headers=22907 nccl-library=22907
# Collective test starting: all_reduce_perf_mpi
# nThread 1 nGpus 1 minBytes 156250 maxBytes 156250 step: 1048576(bytes) warmup iters: 20 iters: 100 agg iters: 1 validation: 1 graph: 0
#
# Using devices
#  Rank  0 Group  0 Pid 3052187 on        lab device  0 [0000:02:00] NVIDIA GeForce RTX 5090
#  Rank  1 Group  0 Pid 3052188 on        lab device  0 [0000:02:00] NVIDIA GeForce RTX 5090
#
#                                                              out-of-place                       in-place          
#       size         count      type   redop    root     time   algbw   busbw  #wrong     time   algbw   busbw  #wrong 
#        (B)    (elements)                               (us)  (GB/s)  (GB/s)             (us)  (GB/s)  (GB/s)         
      156248         39062     float     sum      -1  4579.32    0.03    0.03       0  4774.07    0.03    0.03       0
# Out of bounds values : 0 OK
# Avg bus bandwidth    : 0.0334244 
#
# Collective test concluded: all_reduce_perf_mpi
#
```

stderr:
```text
Authorization required, but no authorization protocol specified

Authorization required, but no authorization protocol specified

Authorization required, but no authorization protocol specified

Authorization required, but no authorization protocol specified
```

#### exp3-fine-transition-corrected-simple-rep2-195312

Command:
```bash
mpirun --oversubscribe -np 1 /usr/bin/env LD_LIBRARY_PATH=/home/yunwei37/workspace/nccl-eBPF/nccl/build/lib NCCL_TESTS_DEVICE=0 NCCL_P2P_DISABLE=1 NCCL_NET=Socket NCCL_PROTO=Simple NCCL_SHM_DISABLE=1 NCCL_HOSTID=exp3-fine-transition-corrected-simple-rep2-195312-rank0 /home/yunwei37/workspace/nccl-eBPF/nccl-tests/build/all_reduce_perf_mpi -b 195312 -e 195312 -g 1 -n 100 -w 20 : -np 1 /usr/bin/env LD_LIBRARY_PATH=/home/yunwei37/workspace/nccl-eBPF/nccl/build/lib NCCL_TESTS_DEVICE=0 NCCL_P2P_DISABLE=1 NCCL_NET=Socket NCCL_PROTO=Simple NCCL_SHM_DISABLE=1 NCCL_HOSTID=exp3-fine-transition-corrected-simple-rep2-195312-rank1 /home/yunwei37/workspace/nccl-eBPF/nccl-tests/build/all_reduce_perf_mpi -b 195312 -e 195312 -g 1 -n 100 -w 20
```

stdout:
```text
# nccl-tests version 2.18.0 nccl-headers=22907 nccl-library=22907
# Collective test starting: all_reduce_perf_mpi
# nThread 1 nGpus 1 minBytes 195312 maxBytes 195312 step: 1048576(bytes) warmup iters: 20 iters: 100 agg iters: 1 validation: 1 graph: 0
#
# Using devices
#  Rank  0 Group  0 Pid 3052332 on        lab device  0 [0000:02:00] NVIDIA GeForce RTX 5090
#  Rank  1 Group  0 Pid 3052333 on        lab device  0 [0000:02:00] NVIDIA GeForce RTX 5090
#
#                                                              out-of-place                       in-place          
#       size         count      type   redop    root     time   algbw   busbw  #wrong     time   algbw   busbw  #wrong 
#        (B)    (elements)                               (us)  (GB/s)  (GB/s)             (us)  (GB/s)  (GB/s)         
      195312         48828     float     sum      -1  9640.81    0.02    0.02       0  9759.55    0.02    0.02       0
# Out of bounds values : 0 OK
# Avg bus bandwidth    : 0.0201356 
#
# Collective test concluded: all_reduce_perf_mpi
#
```

stderr:
```text
Authorization required, but no authorization protocol specified

Authorization required, but no authorization protocol specified

Authorization required, but no authorization protocol specified

Authorization required, but no authorization protocol specified
```

#### exp3-fine-transition-corrected-simple-rep2-244140

Command:
```bash
mpirun --oversubscribe -np 1 /usr/bin/env LD_LIBRARY_PATH=/home/yunwei37/workspace/nccl-eBPF/nccl/build/lib NCCL_TESTS_DEVICE=0 NCCL_P2P_DISABLE=1 NCCL_NET=Socket NCCL_PROTO=Simple NCCL_SHM_DISABLE=1 NCCL_HOSTID=exp3-fine-transition-corrected-simple-rep2-244140-rank0 /home/yunwei37/workspace/nccl-eBPF/nccl-tests/build/all_reduce_perf_mpi -b 244140 -e 244140 -g 1 -n 100 -w 20 : -np 1 /usr/bin/env LD_LIBRARY_PATH=/home/yunwei37/workspace/nccl-eBPF/nccl/build/lib NCCL_TESTS_DEVICE=0 NCCL_P2P_DISABLE=1 NCCL_NET=Socket NCCL_PROTO=Simple NCCL_SHM_DISABLE=1 NCCL_HOSTID=exp3-fine-transition-corrected-simple-rep2-244140-rank1 /home/yunwei37/workspace/nccl-eBPF/nccl-tests/build/all_reduce_perf_mpi -b 244140 -e 244140 -g 1 -n 100 -w 20
```

stdout:
```text
# nccl-tests version 2.18.0 nccl-headers=22907 nccl-library=22907
# Collective test starting: all_reduce_perf_mpi
# nThread 1 nGpus 1 minBytes 244140 maxBytes 244140 step: 1048576(bytes) warmup iters: 20 iters: 100 agg iters: 1 validation: 1 graph: 0
#
# Using devices
#  Rank  0 Group  0 Pid 3052611 on        lab device  0 [0000:02:00] NVIDIA GeForce RTX 5090
#  Rank  1 Group  0 Pid 3052614 on        lab device  0 [0000:02:00] NVIDIA GeForce RTX 5090
#
#                                                              out-of-place                       in-place          
#       size         count      type   redop    root     time   algbw   busbw  #wrong     time   algbw   busbw  #wrong 
#        (B)    (elements)                               (us)  (GB/s)  (GB/s)             (us)  (GB/s)  (GB/s)         
      244140         61035     float     sum      -1  9383.75    0.03    0.03       0  9437.91    0.03    0.03       0
# Out of bounds values : 0 OK
# Avg bus bandwidth    : 0.0259427 
#
# Collective test concluded: all_reduce_perf_mpi
#
```

stderr:
```text
Authorization required, but no authorization protocol specified

Authorization required, but no authorization protocol specified

Authorization required, but no authorization protocol specified

Authorization required, but no authorization protocol specified
```

#### exp3-fine-transition-corrected-simple-rep2-305175

Command:
```bash
mpirun --oversubscribe -np 1 /usr/bin/env LD_LIBRARY_PATH=/home/yunwei37/workspace/nccl-eBPF/nccl/build/lib NCCL_TESTS_DEVICE=0 NCCL_P2P_DISABLE=1 NCCL_NET=Socket NCCL_PROTO=Simple NCCL_SHM_DISABLE=1 NCCL_HOSTID=exp3-fine-transition-corrected-simple-rep2-305175-rank0 /home/yunwei37/workspace/nccl-eBPF/nccl-tests/build/all_reduce_perf_mpi -b 305175 -e 305175 -g 1 -n 100 -w 20 : -np 1 /usr/bin/env LD_LIBRARY_PATH=/home/yunwei37/workspace/nccl-eBPF/nccl/build/lib NCCL_TESTS_DEVICE=0 NCCL_P2P_DISABLE=1 NCCL_NET=Socket NCCL_PROTO=Simple NCCL_SHM_DISABLE=1 NCCL_HOSTID=exp3-fine-transition-corrected-simple-rep2-305175-rank1 /home/yunwei37/workspace/nccl-eBPF/nccl-tests/build/all_reduce_perf_mpi -b 305175 -e 305175 -g 1 -n 100 -w 20
```

stdout:
```text
# nccl-tests version 2.18.0 nccl-headers=22907 nccl-library=22907
# Collective test starting: all_reduce_perf_mpi
# nThread 1 nGpus 1 minBytes 305175 maxBytes 305175 step: 1048576(bytes) warmup iters: 20 iters: 100 agg iters: 1 validation: 1 graph: 0
#
# Using devices
#  Rank  0 Group  0 Pid 3052785 on        lab device  0 [0000:02:00] NVIDIA GeForce RTX 5090
#  Rank  1 Group  0 Pid 3052786 on        lab device  0 [0000:02:00] NVIDIA GeForce RTX 5090
#
#                                                              out-of-place                       in-place          
#       size         count      type   redop    root     time   algbw   busbw  #wrong     time   algbw   busbw  #wrong 
#        (B)    (elements)                               (us)  (GB/s)  (GB/s)             (us)  (GB/s)  (GB/s)         
      305172         76293     float     sum      -1  8797.89    0.03    0.03       0  8818.89    0.03    0.03       0
# Out of bounds values : 0 OK
# Avg bus bandwidth    : 0.0346457 
#
# Collective test concluded: all_reduce_perf_mpi
#
```

stderr:
```text
Authorization required, but no authorization protocol specified

Authorization required, but no authorization protocol specified

Authorization required, but no authorization protocol specified

Authorization required, but no authorization protocol specified
```

#### exp3-fine-transition-corrected-simple-rep2-381469

Command:
```bash
mpirun --oversubscribe -np 1 /usr/bin/env LD_LIBRARY_PATH=/home/yunwei37/workspace/nccl-eBPF/nccl/build/lib NCCL_TESTS_DEVICE=0 NCCL_P2P_DISABLE=1 NCCL_NET=Socket NCCL_PROTO=Simple NCCL_SHM_DISABLE=1 NCCL_HOSTID=exp3-fine-transition-corrected-simple-rep2-381469-rank0 /home/yunwei37/workspace/nccl-eBPF/nccl-tests/build/all_reduce_perf_mpi -b 381469 -e 381469 -g 1 -n 100 -w 20 : -np 1 /usr/bin/env LD_LIBRARY_PATH=/home/yunwei37/workspace/nccl-eBPF/nccl/build/lib NCCL_TESTS_DEVICE=0 NCCL_P2P_DISABLE=1 NCCL_NET=Socket NCCL_PROTO=Simple NCCL_SHM_DISABLE=1 NCCL_HOSTID=exp3-fine-transition-corrected-simple-rep2-381469-rank1 /home/yunwei37/workspace/nccl-eBPF/nccl-tests/build/all_reduce_perf_mpi -b 381469 -e 381469 -g 1 -n 100 -w 20
```

stdout:
```text
# nccl-tests version 2.18.0 nccl-headers=22907 nccl-library=22907
# Collective test starting: all_reduce_perf_mpi
# nThread 1 nGpus 1 minBytes 381469 maxBytes 381469 step: 1048576(bytes) warmup iters: 20 iters: 100 agg iters: 1 validation: 1 graph: 0
#
# Using devices
#  Rank  0 Group  0 Pid 3053030 on        lab device  0 [0000:02:00] NVIDIA GeForce RTX 5090
#  Rank  1 Group  0 Pid 3053031 on        lab device  0 [0000:02:00] NVIDIA GeForce RTX 5090
#
#                                                              out-of-place                       in-place          
#       size         count      type   redop    root     time   algbw   busbw  #wrong     time   algbw   busbw  #wrong 
#        (B)    (elements)                               (us)  (GB/s)  (GB/s)             (us)  (GB/s)  (GB/s)         
      381468         95367     float     sum      -1  4712.77    0.08    0.08       0  4647.12    0.08    0.08       0
# Out of bounds values : 0 OK
# Avg bus bandwidth    : 0.0815152 
#
# Collective test concluded: all_reduce_perf_mpi
#
```

stderr:
```text
Authorization required, but no authorization protocol specified

Authorization required, but no authorization protocol specified

Authorization required, but no authorization protocol specified

Authorization required, but no authorization protocol specified
```

#### exp3-fine-transition-corrected-simple-rep2-476836

Command:
```bash
mpirun --oversubscribe -np 1 /usr/bin/env LD_LIBRARY_PATH=/home/yunwei37/workspace/nccl-eBPF/nccl/build/lib NCCL_TESTS_DEVICE=0 NCCL_P2P_DISABLE=1 NCCL_NET=Socket NCCL_PROTO=Simple NCCL_SHM_DISABLE=1 NCCL_HOSTID=exp3-fine-transition-corrected-simple-rep2-476836-rank0 /home/yunwei37/workspace/nccl-eBPF/nccl-tests/build/all_reduce_perf_mpi -b 476836 -e 476836 -g 1 -n 100 -w 20 : -np 1 /usr/bin/env LD_LIBRARY_PATH=/home/yunwei37/workspace/nccl-eBPF/nccl/build/lib NCCL_TESTS_DEVICE=0 NCCL_P2P_DISABLE=1 NCCL_NET=Socket NCCL_PROTO=Simple NCCL_SHM_DISABLE=1 NCCL_HOSTID=exp3-fine-transition-corrected-simple-rep2-476836-rank1 /home/yunwei37/workspace/nccl-eBPF/nccl-tests/build/all_reduce_perf_mpi -b 476836 -e 476836 -g 1 -n 100 -w 20
```

stdout:
```text
# nccl-tests version 2.18.0 nccl-headers=22907 nccl-library=22907
# Collective test starting: all_reduce_perf_mpi
# nThread 1 nGpus 1 minBytes 476836 maxBytes 476836 step: 1048576(bytes) warmup iters: 20 iters: 100 agg iters: 1 validation: 1 graph: 0
#
# Using devices
#  Rank  0 Group  0 Pid 3053226 on        lab device  0 [0000:02:00] NVIDIA GeForce RTX 5090
#  Rank  1 Group  0 Pid 3053233 on        lab device  0 [0000:02:00] NVIDIA GeForce RTX 5090
#
#                                                              out-of-place                       in-place          
#       size         count      type   redop    root     time   algbw   busbw  #wrong     time   algbw   busbw  #wrong 
#        (B)    (elements)                               (us)  (GB/s)  (GB/s)             (us)  (GB/s)  (GB/s)         
      476836        119209     float     sum      -1  8997.72    0.05    0.05       0  9129.78    0.05    0.05       0
# Out of bounds values : 0 OK
# Avg bus bandwidth    : 0.0526119 
#
# Collective test concluded: all_reduce_perf_mpi
#
```

stderr:
```text
Authorization required, but no authorization protocol specified

Authorization required, but no authorization protocol specified

Authorization required, but no authorization protocol specified

Authorization required, but no authorization protocol specified
```

#### exp3-fine-transition-corrected-default-rep3-32768

Command:
```bash
mpirun --oversubscribe -np 1 /usr/bin/env LD_LIBRARY_PATH=/home/yunwei37/workspace/nccl-eBPF/nccl/build/lib NCCL_TESTS_DEVICE=0 NCCL_P2P_DISABLE=1 NCCL_NET=Socket NCCL_SHM_DISABLE=1 NCCL_HOSTID=exp3-fine-transition-corrected-default-rep3-32768-rank0 /home/yunwei37/workspace/nccl-eBPF/nccl-tests/build/all_reduce_perf_mpi -b 32768 -e 32768 -g 1 -n 100 -w 20 : -np 1 /usr/bin/env LD_LIBRARY_PATH=/home/yunwei37/workspace/nccl-eBPF/nccl/build/lib NCCL_TESTS_DEVICE=0 NCCL_P2P_DISABLE=1 NCCL_NET=Socket NCCL_SHM_DISABLE=1 NCCL_HOSTID=exp3-fine-transition-corrected-default-rep3-32768-rank1 /home/yunwei37/workspace/nccl-eBPF/nccl-tests/build/all_reduce_perf_mpi -b 32768 -e 32768 -g 1 -n 100 -w 20
```

stdout:
```text
# nccl-tests version 2.18.0 nccl-headers=22907 nccl-library=22907
# Collective test starting: all_reduce_perf_mpi
# nThread 1 nGpus 1 minBytes 32768 maxBytes 32768 step: 1048576(bytes) warmup iters: 20 iters: 100 agg iters: 1 validation: 1 graph: 0
#
# Using devices
#  Rank  0 Group  0 Pid 3053410 on        lab device  0 [0000:02:00] NVIDIA GeForce RTX 5090
#  Rank  1 Group  0 Pid 3053411 on        lab device  0 [0000:02:00] NVIDIA GeForce RTX 5090
#
#                                                              out-of-place                       in-place          
#       size         count      type   redop    root     time   algbw   busbw  #wrong     time   algbw   busbw  #wrong 
#        (B)    (elements)                               (us)  (GB/s)  (GB/s)             (us)  (GB/s)  (GB/s)         
       32768          8192     float     sum      -1  9012.15    0.00    0.00       0  8916.82    0.00    0.00       0
# Out of bounds values : 0 OK
# Avg bus bandwidth    : 0.00365542 
#
# Collective test concluded: all_reduce_perf_mpi
#
```

stderr:
```text
Authorization required, but no authorization protocol specified

Authorization required, but no authorization protocol specified

Authorization required, but no authorization protocol specified

Authorization required, but no authorization protocol specified
```

#### exp3-fine-transition-corrected-default-rep3-40960

Command:
```bash
mpirun --oversubscribe -np 1 /usr/bin/env LD_LIBRARY_PATH=/home/yunwei37/workspace/nccl-eBPF/nccl/build/lib NCCL_TESTS_DEVICE=0 NCCL_P2P_DISABLE=1 NCCL_NET=Socket NCCL_SHM_DISABLE=1 NCCL_HOSTID=exp3-fine-transition-corrected-default-rep3-40960-rank0 /home/yunwei37/workspace/nccl-eBPF/nccl-tests/build/all_reduce_perf_mpi -b 40960 -e 40960 -g 1 -n 100 -w 20 : -np 1 /usr/bin/env LD_LIBRARY_PATH=/home/yunwei37/workspace/nccl-eBPF/nccl/build/lib NCCL_TESTS_DEVICE=0 NCCL_P2P_DISABLE=1 NCCL_NET=Socket NCCL_SHM_DISABLE=1 NCCL_HOSTID=exp3-fine-transition-corrected-default-rep3-40960-rank1 /home/yunwei37/workspace/nccl-eBPF/nccl-tests/build/all_reduce_perf_mpi -b 40960 -e 40960 -g 1 -n 100 -w 20
```

stdout:
```text
# nccl-tests version 2.18.0 nccl-headers=22907 nccl-library=22907
# Collective test starting: all_reduce_perf_mpi
# nThread 1 nGpus 1 minBytes 40960 maxBytes 40960 step: 1048576(bytes) warmup iters: 20 iters: 100 agg iters: 1 validation: 1 graph: 0
#
# Using devices
#  Rank  0 Group  0 Pid 3053682 on        lab device  0 [0000:02:00] NVIDIA GeForce RTX 5090
#  Rank  1 Group  0 Pid 3053683 on        lab device  0 [0000:02:00] NVIDIA GeForce RTX 5090
#
#                                                              out-of-place                       in-place          
#       size         count      type   redop    root     time   algbw   busbw  #wrong     time   algbw   busbw  #wrong 
#        (B)    (elements)                               (us)  (GB/s)  (GB/s)             (us)  (GB/s)  (GB/s)         
       40960         10240     float     sum      -1  8811.21    0.00    0.00       0  8866.50    0.00    0.00       0
# Out of bounds values : 0 OK
# Avg bus bandwidth    : 0.00463413 
#
# Collective test concluded: all_reduce_perf_mpi
#
```

stderr:
```text
Authorization required, but no authorization protocol specified

Authorization required, but no authorization protocol specified

Authorization required, but no authorization protocol specified

Authorization required, but no authorization protocol specified
```

#### exp3-fine-transition-corrected-default-rep3-51200

Command:
```bash
mpirun --oversubscribe -np 1 /usr/bin/env LD_LIBRARY_PATH=/home/yunwei37/workspace/nccl-eBPF/nccl/build/lib NCCL_TESTS_DEVICE=0 NCCL_P2P_DISABLE=1 NCCL_NET=Socket NCCL_SHM_DISABLE=1 NCCL_HOSTID=exp3-fine-transition-corrected-default-rep3-51200-rank0 /home/yunwei37/workspace/nccl-eBPF/nccl-tests/build/all_reduce_perf_mpi -b 51200 -e 51200 -g 1 -n 100 -w 20 : -np 1 /usr/bin/env LD_LIBRARY_PATH=/home/yunwei37/workspace/nccl-eBPF/nccl/build/lib NCCL_TESTS_DEVICE=0 NCCL_P2P_DISABLE=1 NCCL_NET=Socket NCCL_SHM_DISABLE=1 NCCL_HOSTID=exp3-fine-transition-corrected-default-rep3-51200-rank1 /home/yunwei37/workspace/nccl-eBPF/nccl-tests/build/all_reduce_perf_mpi -b 51200 -e 51200 -g 1 -n 100 -w 20
```

stdout:
```text
# nccl-tests version 2.18.0 nccl-headers=22907 nccl-library=22907
# Collective test starting: all_reduce_perf_mpi
# nThread 1 nGpus 1 minBytes 51200 maxBytes 51200 step: 1048576(bytes) warmup iters: 20 iters: 100 agg iters: 1 validation: 1 graph: 0
#
# Using devices
#  Rank  0 Group  0 Pid 3053977 on        lab device  0 [0000:02:00] NVIDIA GeForce RTX 5090
#  Rank  1 Group  0 Pid 3053978 on        lab device  0 [0000:02:00] NVIDIA GeForce RTX 5090
#
#                                                              out-of-place                       in-place          
#       size         count      type   redop    root     time   algbw   busbw  #wrong     time   algbw   busbw  #wrong 
#        (B)    (elements)                               (us)  (GB/s)  (GB/s)             (us)  (GB/s)  (GB/s)         
       51200         12800     float     sum      -1  5474.54    0.01    0.01       0  4707.17    0.01    0.01       0
# Out of bounds values : 0 OK
# Avg bus bandwidth    : 0.0101147 
#
# Collective test concluded: all_reduce_perf_mpi
#
```

stderr:
```text
Authorization required, but no authorization protocol specified

Authorization required, but no authorization protocol specified

Authorization required, but no authorization protocol specified

Authorization required, but no authorization protocol specified
```

#### exp3-fine-transition-corrected-default-rep3-64000

Command:
```bash
mpirun --oversubscribe -np 1 /usr/bin/env LD_LIBRARY_PATH=/home/yunwei37/workspace/nccl-eBPF/nccl/build/lib NCCL_TESTS_DEVICE=0 NCCL_P2P_DISABLE=1 NCCL_NET=Socket NCCL_SHM_DISABLE=1 NCCL_HOSTID=exp3-fine-transition-corrected-default-rep3-64000-rank0 /home/yunwei37/workspace/nccl-eBPF/nccl-tests/build/all_reduce_perf_mpi -b 64000 -e 64000 -g 1 -n 100 -w 20 : -np 1 /usr/bin/env LD_LIBRARY_PATH=/home/yunwei37/workspace/nccl-eBPF/nccl/build/lib NCCL_TESTS_DEVICE=0 NCCL_P2P_DISABLE=1 NCCL_NET=Socket NCCL_SHM_DISABLE=1 NCCL_HOSTID=exp3-fine-transition-corrected-default-rep3-64000-rank1 /home/yunwei37/workspace/nccl-eBPF/nccl-tests/build/all_reduce_perf_mpi -b 64000 -e 64000 -g 1 -n 100 -w 20
```

stdout:
```text
# nccl-tests version 2.18.0 nccl-headers=22907 nccl-library=22907
# Collective test starting: all_reduce_perf_mpi
# nThread 1 nGpus 1 minBytes 64000 maxBytes 64000 step: 1048576(bytes) warmup iters: 20 iters: 100 agg iters: 1 validation: 1 graph: 0
#
# Using devices
#  Rank  0 Group  0 Pid 3054118 on        lab device  0 [0000:02:00] NVIDIA GeForce RTX 5090
#  Rank  1 Group  0 Pid 3054119 on        lab device  0 [0000:02:00] NVIDIA GeForce RTX 5090
#
#                                                              out-of-place                       in-place          
#       size         count      type   redop    root     time   algbw   busbw  #wrong     time   algbw   busbw  #wrong 
#        (B)    (elements)                               (us)  (GB/s)  (GB/s)             (us)  (GB/s)  (GB/s)         
       64000         16000     float     sum      -1  8717.20    0.01    0.01       0  8870.42    0.01    0.01       0
# Out of bounds values : 0 OK
# Avg bus bandwidth    : 0.0072784 
#
# Collective test concluded: all_reduce_perf_mpi
#
```

stderr:
```text
Authorization required, but no authorization protocol specified

Authorization required, but no authorization protocol specified

Authorization required, but no authorization protocol specified

Authorization required, but no authorization protocol specified
```

#### exp3-fine-transition-corrected-default-rep3-80000

Command:
```bash
mpirun --oversubscribe -np 1 /usr/bin/env LD_LIBRARY_PATH=/home/yunwei37/workspace/nccl-eBPF/nccl/build/lib NCCL_TESTS_DEVICE=0 NCCL_P2P_DISABLE=1 NCCL_NET=Socket NCCL_SHM_DISABLE=1 NCCL_HOSTID=exp3-fine-transition-corrected-default-rep3-80000-rank0 /home/yunwei37/workspace/nccl-eBPF/nccl-tests/build/all_reduce_perf_mpi -b 80000 -e 80000 -g 1 -n 100 -w 20 : -np 1 /usr/bin/env LD_LIBRARY_PATH=/home/yunwei37/workspace/nccl-eBPF/nccl/build/lib NCCL_TESTS_DEVICE=0 NCCL_P2P_DISABLE=1 NCCL_NET=Socket NCCL_SHM_DISABLE=1 NCCL_HOSTID=exp3-fine-transition-corrected-default-rep3-80000-rank1 /home/yunwei37/workspace/nccl-eBPF/nccl-tests/build/all_reduce_perf_mpi -b 80000 -e 80000 -g 1 -n 100 -w 20
```

stdout:
```text
# nccl-tests version 2.18.0 nccl-headers=22907 nccl-library=22907
# Collective test starting: all_reduce_perf_mpi
# nThread 1 nGpus 1 minBytes 80000 maxBytes 80000 step: 1048576(bytes) warmup iters: 20 iters: 100 agg iters: 1 validation: 1 graph: 0
#
# Using devices
#  Rank  0 Group  0 Pid 3054398 on        lab device  0 [0000:02:00] NVIDIA GeForce RTX 5090
#  Rank  1 Group  0 Pid 3054399 on        lab device  0 [0000:02:00] NVIDIA GeForce RTX 5090
#
#                                                              out-of-place                       in-place          
#       size         count      type   redop    root     time   algbw   busbw  #wrong     time   algbw   busbw  #wrong 
#        (B)    (elements)                               (us)  (GB/s)  (GB/s)             (us)  (GB/s)  (GB/s)         
       80000         20000     float     sum      -1  8921.72    0.01    0.01       0  9174.53    0.01    0.01       0
# Out of bounds values : 0 OK
# Avg bus bandwidth    : 0.00884334 
#
# Collective test concluded: all_reduce_perf_mpi
#
```

stderr:
```text
Authorization required, but no authorization protocol specified

Authorization required, but no authorization protocol specified

Authorization required, but no authorization protocol specified

Authorization required, but no authorization protocol specified
```

#### exp3-fine-transition-corrected-default-rep3-100000

Command:
```bash
mpirun --oversubscribe -np 1 /usr/bin/env LD_LIBRARY_PATH=/home/yunwei37/workspace/nccl-eBPF/nccl/build/lib NCCL_TESTS_DEVICE=0 NCCL_P2P_DISABLE=1 NCCL_NET=Socket NCCL_SHM_DISABLE=1 NCCL_HOSTID=exp3-fine-transition-corrected-default-rep3-100000-rank0 /home/yunwei37/workspace/nccl-eBPF/nccl-tests/build/all_reduce_perf_mpi -b 100000 -e 100000 -g 1 -n 100 -w 20 : -np 1 /usr/bin/env LD_LIBRARY_PATH=/home/yunwei37/workspace/nccl-eBPF/nccl/build/lib NCCL_TESTS_DEVICE=0 NCCL_P2P_DISABLE=1 NCCL_NET=Socket NCCL_SHM_DISABLE=1 NCCL_HOSTID=exp3-fine-transition-corrected-default-rep3-100000-rank1 /home/yunwei37/workspace/nccl-eBPF/nccl-tests/build/all_reduce_perf_mpi -b 100000 -e 100000 -g 1 -n 100 -w 20
```

stdout:
```text
# nccl-tests version 2.18.0 nccl-headers=22907 nccl-library=22907
# Collective test starting: all_reduce_perf_mpi
# nThread 1 nGpus 1 minBytes 100000 maxBytes 100000 step: 1048576(bytes) warmup iters: 20 iters: 100 agg iters: 1 validation: 1 graph: 0
#
# Using devices
#  Rank  0 Group  0 Pid 3054622 on        lab device  0 [0000:02:00] NVIDIA GeForce RTX 5090
#  Rank  1 Group  0 Pid 3054623 on        lab device  0 [0000:02:00] NVIDIA GeForce RTX 5090
#
#                                                              out-of-place                       in-place          
#       size         count      type   redop    root     time   algbw   busbw  #wrong     time   algbw   busbw  #wrong 
#        (B)    (elements)                               (us)  (GB/s)  (GB/s)             (us)  (GB/s)  (GB/s)         
      100000         25000     float     sum      -1  8775.18    0.01    0.01       0  9036.19    0.01    0.01       0
# Out of bounds values : 0 OK
# Avg bus bandwidth    : 0.0112312 
#
# Collective test concluded: all_reduce_perf_mpi
#
```

stderr:
```text
Authorization required, but no authorization protocol specified

Authorization required, but no authorization protocol specified

Authorization required, but no authorization protocol specified

Authorization required, but no authorization protocol specified
```

#### exp3-fine-transition-corrected-default-rep3-125000

Command:
```bash
mpirun --oversubscribe -np 1 /usr/bin/env LD_LIBRARY_PATH=/home/yunwei37/workspace/nccl-eBPF/nccl/build/lib NCCL_TESTS_DEVICE=0 NCCL_P2P_DISABLE=1 NCCL_NET=Socket NCCL_SHM_DISABLE=1 NCCL_HOSTID=exp3-fine-transition-corrected-default-rep3-125000-rank0 /home/yunwei37/workspace/nccl-eBPF/nccl-tests/build/all_reduce_perf_mpi -b 125000 -e 125000 -g 1 -n 100 -w 20 : -np 1 /usr/bin/env LD_LIBRARY_PATH=/home/yunwei37/workspace/nccl-eBPF/nccl/build/lib NCCL_TESTS_DEVICE=0 NCCL_P2P_DISABLE=1 NCCL_NET=Socket NCCL_SHM_DISABLE=1 NCCL_HOSTID=exp3-fine-transition-corrected-default-rep3-125000-rank1 /home/yunwei37/workspace/nccl-eBPF/nccl-tests/build/all_reduce_perf_mpi -b 125000 -e 125000 -g 1 -n 100 -w 20
```

stdout:
```text
# nccl-tests version 2.18.0 nccl-headers=22907 nccl-library=22907
# Collective test starting: all_reduce_perf_mpi
# nThread 1 nGpus 1 minBytes 125000 maxBytes 125000 step: 1048576(bytes) warmup iters: 20 iters: 100 agg iters: 1 validation: 1 graph: 0
#
# Using devices
#  Rank  0 Group  0 Pid 3054802 on        lab device  0 [0000:02:00] NVIDIA GeForce RTX 5090
#  Rank  1 Group  0 Pid 3054803 on        lab device  0 [0000:02:00] NVIDIA GeForce RTX 5090
#
#                                                              out-of-place                       in-place          
#       size         count      type   redop    root     time   algbw   busbw  #wrong     time   algbw   busbw  #wrong 
#        (B)    (elements)                               (us)  (GB/s)  (GB/s)             (us)  (GB/s)  (GB/s)         
      125000         31250     float     sum      -1  4993.93    0.03    0.03       0  4734.69    0.03    0.03       0
# Out of bounds values : 0 OK
# Avg bus bandwidth    : 0.0257156 
#
# Collective test concluded: all_reduce_perf_mpi
#
```

stderr:
```text
Authorization required, but no authorization protocol specified

Authorization required, but no authorization protocol specified

Authorization required, but no authorization protocol specified

Authorization required, but no authorization protocol specified
```

#### exp3-fine-transition-corrected-default-rep3-156250

Command:
```bash
mpirun --oversubscribe -np 1 /usr/bin/env LD_LIBRARY_PATH=/home/yunwei37/workspace/nccl-eBPF/nccl/build/lib NCCL_TESTS_DEVICE=0 NCCL_P2P_DISABLE=1 NCCL_NET=Socket NCCL_SHM_DISABLE=1 NCCL_HOSTID=exp3-fine-transition-corrected-default-rep3-156250-rank0 /home/yunwei37/workspace/nccl-eBPF/nccl-tests/build/all_reduce_perf_mpi -b 156250 -e 156250 -g 1 -n 100 -w 20 : -np 1 /usr/bin/env LD_LIBRARY_PATH=/home/yunwei37/workspace/nccl-eBPF/nccl/build/lib NCCL_TESTS_DEVICE=0 NCCL_P2P_DISABLE=1 NCCL_NET=Socket NCCL_SHM_DISABLE=1 NCCL_HOSTID=exp3-fine-transition-corrected-default-rep3-156250-rank1 /home/yunwei37/workspace/nccl-eBPF/nccl-tests/build/all_reduce_perf_mpi -b 156250 -e 156250 -g 1 -n 100 -w 20
```

stdout:
```text
# nccl-tests version 2.18.0 nccl-headers=22907 nccl-library=22907
# Collective test starting: all_reduce_perf_mpi
# nThread 1 nGpus 1 minBytes 156250 maxBytes 156250 step: 1048576(bytes) warmup iters: 20 iters: 100 agg iters: 1 validation: 1 graph: 0
#
# Using devices
#  Rank  0 Group  0 Pid 3054979 on        lab device  0 [0000:02:00] NVIDIA GeForce RTX 5090
#  Rank  1 Group  0 Pid 3054980 on        lab device  0 [0000:02:00] NVIDIA GeForce RTX 5090
#
#                                                              out-of-place                       in-place          
#       size         count      type   redop    root     time   algbw   busbw  #wrong     time   algbw   busbw  #wrong 
#        (B)    (elements)                               (us)  (GB/s)  (GB/s)             (us)  (GB/s)  (GB/s)         
      156248         39062     float     sum      -1  8985.82    0.02    0.02       0  8794.08    0.02    0.02       0
# Out of bounds values : 0 OK
# Avg bus bandwidth    : 0.0175778 
#
# Collective test concluded: all_reduce_perf_mpi
#
```

stderr:
```text
Authorization required, but no authorization protocol specified

Authorization required, but no authorization protocol specified

Authorization required, but no authorization protocol specified

Authorization required, but no authorization protocol specified
```

#### exp3-fine-transition-corrected-default-rep3-195312

Command:
```bash
mpirun --oversubscribe -np 1 /usr/bin/env LD_LIBRARY_PATH=/home/yunwei37/workspace/nccl-eBPF/nccl/build/lib NCCL_TESTS_DEVICE=0 NCCL_P2P_DISABLE=1 NCCL_NET=Socket NCCL_SHM_DISABLE=1 NCCL_HOSTID=exp3-fine-transition-corrected-default-rep3-195312-rank0 /home/yunwei37/workspace/nccl-eBPF/nccl-tests/build/all_reduce_perf_mpi -b 195312 -e 195312 -g 1 -n 100 -w 20 : -np 1 /usr/bin/env LD_LIBRARY_PATH=/home/yunwei37/workspace/nccl-eBPF/nccl/build/lib NCCL_TESTS_DEVICE=0 NCCL_P2P_DISABLE=1 NCCL_NET=Socket NCCL_SHM_DISABLE=1 NCCL_HOSTID=exp3-fine-transition-corrected-default-rep3-195312-rank1 /home/yunwei37/workspace/nccl-eBPF/nccl-tests/build/all_reduce_perf_mpi -b 195312 -e 195312 -g 1 -n 100 -w 20
```

stdout:
```text
# nccl-tests version 2.18.0 nccl-headers=22907 nccl-library=22907
# Collective test starting: all_reduce_perf_mpi
# nThread 1 nGpus 1 minBytes 195312 maxBytes 195312 step: 1048576(bytes) warmup iters: 20 iters: 100 agg iters: 1 validation: 1 graph: 0
#
# Using devices
#  Rank  0 Group  0 Pid 3055189 on        lab device  0 [0000:02:00] NVIDIA GeForce RTX 5090
#  Rank  1 Group  0 Pid 3055190 on        lab device  0 [0000:02:00] NVIDIA GeForce RTX 5090
#
#                                                              out-of-place                       in-place          
#       size         count      type   redop    root     time   algbw   busbw  #wrong     time   algbw   busbw  #wrong 
#        (B)    (elements)                               (us)  (GB/s)  (GB/s)             (us)  (GB/s)  (GB/s)         
      195312         48828     float     sum      -1  8774.13    0.02    0.02       0  9169.40    0.02    0.02       0
# Out of bounds values : 0 OK
# Avg bus bandwidth    : 0.0217802 
#
# Collective test concluded: all_reduce_perf_mpi
#
```

stderr:
```text
Authorization required, but no authorization protocol specified

Authorization required, but no authorization protocol specified

Authorization required, but no authorization protocol specified

Authorization required, but no authorization protocol specified
```

#### exp3-fine-transition-corrected-default-rep3-244140

Command:
```bash
mpirun --oversubscribe -np 1 /usr/bin/env LD_LIBRARY_PATH=/home/yunwei37/workspace/nccl-eBPF/nccl/build/lib NCCL_TESTS_DEVICE=0 NCCL_P2P_DISABLE=1 NCCL_NET=Socket NCCL_SHM_DISABLE=1 NCCL_HOSTID=exp3-fine-transition-corrected-default-rep3-244140-rank0 /home/yunwei37/workspace/nccl-eBPF/nccl-tests/build/all_reduce_perf_mpi -b 244140 -e 244140 -g 1 -n 100 -w 20 : -np 1 /usr/bin/env LD_LIBRARY_PATH=/home/yunwei37/workspace/nccl-eBPF/nccl/build/lib NCCL_TESTS_DEVICE=0 NCCL_P2P_DISABLE=1 NCCL_NET=Socket NCCL_SHM_DISABLE=1 NCCL_HOSTID=exp3-fine-transition-corrected-default-rep3-244140-rank1 /home/yunwei37/workspace/nccl-eBPF/nccl-tests/build/all_reduce_perf_mpi -b 244140 -e 244140 -g 1 -n 100 -w 20
```

stdout:
```text
# nccl-tests version 2.18.0 nccl-headers=22907 nccl-library=22907
# Collective test starting: all_reduce_perf_mpi
# nThread 1 nGpus 1 minBytes 244140 maxBytes 244140 step: 1048576(bytes) warmup iters: 20 iters: 100 agg iters: 1 validation: 1 graph: 0
#
# Using devices
#  Rank  0 Group  0 Pid 3055387 on        lab device  0 [0000:02:00] NVIDIA GeForce RTX 5090
#  Rank  1 Group  0 Pid 3055388 on        lab device  0 [0000:02:00] NVIDIA GeForce RTX 5090
#
#                                                              out-of-place                       in-place          
#       size         count      type   redop    root     time   algbw   busbw  #wrong     time   algbw   busbw  #wrong 
#        (B)    (elements)                               (us)  (GB/s)  (GB/s)             (us)  (GB/s)  (GB/s)         
      244140         61035     float     sum      -1  8866.55    0.03    0.03       0  9222.53    0.03    0.03       0
# Out of bounds values : 0 OK
# Avg bus bandwidth    : 0.0270035 
#
# Collective test concluded: all_reduce_perf_mpi
#
```

stderr:
```text
Authorization required, but no authorization protocol specified

Authorization required, but no authorization protocol specified

Authorization required, but no authorization protocol specified

Authorization required, but no authorization protocol specified
```

#### exp3-fine-transition-corrected-default-rep3-305175

Command:
```bash
mpirun --oversubscribe -np 1 /usr/bin/env LD_LIBRARY_PATH=/home/yunwei37/workspace/nccl-eBPF/nccl/build/lib NCCL_TESTS_DEVICE=0 NCCL_P2P_DISABLE=1 NCCL_NET=Socket NCCL_SHM_DISABLE=1 NCCL_HOSTID=exp3-fine-transition-corrected-default-rep3-305175-rank0 /home/yunwei37/workspace/nccl-eBPF/nccl-tests/build/all_reduce_perf_mpi -b 305175 -e 305175 -g 1 -n 100 -w 20 : -np 1 /usr/bin/env LD_LIBRARY_PATH=/home/yunwei37/workspace/nccl-eBPF/nccl/build/lib NCCL_TESTS_DEVICE=0 NCCL_P2P_DISABLE=1 NCCL_NET=Socket NCCL_SHM_DISABLE=1 NCCL_HOSTID=exp3-fine-transition-corrected-default-rep3-305175-rank1 /home/yunwei37/workspace/nccl-eBPF/nccl-tests/build/all_reduce_perf_mpi -b 305175 -e 305175 -g 1 -n 100 -w 20
```

stdout:
```text
# nccl-tests version 2.18.0 nccl-headers=22907 nccl-library=22907
# Collective test starting: all_reduce_perf_mpi
# nThread 1 nGpus 1 minBytes 305175 maxBytes 305175 step: 1048576(bytes) warmup iters: 20 iters: 100 agg iters: 1 validation: 1 graph: 0
#
# Using devices
#  Rank  0 Group  0 Pid 3055620 on        lab device  0 [0000:02:00] NVIDIA GeForce RTX 5090
#  Rank  1 Group  0 Pid 3055621 on        lab device  0 [0000:02:00] NVIDIA GeForce RTX 5090
#
#                                                              out-of-place                       in-place          
#       size         count      type   redop    root     time   algbw   busbw  #wrong     time   algbw   busbw  #wrong 
#        (B)    (elements)                               (us)  (GB/s)  (GB/s)             (us)  (GB/s)  (GB/s)         
      305172         76293     float     sum      -1  5578.98    0.05    0.05       0  4611.58    0.07    0.07       0
# Out of bounds values : 0 OK
# Avg bus bandwidth    : 0.0604377 
#
# Collective test concluded: all_reduce_perf_mpi
#
```

stderr:
```text
Authorization required, but no authorization protocol specified

Authorization required, but no authorization protocol specified

Authorization required, but no authorization protocol specified

Authorization required, but no authorization protocol specified
```

#### exp3-fine-transition-corrected-default-rep3-381469

Command:
```bash
mpirun --oversubscribe -np 1 /usr/bin/env LD_LIBRARY_PATH=/home/yunwei37/workspace/nccl-eBPF/nccl/build/lib NCCL_TESTS_DEVICE=0 NCCL_P2P_DISABLE=1 NCCL_NET=Socket NCCL_SHM_DISABLE=1 NCCL_HOSTID=exp3-fine-transition-corrected-default-rep3-381469-rank0 /home/yunwei37/workspace/nccl-eBPF/nccl-tests/build/all_reduce_perf_mpi -b 381469 -e 381469 -g 1 -n 100 -w 20 : -np 1 /usr/bin/env LD_LIBRARY_PATH=/home/yunwei37/workspace/nccl-eBPF/nccl/build/lib NCCL_TESTS_DEVICE=0 NCCL_P2P_DISABLE=1 NCCL_NET=Socket NCCL_SHM_DISABLE=1 NCCL_HOSTID=exp3-fine-transition-corrected-default-rep3-381469-rank1 /home/yunwei37/workspace/nccl-eBPF/nccl-tests/build/all_reduce_perf_mpi -b 381469 -e 381469 -g 1 -n 100 -w 20
```

stdout:
```text
# nccl-tests version 2.18.0 nccl-headers=22907 nccl-library=22907
# Collective test starting: all_reduce_perf_mpi
# nThread 1 nGpus 1 minBytes 381469 maxBytes 381469 step: 1048576(bytes) warmup iters: 20 iters: 100 agg iters: 1 validation: 1 graph: 0
#
# Using devices
#  Rank  0 Group  0 Pid 3055706 on        lab device  0 [0000:02:00] NVIDIA GeForce RTX 5090
#  Rank  1 Group  0 Pid 3055707 on        lab device  0 [0000:02:00] NVIDIA GeForce RTX 5090
#
#                                                              out-of-place                       in-place          
#       size         count      type   redop    root     time   algbw   busbw  #wrong     time   algbw   busbw  #wrong 
#        (B)    (elements)                               (us)  (GB/s)  (GB/s)             (us)  (GB/s)  (GB/s)         
      381468         95367     float     sum      -1  9401.04    0.04    0.04       0  9278.05    0.04    0.04       0
# Out of bounds values : 0 OK
# Avg bus bandwidth    : 0.0408462 
#
# Collective test concluded: all_reduce_perf_mpi
#
```

stderr:
```text
Authorization required, but no authorization protocol specified

Authorization required, but no authorization protocol specified

Authorization required, but no authorization protocol specified

Authorization required, but no authorization protocol specified
```

#### exp3-fine-transition-corrected-default-rep3-476836

Command:
```bash
mpirun --oversubscribe -np 1 /usr/bin/env LD_LIBRARY_PATH=/home/yunwei37/workspace/nccl-eBPF/nccl/build/lib NCCL_TESTS_DEVICE=0 NCCL_P2P_DISABLE=1 NCCL_NET=Socket NCCL_SHM_DISABLE=1 NCCL_HOSTID=exp3-fine-transition-corrected-default-rep3-476836-rank0 /home/yunwei37/workspace/nccl-eBPF/nccl-tests/build/all_reduce_perf_mpi -b 476836 -e 476836 -g 1 -n 100 -w 20 : -np 1 /usr/bin/env LD_LIBRARY_PATH=/home/yunwei37/workspace/nccl-eBPF/nccl/build/lib NCCL_TESTS_DEVICE=0 NCCL_P2P_DISABLE=1 NCCL_NET=Socket NCCL_SHM_DISABLE=1 NCCL_HOSTID=exp3-fine-transition-corrected-default-rep3-476836-rank1 /home/yunwei37/workspace/nccl-eBPF/nccl-tests/build/all_reduce_perf_mpi -b 476836 -e 476836 -g 1 -n 100 -w 20
```

stdout:
```text
# nccl-tests version 2.18.0 nccl-headers=22907 nccl-library=22907
# Collective test starting: all_reduce_perf_mpi
# nThread 1 nGpus 1 minBytes 476836 maxBytes 476836 step: 1048576(bytes) warmup iters: 20 iters: 100 agg iters: 1 validation: 1 graph: 0
#
# Using devices
#  Rank  0 Group  0 Pid 3055962 on        lab device  0 [0000:02:00] NVIDIA GeForce RTX 5090
#  Rank  1 Group  0 Pid 3055963 on        lab device  0 [0000:02:00] NVIDIA GeForce RTX 5090
#
#                                                              out-of-place                       in-place          
#       size         count      type   redop    root     time   algbw   busbw  #wrong     time   algbw   busbw  #wrong 
#        (B)    (elements)                               (us)  (GB/s)  (GB/s)             (us)  (GB/s)  (GB/s)         
      476836        119209     float     sum      -1  9612.04    0.05    0.05       0  9038.72    0.05    0.05       0
# Out of bounds values : 0 OK
# Avg bus bandwidth    : 0.0511815 
#
# Collective test concluded: all_reduce_perf_mpi
#
```

stderr:
```text
Authorization required, but no authorization protocol specified

Authorization required, but no authorization protocol specified

Authorization required, but no authorization protocol specified

Authorization required, but no authorization protocol specified
```

#### exp3-fine-transition-corrected-simple-rep3-32768

Command:
```bash
mpirun --oversubscribe -np 1 /usr/bin/env LD_LIBRARY_PATH=/home/yunwei37/workspace/nccl-eBPF/nccl/build/lib NCCL_TESTS_DEVICE=0 NCCL_P2P_DISABLE=1 NCCL_NET=Socket NCCL_PROTO=Simple NCCL_SHM_DISABLE=1 NCCL_HOSTID=exp3-fine-transition-corrected-simple-rep3-32768-rank0 /home/yunwei37/workspace/nccl-eBPF/nccl-tests/build/all_reduce_perf_mpi -b 32768 -e 32768 -g 1 -n 100 -w 20 : -np 1 /usr/bin/env LD_LIBRARY_PATH=/home/yunwei37/workspace/nccl-eBPF/nccl/build/lib NCCL_TESTS_DEVICE=0 NCCL_P2P_DISABLE=1 NCCL_NET=Socket NCCL_PROTO=Simple NCCL_SHM_DISABLE=1 NCCL_HOSTID=exp3-fine-transition-corrected-simple-rep3-32768-rank1 /home/yunwei37/workspace/nccl-eBPF/nccl-tests/build/all_reduce_perf_mpi -b 32768 -e 32768 -g 1 -n 100 -w 20
```

stdout:
```text
# nccl-tests version 2.18.0 nccl-headers=22907 nccl-library=22907
# Collective test starting: all_reduce_perf_mpi
# nThread 1 nGpus 1 minBytes 32768 maxBytes 32768 step: 1048576(bytes) warmup iters: 20 iters: 100 agg iters: 1 validation: 1 graph: 0
#
# Using devices
#  Rank  0 Group  0 Pid 3056260 on        lab device  0 [0000:02:00] NVIDIA GeForce RTX 5090
#  Rank  1 Group  0 Pid 3056261 on        lab device  0 [0000:02:00] NVIDIA GeForce RTX 5090
#
#                                                              out-of-place                       in-place          
#       size         count      type   redop    root     time   algbw   busbw  #wrong     time   algbw   busbw  #wrong 
#        (B)    (elements)                               (us)  (GB/s)  (GB/s)             (us)  (GB/s)  (GB/s)         
       32768          8192     float     sum      -1  8771.17    0.00    0.00       0  8735.01    0.00    0.00       0
# Out of bounds values : 0 OK
# Avg bus bandwidth    : 0.00374361 
#
# Collective test concluded: all_reduce_perf_mpi
#
```

stderr:
```text
Authorization required, but no authorization protocol specified

Authorization required, but no authorization protocol specified

Authorization required, but no authorization protocol specified

Authorization required, but no authorization protocol specified
```

#### exp3-fine-transition-corrected-simple-rep3-40960

Command:
```bash
mpirun --oversubscribe -np 1 /usr/bin/env LD_LIBRARY_PATH=/home/yunwei37/workspace/nccl-eBPF/nccl/build/lib NCCL_TESTS_DEVICE=0 NCCL_P2P_DISABLE=1 NCCL_NET=Socket NCCL_PROTO=Simple NCCL_SHM_DISABLE=1 NCCL_HOSTID=exp3-fine-transition-corrected-simple-rep3-40960-rank0 /home/yunwei37/workspace/nccl-eBPF/nccl-tests/build/all_reduce_perf_mpi -b 40960 -e 40960 -g 1 -n 100 -w 20 : -np 1 /usr/bin/env LD_LIBRARY_PATH=/home/yunwei37/workspace/nccl-eBPF/nccl/build/lib NCCL_TESTS_DEVICE=0 NCCL_P2P_DISABLE=1 NCCL_NET=Socket NCCL_PROTO=Simple NCCL_SHM_DISABLE=1 NCCL_HOSTID=exp3-fine-transition-corrected-simple-rep3-40960-rank1 /home/yunwei37/workspace/nccl-eBPF/nccl-tests/build/all_reduce_perf_mpi -b 40960 -e 40960 -g 1 -n 100 -w 20
```

stdout:
```text
# nccl-tests version 2.18.0 nccl-headers=22907 nccl-library=22907
# Collective test starting: all_reduce_perf_mpi
# nThread 1 nGpus 1 minBytes 40960 maxBytes 40960 step: 1048576(bytes) warmup iters: 20 iters: 100 agg iters: 1 validation: 1 graph: 0
#
# Using devices
#  Rank  0 Group  0 Pid 3056598 on        lab device  0 [0000:02:00] NVIDIA GeForce RTX 5090
#  Rank  1 Group  0 Pid 3056599 on        lab device  0 [0000:02:00] NVIDIA GeForce RTX 5090
#
#                                                              out-of-place                       in-place          
#       size         count      type   redop    root     time   algbw   busbw  #wrong     time   algbw   busbw  #wrong 
#        (B)    (elements)                               (us)  (GB/s)  (GB/s)             (us)  (GB/s)  (GB/s)         
       40960         10240     float     sum      -1  5205.38    0.01    0.01       0  4358.53    0.01    0.01       0
# Out of bounds values : 0 OK
# Avg bus bandwidth    : 0.00863322 
#
# Collective test concluded: all_reduce_perf_mpi
#
```

stderr:
```text
Authorization required, but no authorization protocol specified

Authorization required, but no authorization protocol specified

Authorization required, but no authorization protocol specified

Authorization required, but no authorization protocol specified
```

#### exp3-fine-transition-corrected-simple-rep3-51200

Command:
```bash
mpirun --oversubscribe -np 1 /usr/bin/env LD_LIBRARY_PATH=/home/yunwei37/workspace/nccl-eBPF/nccl/build/lib NCCL_TESTS_DEVICE=0 NCCL_P2P_DISABLE=1 NCCL_NET=Socket NCCL_PROTO=Simple NCCL_SHM_DISABLE=1 NCCL_HOSTID=exp3-fine-transition-corrected-simple-rep3-51200-rank0 /home/yunwei37/workspace/nccl-eBPF/nccl-tests/build/all_reduce_perf_mpi -b 51200 -e 51200 -g 1 -n 100 -w 20 : -np 1 /usr/bin/env LD_LIBRARY_PATH=/home/yunwei37/workspace/nccl-eBPF/nccl/build/lib NCCL_TESTS_DEVICE=0 NCCL_P2P_DISABLE=1 NCCL_NET=Socket NCCL_PROTO=Simple NCCL_SHM_DISABLE=1 NCCL_HOSTID=exp3-fine-transition-corrected-simple-rep3-51200-rank1 /home/yunwei37/workspace/nccl-eBPF/nccl-tests/build/all_reduce_perf_mpi -b 51200 -e 51200 -g 1 -n 100 -w 20
```

stdout:
```text
# nccl-tests version 2.18.0 nccl-headers=22907 nccl-library=22907
# Collective test starting: all_reduce_perf_mpi
# nThread 1 nGpus 1 minBytes 51200 maxBytes 51200 step: 1048576(bytes) warmup iters: 20 iters: 100 agg iters: 1 validation: 1 graph: 0
#
# Using devices
#  Rank  0 Group  0 Pid 3056724 on        lab device  0 [0000:02:00] NVIDIA GeForce RTX 5090
#  Rank  1 Group  0 Pid 3056725 on        lab device  0 [0000:02:00] NVIDIA GeForce RTX 5090
#
#                                                              out-of-place                       in-place          
#       size         count      type   redop    root     time   algbw   busbw  #wrong     time   algbw   busbw  #wrong 
#        (B)    (elements)                               (us)  (GB/s)  (GB/s)             (us)  (GB/s)  (GB/s)         
       51200         12800     float     sum      -1  4359.07    0.01    0.01       0  4358.83    0.01    0.01       0
# Out of bounds values : 0 OK
# Avg bus bandwidth    : 0.0117459 
#
# Collective test concluded: all_reduce_perf_mpi
#
```

stderr:
```text
Authorization required, but no authorization protocol specified

Authorization required, but no authorization protocol specified

Authorization required, but no authorization protocol specified

Authorization required, but no authorization protocol specified
```

#### exp3-fine-transition-corrected-simple-rep3-64000

Command:
```bash
mpirun --oversubscribe -np 1 /usr/bin/env LD_LIBRARY_PATH=/home/yunwei37/workspace/nccl-eBPF/nccl/build/lib NCCL_TESTS_DEVICE=0 NCCL_P2P_DISABLE=1 NCCL_NET=Socket NCCL_PROTO=Simple NCCL_SHM_DISABLE=1 NCCL_HOSTID=exp3-fine-transition-corrected-simple-rep3-64000-rank0 /home/yunwei37/workspace/nccl-eBPF/nccl-tests/build/all_reduce_perf_mpi -b 64000 -e 64000 -g 1 -n 100 -w 20 : -np 1 /usr/bin/env LD_LIBRARY_PATH=/home/yunwei37/workspace/nccl-eBPF/nccl/build/lib NCCL_TESTS_DEVICE=0 NCCL_P2P_DISABLE=1 NCCL_NET=Socket NCCL_PROTO=Simple NCCL_SHM_DISABLE=1 NCCL_HOSTID=exp3-fine-transition-corrected-simple-rep3-64000-rank1 /home/yunwei37/workspace/nccl-eBPF/nccl-tests/build/all_reduce_perf_mpi -b 64000 -e 64000 -g 1 -n 100 -w 20
```

stdout:
```text
# nccl-tests version 2.18.0 nccl-headers=22907 nccl-library=22907
# Collective test starting: all_reduce_perf_mpi
# nThread 1 nGpus 1 minBytes 64000 maxBytes 64000 step: 1048576(bytes) warmup iters: 20 iters: 100 agg iters: 1 validation: 1 graph: 0
#
# Using devices
#  Rank  0 Group  0 Pid 3056796 on        lab device  0 [0000:02:00] NVIDIA GeForce RTX 5090
#  Rank  1 Group  0 Pid 3056797 on        lab device  0 [0000:02:00] NVIDIA GeForce RTX 5090
#
#                                                              out-of-place                       in-place          
#       size         count      type   redop    root     time   algbw   busbw  #wrong     time   algbw   busbw  #wrong 
#        (B)    (elements)                               (us)  (GB/s)  (GB/s)             (us)  (GB/s)  (GB/s)         
       64000         16000     float     sum      -1  4359.19    0.01    0.01       0  4359.84    0.01    0.01       0
# Out of bounds values : 0 OK
# Avg bus bandwidth    : 0.0146805 
#
# Collective test concluded: all_reduce_perf_mpi
#
```

stderr:
```text
Authorization required, but no authorization protocol specified

Authorization required, but no authorization protocol specified

Authorization required, but no authorization protocol specified

Authorization required, but no authorization protocol specified
```

#### exp3-fine-transition-corrected-simple-rep3-80000

Command:
```bash
mpirun --oversubscribe -np 1 /usr/bin/env LD_LIBRARY_PATH=/home/yunwei37/workspace/nccl-eBPF/nccl/build/lib NCCL_TESTS_DEVICE=0 NCCL_P2P_DISABLE=1 NCCL_NET=Socket NCCL_PROTO=Simple NCCL_SHM_DISABLE=1 NCCL_HOSTID=exp3-fine-transition-corrected-simple-rep3-80000-rank0 /home/yunwei37/workspace/nccl-eBPF/nccl-tests/build/all_reduce_perf_mpi -b 80000 -e 80000 -g 1 -n 100 -w 20 : -np 1 /usr/bin/env LD_LIBRARY_PATH=/home/yunwei37/workspace/nccl-eBPF/nccl/build/lib NCCL_TESTS_DEVICE=0 NCCL_P2P_DISABLE=1 NCCL_NET=Socket NCCL_PROTO=Simple NCCL_SHM_DISABLE=1 NCCL_HOSTID=exp3-fine-transition-corrected-simple-rep3-80000-rank1 /home/yunwei37/workspace/nccl-eBPF/nccl-tests/build/all_reduce_perf_mpi -b 80000 -e 80000 -g 1 -n 100 -w 20
```

stdout:
```text
# nccl-tests version 2.18.0 nccl-headers=22907 nccl-library=22907
# Collective test starting: all_reduce_perf_mpi
# nThread 1 nGpus 1 minBytes 80000 maxBytes 80000 step: 1048576(bytes) warmup iters: 20 iters: 100 agg iters: 1 validation: 1 graph: 0
#
# Using devices
#  Rank  0 Group  0 Pid 3056893 on        lab device  0 [0000:02:00] NVIDIA GeForce RTX 5090
#  Rank  1 Group  0 Pid 3056894 on        lab device  0 [0000:02:00] NVIDIA GeForce RTX 5090
#
#                                                              out-of-place                       in-place          
#       size         count      type   redop    root     time   algbw   busbw  #wrong     time   algbw   busbw  #wrong 
#        (B)    (elements)                               (us)  (GB/s)  (GB/s)             (us)  (GB/s)  (GB/s)         
       80000         20000     float     sum      -1  4359.64    0.02    0.02       0  4359.03    0.02    0.02       0
# Out of bounds values : 0 OK
# Avg bus bandwidth    : 0.0183514 
#
# Collective test concluded: all_reduce_perf_mpi
#
```

stderr:
```text
Authorization required, but no authorization protocol specified

Authorization required, but no authorization protocol specified

Authorization required, but no authorization protocol specified

Authorization required, but no authorization protocol specified
```

#### exp3-fine-transition-corrected-simple-rep3-100000

Command:
```bash
mpirun --oversubscribe -np 1 /usr/bin/env LD_LIBRARY_PATH=/home/yunwei37/workspace/nccl-eBPF/nccl/build/lib NCCL_TESTS_DEVICE=0 NCCL_P2P_DISABLE=1 NCCL_NET=Socket NCCL_PROTO=Simple NCCL_SHM_DISABLE=1 NCCL_HOSTID=exp3-fine-transition-corrected-simple-rep3-100000-rank0 /home/yunwei37/workspace/nccl-eBPF/nccl-tests/build/all_reduce_perf_mpi -b 100000 -e 100000 -g 1 -n 100 -w 20 : -np 1 /usr/bin/env LD_LIBRARY_PATH=/home/yunwei37/workspace/nccl-eBPF/nccl/build/lib NCCL_TESTS_DEVICE=0 NCCL_P2P_DISABLE=1 NCCL_NET=Socket NCCL_PROTO=Simple NCCL_SHM_DISABLE=1 NCCL_HOSTID=exp3-fine-transition-corrected-simple-rep3-100000-rank1 /home/yunwei37/workspace/nccl-eBPF/nccl-tests/build/all_reduce_perf_mpi -b 100000 -e 100000 -g 1 -n 100 -w 20
```

stdout:
```text
# nccl-tests version 2.18.0 nccl-headers=22907 nccl-library=22907
# Collective test starting: all_reduce_perf_mpi
# nThread 1 nGpus 1 minBytes 100000 maxBytes 100000 step: 1048576(bytes) warmup iters: 20 iters: 100 agg iters: 1 validation: 1 graph: 0
#
# Using devices
#  Rank  0 Group  0 Pid 3056968 on        lab device  0 [0000:02:00] NVIDIA GeForce RTX 5090
#  Rank  1 Group  0 Pid 3056969 on        lab device  0 [0000:02:00] NVIDIA GeForce RTX 5090
#
#                                                              out-of-place                       in-place          
#       size         count      type   redop    root     time   algbw   busbw  #wrong     time   algbw   busbw  #wrong 
#        (B)    (elements)                               (us)  (GB/s)  (GB/s)             (us)  (GB/s)  (GB/s)         
      100000         25000     float     sum      -1  4362.00    0.02    0.02       0  4362.15    0.02    0.02       0
# Out of bounds values : 0 OK
# Avg bus bandwidth    : 0.0229249 
#
# Collective test concluded: all_reduce_perf_mpi
#
```

stderr:
```text
Authorization required, but no authorization protocol specified

Authorization required, but no authorization protocol specified

Authorization required, but no authorization protocol specified

Authorization required, but no authorization protocol specified
```

#### exp3-fine-transition-corrected-simple-rep3-125000

Command:
```bash
mpirun --oversubscribe -np 1 /usr/bin/env LD_LIBRARY_PATH=/home/yunwei37/workspace/nccl-eBPF/nccl/build/lib NCCL_TESTS_DEVICE=0 NCCL_P2P_DISABLE=1 NCCL_NET=Socket NCCL_PROTO=Simple NCCL_SHM_DISABLE=1 NCCL_HOSTID=exp3-fine-transition-corrected-simple-rep3-125000-rank0 /home/yunwei37/workspace/nccl-eBPF/nccl-tests/build/all_reduce_perf_mpi -b 125000 -e 125000 -g 1 -n 100 -w 20 : -np 1 /usr/bin/env LD_LIBRARY_PATH=/home/yunwei37/workspace/nccl-eBPF/nccl/build/lib NCCL_TESTS_DEVICE=0 NCCL_P2P_DISABLE=1 NCCL_NET=Socket NCCL_PROTO=Simple NCCL_SHM_DISABLE=1 NCCL_HOSTID=exp3-fine-transition-corrected-simple-rep3-125000-rank1 /home/yunwei37/workspace/nccl-eBPF/nccl-tests/build/all_reduce_perf_mpi -b 125000 -e 125000 -g 1 -n 100 -w 20
```

stdout:
```text
# nccl-tests version 2.18.0 nccl-headers=22907 nccl-library=22907
# Collective test starting: all_reduce_perf_mpi
# nThread 1 nGpus 1 minBytes 125000 maxBytes 125000 step: 1048576(bytes) warmup iters: 20 iters: 100 agg iters: 1 validation: 1 graph: 0
#
# Using devices
#  Rank  0 Group  0 Pid 3057070 on        lab device  0 [0000:02:00] NVIDIA GeForce RTX 5090
#  Rank  1 Group  0 Pid 3057071 on        lab device  0 [0000:02:00] NVIDIA GeForce RTX 5090
#
#                                                              out-of-place                       in-place          
#       size         count      type   redop    root     time   algbw   busbw  #wrong     time   algbw   busbw  #wrong 
#        (B)    (elements)                               (us)  (GB/s)  (GB/s)             (us)  (GB/s)  (GB/s)         
      125000         31250     float     sum      -1  4361.83    0.03    0.03       0  4362.75    0.03    0.03       0
# Out of bounds values : 0 OK
# Avg bus bandwidth    : 0.0286547 
#
# Collective test concluded: all_reduce_perf_mpi
#
```

stderr:
```text
Authorization required, but no authorization protocol specified

Authorization required, but no authorization protocol specified

Authorization required, but no authorization protocol specified

Authorization required, but no authorization protocol specified
```

#### exp3-fine-transition-corrected-simple-rep3-156250

Command:
```bash
mpirun --oversubscribe -np 1 /usr/bin/env LD_LIBRARY_PATH=/home/yunwei37/workspace/nccl-eBPF/nccl/build/lib NCCL_TESTS_DEVICE=0 NCCL_P2P_DISABLE=1 NCCL_NET=Socket NCCL_PROTO=Simple NCCL_SHM_DISABLE=1 NCCL_HOSTID=exp3-fine-transition-corrected-simple-rep3-156250-rank0 /home/yunwei37/workspace/nccl-eBPF/nccl-tests/build/all_reduce_perf_mpi -b 156250 -e 156250 -g 1 -n 100 -w 20 : -np 1 /usr/bin/env LD_LIBRARY_PATH=/home/yunwei37/workspace/nccl-eBPF/nccl/build/lib NCCL_TESTS_DEVICE=0 NCCL_P2P_DISABLE=1 NCCL_NET=Socket NCCL_PROTO=Simple NCCL_SHM_DISABLE=1 NCCL_HOSTID=exp3-fine-transition-corrected-simple-rep3-156250-rank1 /home/yunwei37/workspace/nccl-eBPF/nccl-tests/build/all_reduce_perf_mpi -b 156250 -e 156250 -g 1 -n 100 -w 20
```

stdout:
```text
# nccl-tests version 2.18.0 nccl-headers=22907 nccl-library=22907
# Collective test starting: all_reduce_perf_mpi
# nThread 1 nGpus 1 minBytes 156250 maxBytes 156250 step: 1048576(bytes) warmup iters: 20 iters: 100 agg iters: 1 validation: 1 graph: 0
#
# Using devices
#  Rank  0 Group  0 Pid 3057142 on        lab device  0 [0000:02:00] NVIDIA GeForce RTX 5090
#  Rank  1 Group  0 Pid 3057143 on        lab device  0 [0000:02:00] NVIDIA GeForce RTX 5090
#
#                                                              out-of-place                       in-place          
#       size         count      type   redop    root     time   algbw   busbw  #wrong     time   algbw   busbw  #wrong 
#        (B)    (elements)                               (us)  (GB/s)  (GB/s)             (us)  (GB/s)  (GB/s)         
      156248         39062     float     sum      -1  4363.40    0.04    0.04       0  4362.27    0.04    0.04       0
# Out of bounds values : 0 OK
# Avg bus bandwidth    : 0.0358134 
#
# Collective test concluded: all_reduce_perf_mpi
#
```

stderr:
```text
Authorization required, but no authorization protocol specified

Authorization required, but no authorization protocol specified

Authorization required, but no authorization protocol specified

Authorization required, but no authorization protocol specified
```

#### exp3-fine-transition-corrected-simple-rep3-195312

Command:
```bash
mpirun --oversubscribe -np 1 /usr/bin/env LD_LIBRARY_PATH=/home/yunwei37/workspace/nccl-eBPF/nccl/build/lib NCCL_TESTS_DEVICE=0 NCCL_P2P_DISABLE=1 NCCL_NET=Socket NCCL_PROTO=Simple NCCL_SHM_DISABLE=1 NCCL_HOSTID=exp3-fine-transition-corrected-simple-rep3-195312-rank0 /home/yunwei37/workspace/nccl-eBPF/nccl-tests/build/all_reduce_perf_mpi -b 195312 -e 195312 -g 1 -n 100 -w 20 : -np 1 /usr/bin/env LD_LIBRARY_PATH=/home/yunwei37/workspace/nccl-eBPF/nccl/build/lib NCCL_TESTS_DEVICE=0 NCCL_P2P_DISABLE=1 NCCL_NET=Socket NCCL_PROTO=Simple NCCL_SHM_DISABLE=1 NCCL_HOSTID=exp3-fine-transition-corrected-simple-rep3-195312-rank1 /home/yunwei37/workspace/nccl-eBPF/nccl-tests/build/all_reduce_perf_mpi -b 195312 -e 195312 -g 1 -n 100 -w 20
```

stdout:
```text
# nccl-tests version 2.18.0 nccl-headers=22907 nccl-library=22907
# Collective test starting: all_reduce_perf_mpi
# nThread 1 nGpus 1 minBytes 195312 maxBytes 195312 step: 1048576(bytes) warmup iters: 20 iters: 100 agg iters: 1 validation: 1 graph: 0
#
# Using devices
#  Rank  0 Group  0 Pid 3057214 on        lab device  0 [0000:02:00] NVIDIA GeForce RTX 5090
#  Rank  1 Group  0 Pid 3057215 on        lab device  0 [0000:02:00] NVIDIA GeForce RTX 5090
#
#                                                              out-of-place                       in-place          
#       size         count      type   redop    root     time   algbw   busbw  #wrong     time   algbw   busbw  #wrong 
#        (B)    (elements)                               (us)  (GB/s)  (GB/s)             (us)  (GB/s)  (GB/s)         
      195312         48828     float     sum      -1  4362.70    0.04    0.04       0  4361.68    0.04    0.04       0
# Out of bounds values : 0 OK
# Avg bus bandwidth    : 0.0447738 
#
# Collective test concluded: all_reduce_perf_mpi
#
```

stderr:
```text
Authorization required, but no authorization protocol specified

Authorization required, but no authorization protocol specified

Authorization required, but no authorization protocol specified

Authorization required, but no authorization protocol specified
```

#### exp3-fine-transition-corrected-simple-rep3-244140

Command:
```bash
mpirun --oversubscribe -np 1 /usr/bin/env LD_LIBRARY_PATH=/home/yunwei37/workspace/nccl-eBPF/nccl/build/lib NCCL_TESTS_DEVICE=0 NCCL_P2P_DISABLE=1 NCCL_NET=Socket NCCL_PROTO=Simple NCCL_SHM_DISABLE=1 NCCL_HOSTID=exp3-fine-transition-corrected-simple-rep3-244140-rank0 /home/yunwei37/workspace/nccl-eBPF/nccl-tests/build/all_reduce_perf_mpi -b 244140 -e 244140 -g 1 -n 100 -w 20 : -np 1 /usr/bin/env LD_LIBRARY_PATH=/home/yunwei37/workspace/nccl-eBPF/nccl/build/lib NCCL_TESTS_DEVICE=0 NCCL_P2P_DISABLE=1 NCCL_NET=Socket NCCL_PROTO=Simple NCCL_SHM_DISABLE=1 NCCL_HOSTID=exp3-fine-transition-corrected-simple-rep3-244140-rank1 /home/yunwei37/workspace/nccl-eBPF/nccl-tests/build/all_reduce_perf_mpi -b 244140 -e 244140 -g 1 -n 100 -w 20
```

stdout:
```text
# nccl-tests version 2.18.0 nccl-headers=22907 nccl-library=22907
# Collective test starting: all_reduce_perf_mpi
# nThread 1 nGpus 1 minBytes 244140 maxBytes 244140 step: 1048576(bytes) warmup iters: 20 iters: 100 agg iters: 1 validation: 1 graph: 0
#
# Using devices
#  Rank  0 Group  0 Pid 3057287 on        lab device  0 [0000:02:00] NVIDIA GeForce RTX 5090
#  Rank  1 Group  0 Pid 3057288 on        lab device  0 [0000:02:00] NVIDIA GeForce RTX 5090
#
#                                                              out-of-place                       in-place          
#       size         count      type   redop    root     time   algbw   busbw  #wrong     time   algbw   busbw  #wrong 
#        (B)    (elements)                               (us)  (GB/s)  (GB/s)             (us)  (GB/s)  (GB/s)         
      244140         61035     float     sum      -1  4363.75    0.06    0.06       0  4362.30    0.06    0.06       0
# Out of bounds values : 0 OK
# Avg bus bandwidth    : 0.0559566 
#
# Collective test concluded: all_reduce_perf_mpi
#
```

stderr:
```text
Authorization required, but no authorization protocol specified

Authorization required, but no authorization protocol specified

Authorization required, but no authorization protocol specified

Authorization required, but no authorization protocol specified
```

#### exp3-fine-transition-corrected-simple-rep3-305175

Command:
```bash
mpirun --oversubscribe -np 1 /usr/bin/env LD_LIBRARY_PATH=/home/yunwei37/workspace/nccl-eBPF/nccl/build/lib NCCL_TESTS_DEVICE=0 NCCL_P2P_DISABLE=1 NCCL_NET=Socket NCCL_PROTO=Simple NCCL_SHM_DISABLE=1 NCCL_HOSTID=exp3-fine-transition-corrected-simple-rep3-305175-rank0 /home/yunwei37/workspace/nccl-eBPF/nccl-tests/build/all_reduce_perf_mpi -b 305175 -e 305175 -g 1 -n 100 -w 20 : -np 1 /usr/bin/env LD_LIBRARY_PATH=/home/yunwei37/workspace/nccl-eBPF/nccl/build/lib NCCL_TESTS_DEVICE=0 NCCL_P2P_DISABLE=1 NCCL_NET=Socket NCCL_PROTO=Simple NCCL_SHM_DISABLE=1 NCCL_HOSTID=exp3-fine-transition-corrected-simple-rep3-305175-rank1 /home/yunwei37/workspace/nccl-eBPF/nccl-tests/build/all_reduce_perf_mpi -b 305175 -e 305175 -g 1 -n 100 -w 20
```

stdout:
```text
# nccl-tests version 2.18.0 nccl-headers=22907 nccl-library=22907
# Collective test starting: all_reduce_perf_mpi
# nThread 1 nGpus 1 minBytes 305175 maxBytes 305175 step: 1048576(bytes) warmup iters: 20 iters: 100 agg iters: 1 validation: 1 graph: 0
#
# Using devices
#  Rank  0 Group  0 Pid 3057341 on        lab device  0 [0000:02:00] NVIDIA GeForce RTX 5090
#  Rank  1 Group  0 Pid 3057342 on        lab device  0 [0000:02:00] NVIDIA GeForce RTX 5090
#
#                                                              out-of-place                       in-place          
#       size         count      type   redop    root     time   algbw   busbw  #wrong     time   algbw   busbw  #wrong 
#        (B)    (elements)                               (us)  (GB/s)  (GB/s)             (us)  (GB/s)  (GB/s)         
      305172         76293     float     sum      -1  4362.67    0.07    0.07       0  4363.02    0.07    0.07       0
# Out of bounds values : 0 OK
# Avg bus bandwidth    : 0.0699479 
#
# Collective test concluded: all_reduce_perf_mpi
#
```

stderr:
```text
Authorization required, but no authorization protocol specified

Authorization required, but no authorization protocol specified

Authorization required, but no authorization protocol specified

Authorization required, but no authorization protocol specified
```

#### exp3-fine-transition-corrected-simple-rep3-381469

Command:
```bash
mpirun --oversubscribe -np 1 /usr/bin/env LD_LIBRARY_PATH=/home/yunwei37/workspace/nccl-eBPF/nccl/build/lib NCCL_TESTS_DEVICE=0 NCCL_P2P_DISABLE=1 NCCL_NET=Socket NCCL_PROTO=Simple NCCL_SHM_DISABLE=1 NCCL_HOSTID=exp3-fine-transition-corrected-simple-rep3-381469-rank0 /home/yunwei37/workspace/nccl-eBPF/nccl-tests/build/all_reduce_perf_mpi -b 381469 -e 381469 -g 1 -n 100 -w 20 : -np 1 /usr/bin/env LD_LIBRARY_PATH=/home/yunwei37/workspace/nccl-eBPF/nccl/build/lib NCCL_TESTS_DEVICE=0 NCCL_P2P_DISABLE=1 NCCL_NET=Socket NCCL_PROTO=Simple NCCL_SHM_DISABLE=1 NCCL_HOSTID=exp3-fine-transition-corrected-simple-rep3-381469-rank1 /home/yunwei37/workspace/nccl-eBPF/nccl-tests/build/all_reduce_perf_mpi -b 381469 -e 381469 -g 1 -n 100 -w 20
```

stdout:
```text
# nccl-tests version 2.18.0 nccl-headers=22907 nccl-library=22907
# Collective test starting: all_reduce_perf_mpi
# nThread 1 nGpus 1 minBytes 381469 maxBytes 381469 step: 1048576(bytes) warmup iters: 20 iters: 100 agg iters: 1 validation: 1 graph: 0
#
# Using devices
#  Rank  0 Group  0 Pid 3057449 on        lab device  0 [0000:02:00] NVIDIA GeForce RTX 5090
#  Rank  1 Group  0 Pid 3057450 on        lab device  0 [0000:02:00] NVIDIA GeForce RTX 5090
#
#                                                              out-of-place                       in-place          
#       size         count      type   redop    root     time   algbw   busbw  #wrong     time   algbw   busbw  #wrong 
#        (B)    (elements)                               (us)  (GB/s)  (GB/s)             (us)  (GB/s)  (GB/s)         
      381468         95367     float     sum      -1  4361.83    0.09    0.09       0  4384.41    0.09    0.09       0
# Out of bounds values : 0 OK
# Avg bus bandwidth    : 0.0872307 
#
# Collective test concluded: all_reduce_perf_mpi
#
```

stderr:
```text
Authorization required, but no authorization protocol specified

Authorization required, but no authorization protocol specified

Authorization required, but no authorization protocol specified

Authorization required, but no authorization protocol specified
```

#### exp3-fine-transition-corrected-simple-rep3-476836

Command:
```bash
mpirun --oversubscribe -np 1 /usr/bin/env LD_LIBRARY_PATH=/home/yunwei37/workspace/nccl-eBPF/nccl/build/lib NCCL_TESTS_DEVICE=0 NCCL_P2P_DISABLE=1 NCCL_NET=Socket NCCL_PROTO=Simple NCCL_SHM_DISABLE=1 NCCL_HOSTID=exp3-fine-transition-corrected-simple-rep3-476836-rank0 /home/yunwei37/workspace/nccl-eBPF/nccl-tests/build/all_reduce_perf_mpi -b 476836 -e 476836 -g 1 -n 100 -w 20 : -np 1 /usr/bin/env LD_LIBRARY_PATH=/home/yunwei37/workspace/nccl-eBPF/nccl/build/lib NCCL_TESTS_DEVICE=0 NCCL_P2P_DISABLE=1 NCCL_NET=Socket NCCL_PROTO=Simple NCCL_SHM_DISABLE=1 NCCL_HOSTID=exp3-fine-transition-corrected-simple-rep3-476836-rank1 /home/yunwei37/workspace/nccl-eBPF/nccl-tests/build/all_reduce_perf_mpi -b 476836 -e 476836 -g 1 -n 100 -w 20
```

stdout:
```text
# nccl-tests version 2.18.0 nccl-headers=22907 nccl-library=22907
# Collective test starting: all_reduce_perf_mpi
# nThread 1 nGpus 1 minBytes 476836 maxBytes 476836 step: 1048576(bytes) warmup iters: 20 iters: 100 agg iters: 1 validation: 1 graph: 0
#
# Using devices
#  Rank  0 Group  0 Pid 3057494 on        lab device  0 [0000:02:00] NVIDIA GeForce RTX 5090
#  Rank  1 Group  0 Pid 3057495 on        lab device  0 [0000:02:00] NVIDIA GeForce RTX 5090
#
#                                                              out-of-place                       in-place          
#       size         count      type   redop    root     time   algbw   busbw  #wrong     time   algbw   busbw  #wrong 
#        (B)    (elements)                               (us)  (GB/s)  (GB/s)             (us)  (GB/s)  (GB/s)         
      476836        119209     float     sum      -1  4362.09    0.11    0.11       0  4361.27    0.11    0.11       0
# Out of bounds values : 0 OK
# Avg bus bandwidth    : 0.109324 
#
# Collective test concluded: all_reduce_perf_mpi
#
```

stderr:
```text
Authorization required, but no authorization protocol specified

Authorization required, but no authorization protocol specified

Authorization required, but no authorization protocol specified

Authorization required, but no authorization protocol specified
```


## Overall Conclusion

- Experiment 2: SHM Transport AllReduce: 512B shows DEFAULT genuinely slower than Simple.
- Experiment 2: SHM Transport AllReduce: 2KB shows DEFAULT genuinely slower than Simple.
- Experiment 2: SHM Transport AllReduce: 4KB shows DEFAULT genuinely slower than Simple.
- Experiment 2: SHM Transport AllReduce: 8KB shows DEFAULT genuinely slower than Simple.
- Experiment 2: SHM Transport AllReduce: 16KB shows DEFAULT genuinely slower than Tree+Simple.
- Experiment 2: SHM Transport AllReduce: 64KB shows DEFAULT genuinely slower than Simple.
- Experiment 2: SHM Transport AllReduce: 256KB shows DEFAULT genuinely slower than Simple.

Raw logs directory: `/home/yunwei37/workspace/nccl-eBPF/docs/tmp/default-suboptimal-search-logs`
