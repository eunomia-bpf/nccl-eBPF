# AllGather DEFAULT vs NCCL_PROTO=Simple

- Date: 2026-03-10
- Hardware: 1x NVIDIA GeForce RTX 5090, 2 MPI ranks sharing GPU 0
- Transport: `NCCL_NET=Socket`, `NCCL_P2P_DISABLE=1`, `NCCL_SHM_DISABLE=1`
- Workload: `all_gather_perf_mpi -b 1048576 -e 134217728 -f 2 -g 1 -n 50 -w 10`
- Metric below: out-of-place `algbw` from `nccl-tests`, 3 reps per config, std = sample standard deviation

## Summary

| Size | DEFAULT reps (GB/s) | DEFAULT mean +- std | Simple reps (GB/s) | Simple mean +- std | % diff (Simple vs DEFAULT) |
| --- | --- | --- | --- | --- | --- |
| 8 MiB | 3.64, 1.10, 3.70 | 2.81 +- 1.48 | 3.71, 3.63, 3.46 | 3.60 +- 0.13 | +28.0% |
| 16 MiB | 4.51, 1.97, 4.44 | 3.64 +- 1.45 | 4.54, 4.37, 4.42 | 4.44 +- 0.09 | +22.1% |
| 32 MiB | 4.40, 1.75, 4.50 | 3.55 +- 1.56 | 4.44, 4.47, 4.44 | 4.45 +- 0.02 | +25.4% |
| 64 MiB | 2.86, 2.04, 4.58 | 3.16 +- 1.30 | 4.58, 4.51, 4.51 | 4.53 +- 0.04 | +43.5% |
| 128 MiB | 2.08, 2.11, 4.61 | 2.93 +- 1.45 | 4.62, 4.62, 4.63 | 4.62 +- 0.01 | +57.6% |

## Raw Output

### Config A: AllGather DEFAULT

```text
=== AllGather DEFAULT rep 1 ===
Authorization required, but no authorization protocol specified

Authorization required, but no authorization protocol specified

Authorization required, but no authorization protocol specified

Authorization required, but no authorization protocol specified

# nccl-tests version 2.18.0 nccl-headers=22907 nccl-library=22907
# Collective test starting: all_gather_perf_mpi
# nThread 1 nGpus 1 minBytes 1048576 maxBytes 134217728 step: 2(factor) warmup iters: 10 iters: 50 agg iters: 1 validation: 1 graph: 0
#
# Using devices
#  Rank  0 Group  0 Pid 2989375 on        lab device  0 [0000:02:00] NVIDIA GeForce RTX 5090
#  Rank  1 Group  0 Pid 2989376 on        lab device  0 [0000:02:00] NVIDIA GeForce RTX 5090
#
#                                                              out-of-place                       in-place
#       size         count      type   redop    root     time   algbw   busbw  #wrong     time   algbw   busbw  #wrong
#        (B)    (elements)                               (us)  (GB/s)  (GB/s)             (us)  (GB/s)  (GB/s)
     1048576        131072     float    none      -1  2189.16    0.48    0.24       0  2190.15    0.48    0.24       0
     2097152        262144     float    none      -1  2193.39    0.96    0.48       0  2193.91    0.96    0.48       0
     4194304        524288     float    none      -1  2206.04    1.90    0.95       0  2202.74    1.90    0.95       0
     8388608       1048576     float    none      -1  2302.13    3.64    1.82       0  2355.57    3.56    1.78       0
    16777216       2097152     float    none      -1  3723.35    4.51    2.25       0  3746.57    4.48    2.24       0
    33554432       4194304     float    none      -1  7620.22    4.40    2.20       0  9302.84    3.61    1.80       0
    67108864       8388608     float    none      -1  23431.8    2.86    1.43       0  23288.1    2.88    1.44       0
   134217728      16777216     float    none      -1  64380.9    2.08    1.04       0  61013.2    2.20    1.10       0
# Out of bounds values : 0 OK
# Avg bus bandwidth    : 1.27827
#
# Collective test concluded: all_gather_perf_mpi
#

=== AllGather DEFAULT rep 2 ===
Authorization required, but no authorization protocol specified

Authorization required, but no authorization protocol specified

Authorization required, but no authorization protocol specified

Authorization required, but no authorization protocol specified

# nccl-tests version 2.18.0 nccl-headers=22907 nccl-library=22907
# Collective test starting: all_gather_perf_mpi
# nThread 1 nGpus 1 minBytes 1048576 maxBytes 134217728 step: 2(factor) warmup iters: 10 iters: 50 agg iters: 1 validation: 1 graph: 0
#
# Using devices
#  Rank  0 Group  0 Pid 2989695 on        lab device  0 [0000:02:00] NVIDIA GeForce RTX 5090
#  Rank  1 Group  0 Pid 2989696 on        lab device  0 [0000:02:00] NVIDIA GeForce RTX 5090
#
#                                                              out-of-place                       in-place
#       size         count      type   redop    root     time   algbw   busbw  #wrong     time   algbw   busbw  #wrong
#        (B)    (elements)                               (us)  (GB/s)  (GB/s)             (us)  (GB/s)  (GB/s)
     1048576        131072     float    none      -1  4468.53    0.23    0.12       0  4528.44    0.23    0.12       0
     2097152        262144     float    none      -1  4535.01    0.46    0.23       0  4551.84    0.46    0.23       0
     4194304        524288     float    none      -1  4556.40    0.92    0.46       0  4999.08    0.84    0.42       0
     8388608       1048576     float    none      -1  7647.96    1.10    0.55       0  6564.99    1.28    0.64       0
    16777216       2097152     float    none      -1  8498.30    1.97    0.99       0  8647.04    1.94    0.97       0
    33554432       4194304     float    none      -1  19200.7    1.75    0.87       0  17105.4    1.96    0.98       0
    67108864       8388608     float    none      -1  32882.7    2.04    1.02       0  33320.4    2.01    1.01       0
   134217728      16777216     float    none      -1  63473.4    2.11    1.06       0  63517.6    2.11    1.06       0
# Out of bounds values : 0 OK
# Avg bus bandwidth    : 0.669677
#
# Collective test concluded: all_gather_perf_mpi
#

=== AllGather DEFAULT rep 3 ===
Authorization required, but no authorization protocol specified

Authorization required, but no authorization protocol specified

Authorization required, but no authorization protocol specified

Authorization required, but no authorization protocol specified

# nccl-tests version 2.18.0 nccl-headers=22907 nccl-library=22907
# Collective test starting: all_gather_perf_mpi
# nThread 1 nGpus 1 minBytes 1048576 maxBytes 134217728 step: 2(factor) warmup iters: 10 iters: 50 agg iters: 1 validation: 1 graph: 0
#
# Using devices
#  Rank  0 Group  0 Pid 2989945 on        lab device  0 [0000:02:00] NVIDIA GeForce RTX 5090
#  Rank  1 Group  0 Pid 2989946 on        lab device  0 [0000:02:00] NVIDIA GeForce RTX 5090
#
#                                                              out-of-place                       in-place
#       size         count      type   redop    root     time   algbw   busbw  #wrong     time   algbw   busbw  #wrong
#        (B)    (elements)                               (us)  (GB/s)  (GB/s)             (us)  (GB/s)  (GB/s)
     1048576        131072     float    none      -1  2188.98    0.48    0.24       0  2240.32    0.47    0.23       0
     2097152        262144     float    none      -1  2191.56    0.96    0.48       0  2195.27    0.96    0.48       0
     4194304        524288     float    none      -1  2195.83    1.91    0.96       0  2203.80    1.90    0.95       0
     8388608       1048576     float    none      -1  2266.88    3.70    1.85       0  2264.54    3.70    1.85       0
    16777216       2097152     float    none      -1  3779.39    4.44    2.22       0  4767.02    3.52    1.76       0
    33554432       4194304     float    none      -1  7449.51    4.50    2.25       0  7304.67    4.59    2.30       0
    67108864       8388608     float    none      -1  14637.1    4.58    2.29       0  14733.8    4.55    2.28       0
   134217728      16777216     float    none      -1  29133.5    4.61    2.30       0  29216.0    4.59    2.30       0
# Out of bounds values : 0 OK
# Avg bus bandwidth    : 1.54607
#
# Collective test concluded: all_gather_perf_mpi
#
```

### Config B: AllGather NCCL_PROTO=Simple

```text
=== AllGather NCCL_PROTO=Simple rep 1 ===
Authorization required, but no authorization protocol specified

Authorization required, but no authorization protocol specified

Authorization required, but no authorization protocol specified

Authorization required, but no authorization protocol specified

# nccl-tests version 2.18.0 nccl-headers=22907 nccl-library=22907
# Collective test starting: all_gather_perf_mpi
# nThread 1 nGpus 1 minBytes 1048576 maxBytes 134217728 step: 2(factor) warmup iters: 10 iters: 50 agg iters: 1 validation: 1 graph: 0
#
# Using devices
#  Rank  0 Group  0 Pid 2990750 on        lab device  0 [0000:02:00] NVIDIA GeForce RTX 5090
#  Rank  1 Group  0 Pid 2990751 on        lab device  0 [0000:02:00] NVIDIA GeForce RTX 5090
#
#                                                              out-of-place                       in-place
#       size         count      type   redop    root     time   algbw   busbw  #wrong     time   algbw   busbw  #wrong
#        (B)    (elements)                               (us)  (GB/s)  (GB/s)             (us)  (GB/s)  (GB/s)
     1048576        131072     float    none      -1  2189.50    0.48    0.24       0  2188.24    0.48    0.24       0
     2097152        262144     float    none      -1  2191.84    0.96    0.48       0  2194.81    0.96    0.48       0
     4194304        524288     float    none      -1  2196.05    1.91    0.95       0  2196.22    1.91    0.95       0
     8388608       1048576     float    none      -1  2263.20    3.71    1.85       0  2355.58    3.56    1.78       0
    16777216       2097152     float    none      -1  3698.28    4.54    2.27       0  3784.94    4.43    2.22       0
    33554432       4194304     float    none      -1  7556.69    4.44    2.22       0  7590.51    4.42    2.21       0
    67108864       8388608     float    none      -1  14638.4    4.58    2.29       0  14699.4    4.57    2.28       0
   134217728      16777216     float    none      -1  29028.7    4.62    2.31       0  28979.7    4.63    2.32       0
# Out of bounds values : 0 OK
# Avg bus bandwidth    : 1.56852
#
# Collective test concluded: all_gather_perf_mpi
#

=== AllGather NCCL_PROTO=Simple rep 2 ===
Authorization required, but no authorization protocol specified

Authorization required, but no authorization protocol specified

Authorization required, but no authorization protocol specified

Authorization required, but no authorization protocol specified

# nccl-tests version 2.18.0 nccl-headers=22907 nccl-library=22907
# Collective test starting: all_gather_perf_mpi
# nThread 1 nGpus 1 minBytes 1048576 maxBytes 134217728 step: 2(factor) warmup iters: 10 iters: 50 agg iters: 1 validation: 1 graph: 0
#
# Using devices
#  Rank  0 Group  0 Pid 2991014 on        lab device  0 [0000:02:00] NVIDIA GeForce RTX 5090
#  Rank  1 Group  0 Pid 2991015 on        lab device  0 [0000:02:00] NVIDIA GeForce RTX 5090
#
#                                                              out-of-place                       in-place
#       size         count      type   redop    root     time   algbw   busbw  #wrong     time   algbw   busbw  #wrong
#        (B)    (elements)                               (us)  (GB/s)  (GB/s)             (us)  (GB/s)  (GB/s)
     1048576        131072     float    none      -1  2188.86    0.48    0.24       0  2185.92    0.48    0.24       0
     2097152        262144     float    none      -1  2189.71    0.96    0.48       0  2196.03    0.95    0.48       0
     4194304        524288     float    none      -1  2204.90    1.90    0.95       0  2214.53    1.89    0.95       0
     8388608       1048576     float    none      -1  2312.97    3.63    1.81       0  2382.83    3.52    1.76       0
    16777216       2097152     float    none      -1  3834.83    4.37    2.19       0  3798.98    4.42    2.21       0
    33554432       4194304     float    none      -1  7506.77    4.47    2.23       0  7490.53    4.48    2.24       0
    67108864       8388608     float    none      -1  14867.4    4.51    2.26       0  14673.5    4.57    2.29       0
   134217728      16777216     float    none      -1  29078.6    4.62    2.31       0  29581.5    4.54    2.27       0
# Out of bounds values : 0 OK
# Avg bus bandwidth    : 1.55612
#
# Collective test concluded: all_gather_perf_mpi
#

=== AllGather NCCL_PROTO=Simple rep 3 ===
Authorization required, but no authorization protocol specified

Authorization required, but no authorization protocol specified

Authorization required, but no authorization protocol specified

Authorization required, but no authorization protocol specified

# nccl-tests version 2.18.0 nccl-headers=22907 nccl-library=22907
# Collective test starting: all_gather_perf_mpi
# nThread 1 nGpus 1 minBytes 1048576 maxBytes 134217728 step: 2(factor) warmup iters: 10 iters: 50 agg iters: 1 validation: 1 graph: 0
#
# Using devices
#  Rank  0 Group  0 Pid 2991259 on        lab device  0 [0000:02:00] NVIDIA GeForce RTX 5090
#  Rank  1 Group  0 Pid 2991260 on        lab device  0 [0000:02:00] NVIDIA GeForce RTX 5090
#
#                                                              out-of-place                       in-place
#       size         count      type   redop    root     time   algbw   busbw  #wrong     time   algbw   busbw  #wrong
#        (B)    (elements)                               (us)  (GB/s)  (GB/s)             (us)  (GB/s)  (GB/s)
     1048576        131072     float    none      -1  2191.13    0.48    0.24       0  2239.46    0.47    0.23       0
     2097152        262144     float    none      -1  2189.51    0.96    0.48       0  2196.50    0.95    0.48       0
     4194304        524288     float    none      -1  2204.59    1.90    0.95       0  2204.57    1.90    0.95       0
     8388608       1048576     float    none      -1  2427.09    3.46    1.73       0  2442.13    3.43    1.72       0
    16777216       2097152     float    none      -1  3797.36    4.42    2.21       0  3785.35    4.43    2.22       0
    33554432       4194304     float    none      -1  7557.37    4.44    2.22       0  7500.12    4.47    2.24       0
    67108864       8388608     float    none      -1  14871.0    4.51    2.26       0  14664.7    4.58    2.29       0
   134217728      16777216     float    none      -1  29012.8    4.63    2.31       0  29228.8    4.59    2.30       0
# Out of bounds values : 0 OK
# Avg bus bandwidth    : 1.55084
#
# Collective test concluded: all_gather_perf_mpi
#
```
