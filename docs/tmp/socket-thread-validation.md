# NCCL Socket Thread / AllGather Validation

**Date**: 2026-03-10
**Hardware**: 1x RTX 5090, 2 MPI ranks sharing GPU 0, socket transport
**Common environment**: `NCCL_TESTS_DEVICE=0`, `NCCL_P2P_DISABLE=1`, `NCCL_SHM_DISABLE=1`, `NCCL_NET=Socket`
**Sweep range**: 1MB to 128MB, `-f 2 -g 1 -n 50 -w 10`
**Summary metric**: rank-0 out-of-place algbw (GB/s), matching the prior 16MB/128MB comparisons
**Significance rule**: an improvement is marked `Yes` when `mean(config) - mean(default) > 2 * sqrt(std(default)^2 + std(config)^2)`

## Experiment 1: Socket Thread Tuning

### Summary

| Size | Config A: DEFAULT | Config B: NCCL_SOCKET_NTHREADS=4 NCCL_NSOCKS_PERTHREAD=1 | Config C: NCCL_SOCKET_NTHREADS=8 NCCL_NSOCKS_PERTHREAD=1 | NCCL_SOCKET_NTHREADS=4 NCCL_NSOCKS_PERTHREAD=1 vs DEFAULT | >2σ? | NCCL_SOCKET_NTHREADS=8 NCCL_NSOCKS_PERTHREAD=1 vs DEFAULT | >2σ? |
| --- | --- | --- | --- | --- | --- | --- | --- |
| 1MB | 0.24±0.00 | 0.19±0.04 | 0.18±0.05 | -22.2% | No | -26.4% | No |
| 2MB | 0.48±0.00 | 0.46±0.03 | 0.38±0.09 | -4.9% | No | -20.1% | No |
| 4MB | 0.96±0.00 | 0.86±0.07 | 0.75±0.22 | -10.4% | No | -21.5% | No |
| 8MB | 1.91±0.00 | 1.89±0.02 | 1.77±0.02 | -0.9% | No | -7.5% | No |
| 16MB | 2.20±0.03 | 2.21±0.04 | 2.00±0.09 | +0.8% | No | -8.8% | No |
| 32MB | 2.25±0.02 | 1.87±0.23 | 2.06±0.02 | -16.9% | No | -8.3% | No |
| 64MB | 2.30±0.01 | 1.96±0.08 | 2.16±0.09 | -14.8% | No | -6.2% | No |
| 128MB | 2.30±0.00 | 2.06±0.07 | 2.27±0.02 | -10.6% | No | -1.2% | No |

### Raw Output

### Config A: DEFAULT

Command:
```bash
mpirun --oversubscribe --tag-output \
  -np 1 /usr/bin/env \
    LD_LIBRARY_PATH=/home/yunwei37/workspace/nccl-eBPF/nccl/build/lib \
    NCCL_TESTS_DEVICE=0 NCCL_P2P_DISABLE=1 NCCL_SHM_DISABLE=1 \
    NCCL_NET=Socket NCCL_HOSTID=st-exp1a-rank0 \
    /home/yunwei37/workspace/nccl-eBPF/nccl-tests/build/all_reduce_perf_mpi \
      -b 1048576 -e 134217728 -f 2 -g 1 -n 50 -w 10 \
  : \
  -np 1 /usr/bin/env \
    LD_LIBRARY_PATH=/home/yunwei37/workspace/nccl-eBPF/nccl/build/lib \
    NCCL_TESTS_DEVICE=0 NCCL_P2P_DISABLE=1 NCCL_SHM_DISABLE=1 \
    NCCL_NET=Socket NCCL_HOSTID=st-exp1a-rank1 \
    /home/yunwei37/workspace/nccl-eBPF/nccl-tests/build/all_reduce_perf_mpi \
      -b 1048576 -e 134217728 -f 2 -g 1 -n 50 -w 10
```

#### Rep 1: `exp1-config-a-default-rep1.log`

```text
Authorization required, but no authorization protocol specified

Authorization required, but no authorization protocol specified

[1,0]<stderr>:Authorization required, but no authorization protocol specified
[1,0]<stderr>:
[1,1]<stderr>:Authorization required, but no authorization protocol specified
[1,1]<stderr>:
[1,0]<stdout>:# nccl-tests version 2.18.0 nccl-headers=22907 nccl-library=22907
[1,0]<stdout>:# Collective test starting: all_reduce_perf_mpi
[1,0]<stdout>:# nThread 1 nGpus 1 minBytes 1048576 maxBytes 134217728 step: 2(factor) warmup iters: 10 iters: 50 agg iters: 1 validation: 1 graph: 0
[1,0]<stdout>:#
[1,0]<stdout>:# Using devices
[1,0]<stdout>:#  Rank  0 Group  0 Pid 2994447 on        lab device  0 [0000:02:00] NVIDIA GeForce RTX 5090
[1,0]<stdout>:#  Rank  1 Group  0 Pid 2994448 on        lab device  0 [0000:02:00] NVIDIA GeForce RTX 5090
[1,0]<stdout>:#
[1,0]<stdout>:#                                                              out-of-place                       in-place          
[1,0]<stdout>:#       size         count      type   redop    root     time   algbw   busbw  #wrong     time   algbw   busbw  #wrong 
[1,0]<stdout>:#        (B)    (elements)                               (us)  (GB/s)  (GB/s)             (us)  (GB/s)  (GB/s)         
[1,0]<stdout>:     1048576        262144     float     sum      -1  4366.46    0.24    0.24       0  4365.21    0.24    0.24       0
[1,0]<stdout>:     2097152        524288     float     sum      -1  4370.57    0.48    0.48       0  4368.39    0.48    0.48       0
[1,0]<stdout>:     4194304       1048576     float     sum      -1  4374.00    0.96    0.96       0  4375.49    0.96    0.96       0
[1,0]<stdout>:     8388608       2097152     float     sum      -1  4390.92    1.91    1.91       0  4394.16    1.91    1.91       0
[1,0]<stdout>:    16777216       4194304     float     sum      -1  7728.29    2.17    2.17       0  7659.38    2.19    2.19       0
[1,0]<stdout>:    33554432       8388608     float     sum      -1  14977.1    2.24    2.24       0  14863.2    2.26    2.26       0
[1,0]<stdout>:    67108864      16777216     float     sum      -1  29341.4    2.29    2.29       0  29337.0    2.29    2.29       0
[1,0]<stdout>:   134217728      33554432     float     sum      -1  58479.5    2.30    2.30       0  58055.4    2.31    2.31       0
[1,0]<stdout>:# Out of bounds values : 0 OK
[1,0]<stdout>:# Avg bus bandwidth    : 1.57614 
[1,0]<stdout>:#
[1,0]<stdout>:# Collective test concluded: all_reduce_perf_mpi
[1,0]<stdout>:#
[1,0]<stdout>:
```

#### Rep 2: `exp1-config-a-default-rep2.log`

```text
Authorization required, but no authorization protocol specified

Authorization required, but no authorization protocol specified

[1,0]<stderr>:Authorization required, but no authorization protocol specified
[1,0]<stderr>:
[1,1]<stderr>:Authorization required, but no authorization protocol specified
[1,1]<stderr>:
[1,0]<stdout>:# nccl-tests version 2.18.0 nccl-headers=22907 nccl-library=22907
[1,0]<stdout>:# Collective test starting: all_reduce_perf_mpi
[1,0]<stdout>:# nThread 1 nGpus 1 minBytes 1048576 maxBytes 134217728 step: 2(factor) warmup iters: 10 iters: 50 agg iters: 1 validation: 1 graph: 0
[1,0]<stdout>:#
[1,0]<stdout>:# Using devices
[1,0]<stdout>:#  Rank  0 Group  0 Pid 2994729 on        lab device  0 [0000:02:00] NVIDIA GeForce RTX 5090
[1,0]<stdout>:#  Rank  1 Group  0 Pid 2994730 on        lab device  0 [0000:02:00] NVIDIA GeForce RTX 5090
[1,0]<stdout>:#
[1,0]<stdout>:#                                                              out-of-place                       in-place          
[1,0]<stdout>:#       size         count      type   redop    root     time   algbw   busbw  #wrong     time   algbw   busbw  #wrong 
[1,0]<stdout>:#        (B)    (elements)                               (us)  (GB/s)  (GB/s)             (us)  (GB/s)  (GB/s)         
[1,0]<stdout>:     1048576        262144     float     sum      -1  4366.11    0.24    0.24       0  4366.96    0.24    0.24       0
[1,0]<stdout>:     2097152        524288     float     sum      -1  4370.08    0.48    0.48       0  4370.29    0.48    0.48       0
[1,0]<stdout>:     4194304       1048576     float     sum      -1  4375.00    0.96    0.96       0  4373.37    0.96    0.96       0
[1,0]<stdout>:     8388608       2097152     float     sum      -1  4389.39    1.91    1.91       0  4386.11    1.91    1.91       0
[1,0]<stdout>:    16777216       4194304     float     sum      -1  7552.70    2.22    2.22       0  7721.09    2.17    2.17       0
[1,0]<stdout>:    33554432       8388608     float     sum      -1  14753.9    2.27    2.27       0  14799.5    2.27    2.27       0
[1,0]<stdout>:    67108864      16777216     float     sum      -1  28990.0    2.31    2.31       0  29495.3    2.28    2.28       0
[1,0]<stdout>:   134217728      33554432     float     sum      -1  58458.9    2.30    2.30       0  58203.9    2.31    2.31       0
[1,0]<stdout>:# Out of bounds values : 0 OK
[1,0]<stdout>:# Avg bus bandwidth    : 1.58183 
[1,0]<stdout>:#
[1,0]<stdout>:# Collective test concluded: all_reduce_perf_mpi
[1,0]<stdout>:#
[1,0]<stdout>:
```

#### Rep 3: `exp1-config-a-default-rep3.log`

```text
Authorization required, but no authorization protocol specified

Authorization required, but no authorization protocol specified

[1,0]<stderr>:Authorization required, but no authorization protocol specified
[1,0]<stderr>:
[1,1]<stderr>:Authorization required, but no authorization protocol specified
[1,1]<stderr>:
[1,0]<stdout>:# nccl-tests version 2.18.0 nccl-headers=22907 nccl-library=22907
[1,0]<stdout>:# Collective test starting: all_reduce_perf_mpi
[1,0]<stdout>:# nThread 1 nGpus 1 minBytes 1048576 maxBytes 134217728 step: 2(factor) warmup iters: 10 iters: 50 agg iters: 1 validation: 1 graph: 0
[1,0]<stdout>:#
[1,0]<stdout>:# Using devices
[1,0]<stdout>:#  Rank  0 Group  0 Pid 2995412 on        lab device  0 [0000:02:00] NVIDIA GeForce RTX 5090
[1,0]<stdout>:#  Rank  1 Group  0 Pid 2995413 on        lab device  0 [0000:02:00] NVIDIA GeForce RTX 5090
[1,0]<stdout>:#
[1,0]<stdout>:#                                                              out-of-place                       in-place          
[1,0]<stdout>:#       size         count      type   redop    root     time   algbw   busbw  #wrong     time   algbw   busbw  #wrong 
[1,0]<stdout>:#        (B)    (elements)                               (us)  (GB/s)  (GB/s)             (us)  (GB/s)  (GB/s)         
[1,0]<stdout>:     1048576        262144     float     sum      -1  4367.08    0.24    0.24       0  4365.26    0.24    0.24       0
[1,0]<stdout>:     2097152        524288     float     sum      -1  4368.94    0.48    0.48       0  4368.41    0.48    0.48       0
[1,0]<stdout>:     4194304       1048576     float     sum      -1  4373.85    0.96    0.96       0  4466.17    0.94    0.94       0
[1,0]<stdout>:     8388608       2097152     float     sum      -1  4397.15    1.91    1.91       0  4392.47    1.91    1.91       0
[1,0]<stdout>:    16777216       4194304     float     sum      -1  7632.51    2.20    2.20       0  7728.14    2.17    2.17       0
[1,0]<stdout>:    33554432       8388608     float     sum      -1  14963.2    2.24    2.24       0  14640.3    2.29    2.29       0
[1,0]<stdout>:    67108864      16777216     float     sum      -1  29187.5    2.30    2.30       0  29067.2    2.31    2.31       0
[1,0]<stdout>:   134217728      33554432     float     sum      -1  58468.9    2.30    2.30       0  57695.7    2.33    2.33       0
[1,0]<stdout>:# Out of bounds values : 0 OK
[1,0]<stdout>:# Avg bus bandwidth    : 1.58058 
[1,0]<stdout>:#
[1,0]<stdout>:# Collective test concluded: all_reduce_perf_mpi
[1,0]<stdout>:#
[1,0]<stdout>:
```

### Config B: NCCL_SOCKET_NTHREADS=4 NCCL_NSOCKS_PERTHREAD=1

Command:
```bash
mpirun --oversubscribe --tag-output \
  -np 1 /usr/bin/env \
    LD_LIBRARY_PATH=/home/yunwei37/workspace/nccl-eBPF/nccl/build/lib \
    NCCL_TESTS_DEVICE=0 NCCL_P2P_DISABLE=1 NCCL_SHM_DISABLE=1 \
    NCCL_NET=Socket NCCL_HOSTID=st-exp1b-rank0 \
    NCCL_SOCKET_NTHREADS=4 NCCL_NSOCKS_PERTHREAD=1 \
    /home/yunwei37/workspace/nccl-eBPF/nccl-tests/build/all_reduce_perf_mpi \
      -b 1048576 -e 134217728 -f 2 -g 1 -n 50 -w 10 \
  : \
  -np 1 /usr/bin/env \
    LD_LIBRARY_PATH=/home/yunwei37/workspace/nccl-eBPF/nccl/build/lib \
    NCCL_TESTS_DEVICE=0 NCCL_P2P_DISABLE=1 NCCL_SHM_DISABLE=1 \
    NCCL_NET=Socket NCCL_HOSTID=st-exp1b-rank1 \
    NCCL_SOCKET_NTHREADS=4 NCCL_NSOCKS_PERTHREAD=1 \
    /home/yunwei37/workspace/nccl-eBPF/nccl-tests/build/all_reduce_perf_mpi \
      -b 1048576 -e 134217728 -f 2 -g 1 -n 50 -w 10
```

#### Rep 1: `exp1-config-b-nthreads4-rep1.log`

```text
Authorization required, but no authorization protocol specified

Authorization required, but no authorization protocol specified

[1,1]<stderr>:Authorization required, but no authorization protocol specified
[1,1]<stderr>:
[1,0]<stderr>:Authorization required, but no authorization protocol specified
[1,0]<stderr>:
[1,0]<stdout>:# nccl-tests version 2.18.0 nccl-headers=22907 nccl-library=22907
[1,0]<stdout>:# Collective test starting: all_reduce_perf_mpi
[1,0]<stdout>:# nThread 1 nGpus 1 minBytes 1048576 maxBytes 134217728 step: 2(factor) warmup iters: 10 iters: 50 agg iters: 1 validation: 1 graph: 0
[1,0]<stdout>:#
[1,0]<stdout>:# Using devices
[1,0]<stdout>:#  Rank  0 Group  0 Pid 2995974 on        lab device  0 [0000:02:00] NVIDIA GeForce RTX 5090
[1,0]<stdout>:#  Rank  1 Group  0 Pid 2995975 on        lab device  0 [0000:02:00] NVIDIA GeForce RTX 5090
[1,0]<stdout>:#
[1,0]<stdout>:#                                                              out-of-place                       in-place          
[1,0]<stdout>:#       size         count      type   redop    root     time   algbw   busbw  #wrong     time   algbw   busbw  #wrong 
[1,0]<stdout>:#        (B)    (elements)                               (us)  (GB/s)  (GB/s)             (us)  (GB/s)  (GB/s)         
[1,0]<stdout>:     1048576        262144     float     sum      -1  4660.81    0.22    0.22       0  4425.10    0.24    0.24       0
[1,0]<stdout>:     2097152        524288     float     sum      -1  4375.39    0.48    0.48       0  4506.09    0.47    0.47       0
[1,0]<stdout>:     4194304       1048576     float     sum      -1  5369.49    0.78    0.78       0  4814.66    0.87    0.87       0
[1,0]<stdout>:     8388608       2097152     float     sum      -1  4441.24    1.89    1.89       0  4479.41    1.87    1.87       0
[1,0]<stdout>:    16777216       4194304     float     sum      -1  7447.61    2.25    2.25       0  7559.13    2.22    2.22       0
[1,0]<stdout>:    33554432       8388608     float     sum      -1  16703.3    2.01    2.01       0  16606.3    2.02    2.02       0
[1,0]<stdout>:    67108864      16777216     float     sum      -1  33025.9    2.03    2.03       0  30944.6    2.17    2.17       0
[1,0]<stdout>:   134217728      33554432     float     sum      -1  63590.6    2.11    2.11       0  66047.1    2.03    2.03       0
[1,0]<stdout>:# Out of bounds values : 0 OK
[1,0]<stdout>:# Avg bus bandwidth    : 1.4791 
[1,0]<stdout>:#
[1,0]<stdout>:# Collective test concluded: all_reduce_perf_mpi
[1,0]<stdout>:#
[1,0]<stdout>:
```

#### Rep 2: `exp1-config-b-nthreads4-rep2.log`

```text
Authorization required, but no authorization protocol specified

Authorization required, but no authorization protocol specified

[1,1]<stderr>:Authorization required, but no authorization protocol specified
[1,1]<stderr>:
[1,0]<stderr>:Authorization required, but no authorization protocol specified
[1,0]<stderr>:
[1,0]<stdout>:# nccl-tests version 2.18.0 nccl-headers=22907 nccl-library=22907
[1,0]<stdout>:# Collective test starting: all_reduce_perf_mpi
[1,0]<stdout>:# nThread 1 nGpus 1 minBytes 1048576 maxBytes 134217728 step: 2(factor) warmup iters: 10 iters: 50 agg iters: 1 validation: 1 graph: 0
[1,0]<stdout>:#
[1,0]<stdout>:# Using devices
[1,0]<stdout>:#  Rank  0 Group  0 Pid 2996398 on        lab device  0 [0000:02:00] NVIDIA GeForce RTX 5090
[1,0]<stdout>:#  Rank  1 Group  0 Pid 2996399 on        lab device  0 [0000:02:00] NVIDIA GeForce RTX 5090
[1,0]<stdout>:#
[1,0]<stdout>:#                                                              out-of-place                       in-place          
[1,0]<stdout>:#       size         count      type   redop    root     time   algbw   busbw  #wrong     time   algbw   busbw  #wrong 
[1,0]<stdout>:#        (B)    (elements)                               (us)  (GB/s)  (GB/s)             (us)  (GB/s)  (GB/s)         
[1,0]<stdout>:     1048576        262144     float     sum      -1  7072.12    0.15    0.15       0  6961.52    0.15    0.15       0
[1,0]<stdout>:     2097152        524288     float     sum      -1  4579.27    0.46    0.46       0  4413.32    0.48    0.48       0
[1,0]<stdout>:     4194304       1048576     float     sum      -1  4542.58    0.92    0.92       0  4476.28    0.94    0.94       0
[1,0]<stdout>:     8388608       2097152     float     sum      -1  4471.01    1.88    1.88       0  4486.20    1.87    1.87       0
[1,0]<stdout>:    16777216       4194304     float     sum      -1  7708.27    2.18    2.18       0  7995.23    2.10    2.10       0
[1,0]<stdout>:    33554432       8388608     float     sum      -1  20920.6    1.60    1.60       0  16334.5    2.05    2.05       0
[1,0]<stdout>:    67108864      16777216     float     sum      -1  34023.4    1.97    1.97       0  33204.1    2.02    2.02       0
[1,0]<stdout>:   134217728      33554432     float     sum      -1  67765.9    1.98    1.98       0  64417.7    2.08    2.08       0
[1,0]<stdout>:# Out of bounds values : 0 OK
[1,0]<stdout>:# Avg bus bandwidth    : 1.42682 
[1,0]<stdout>:#
[1,0]<stdout>:# Collective test concluded: all_reduce_perf_mpi
[1,0]<stdout>:#
[1,0]<stdout>:
```

#### Rep 3: `exp1-config-b-nthreads4-rep3.log`

```text
Authorization required, but no authorization protocol specified

Authorization required, but no authorization protocol specified

[1,0]<stderr>:Authorization required, but no authorization protocol specified
[1,0]<stderr>:
[1,1]<stderr>:Authorization required, but no authorization protocol specified
[1,1]<stderr>:
[1,0]<stdout>:# nccl-tests version 2.18.0 nccl-headers=22907 nccl-library=22907
[1,0]<stdout>:# Collective test starting: all_reduce_perf_mpi
[1,0]<stdout>:# nThread 1 nGpus 1 minBytes 1048576 maxBytes 134217728 step: 2(factor) warmup iters: 10 iters: 50 agg iters: 1 validation: 1 graph: 0
[1,0]<stdout>:#
[1,0]<stdout>:# Using devices
[1,0]<stdout>:#  Rank  0 Group  0 Pid 2996847 on        lab device  0 [0000:02:00] NVIDIA GeForce RTX 5090
[1,0]<stdout>:#  Rank  1 Group  0 Pid 2996848 on        lab device  0 [0000:02:00] NVIDIA GeForce RTX 5090
[1,0]<stdout>:#
[1,0]<stdout>:#                                                              out-of-place                       in-place          
[1,0]<stdout>:#       size         count      type   redop    root     time   algbw   busbw  #wrong     time   algbw   busbw  #wrong 
[1,0]<stdout>:#        (B)    (elements)                               (us)  (GB/s)  (GB/s)             (us)  (GB/s)  (GB/s)         
[1,0]<stdout>:     1048576        262144     float     sum      -1  5404.60    0.19    0.19       0  5674.14    0.18    0.18       0
[1,0]<stdout>:     2097152        524288     float     sum      -1  4838.08    0.43    0.43       0  4812.77    0.44    0.44       0
[1,0]<stdout>:     4194304       1048576     float     sum      -1  4782.51    0.88    0.88       0  4507.76    0.93    0.93       0
[1,0]<stdout>:     8388608       2097152     float     sum      -1  4392.06    1.91    1.91       0  4479.85    1.87    1.87       0
[1,0]<stdout>:    16777216       4194304     float     sum      -1  7577.12    2.21    2.21       0  7297.58    2.30    2.30       0
[1,0]<stdout>:    33554432       8388608     float     sum      -1  16745.6    2.00    2.00       0  18156.9    1.85    1.85       0
[1,0]<stdout>:    67108864      16777216     float     sum      -1  35624.0    1.88    1.88       0  35065.6    1.91    1.91       0
[1,0]<stdout>:   134217728      33554432     float     sum      -1  64472.0    2.08    2.08       0  63395.3    2.12    2.12       0
[1,0]<stdout>:# Out of bounds values : 0 OK
[1,0]<stdout>:# Avg bus bandwidth    : 1.44997 
[1,0]<stdout>:#
[1,0]<stdout>:# Collective test concluded: all_reduce_perf_mpi
[1,0]<stdout>:#
[1,0]<stdout>:
```

### Config C: NCCL_SOCKET_NTHREADS=8 NCCL_NSOCKS_PERTHREAD=1

Command:
```bash
mpirun --oversubscribe --tag-output \
  -np 1 /usr/bin/env \
    LD_LIBRARY_PATH=/home/yunwei37/workspace/nccl-eBPF/nccl/build/lib \
    NCCL_TESTS_DEVICE=0 NCCL_P2P_DISABLE=1 NCCL_SHM_DISABLE=1 \
    NCCL_NET=Socket NCCL_HOSTID=st-exp1c-rank0 \
    NCCL_SOCKET_NTHREADS=8 NCCL_NSOCKS_PERTHREAD=1 \
    /home/yunwei37/workspace/nccl-eBPF/nccl-tests/build/all_reduce_perf_mpi \
      -b 1048576 -e 134217728 -f 2 -g 1 -n 50 -w 10 \
  : \
  -np 1 /usr/bin/env \
    LD_LIBRARY_PATH=/home/yunwei37/workspace/nccl-eBPF/nccl/build/lib \
    NCCL_TESTS_DEVICE=0 NCCL_P2P_DISABLE=1 NCCL_SHM_DISABLE=1 \
    NCCL_NET=Socket NCCL_HOSTID=st-exp1c-rank1 \
    NCCL_SOCKET_NTHREADS=8 NCCL_NSOCKS_PERTHREAD=1 \
    /home/yunwei37/workspace/nccl-eBPF/nccl-tests/build/all_reduce_perf_mpi \
      -b 1048576 -e 134217728 -f 2 -g 1 -n 50 -w 10
```

#### Rep 1: `exp1-config-c-nthreads8-rep1.log`

```text
Authorization required, but no authorization protocol specified

Authorization required, but no authorization protocol specified

[1,0]<stderr>:Authorization required, but no authorization protocol specified
[1,0]<stderr>:
[1,1]<stderr>:Authorization required, but no authorization protocol specified
[1,1]<stderr>:
[1,0]<stdout>:# nccl-tests version 2.18.0 nccl-headers=22907 nccl-library=22907
[1,0]<stdout>:# Collective test starting: all_reduce_perf_mpi
[1,0]<stdout>:# nThread 1 nGpus 1 minBytes 1048576 maxBytes 134217728 step: 2(factor) warmup iters: 10 iters: 50 agg iters: 1 validation: 1 graph: 0
[1,0]<stdout>:#
[1,0]<stdout>:# Using devices
[1,0]<stdout>:#  Rank  0 Group  0 Pid 2997292 on        lab device  0 [0000:02:00] NVIDIA GeForce RTX 5090
[1,0]<stdout>:#  Rank  1 Group  0 Pid 2997293 on        lab device  0 [0000:02:00] NVIDIA GeForce RTX 5090
[1,0]<stdout>:#
[1,0]<stdout>:#                                                              out-of-place                       in-place          
[1,0]<stdout>:#       size         count      type   redop    root     time   algbw   busbw  #wrong     time   algbw   busbw  #wrong 
[1,0]<stdout>:#        (B)    (elements)                               (us)  (GB/s)  (GB/s)             (us)  (GB/s)  (GB/s)         
[1,0]<stdout>:     1048576        262144     float     sum      -1  7317.27    0.14    0.14       0  4575.26    0.23    0.23       0
[1,0]<stdout>:     2097152        524288     float     sum      -1  7396.03    0.28    0.28       0  4811.66    0.44    0.44       0
[1,0]<stdout>:     4194304       1048576     float     sum      -1  4774.16    0.88    0.88       0  5012.61    0.84    0.84       0
[1,0]<stdout>:     8388608       2097152     float     sum      -1  4725.27    1.78    1.78       0  5148.79    1.63    1.63       0
[1,0]<stdout>:    16777216       4194304     float     sum      -1  8237.44    2.04    2.04       0  8389.75    2.00    2.00       0
[1,0]<stdout>:    33554432       8388608     float     sum      -1  16049.4    2.09    2.09       0  15583.5    2.15    2.15       0
[1,0]<stdout>:    67108864      16777216     float     sum      -1  30131.1    2.23    2.23       0  30241.9    2.22    2.22       0
[1,0]<stdout>:   134217728      33554432     float     sum      -1  58356.8    2.30    2.30       0  59140.4    2.27    2.27       0
[1,0]<stdout>:# Out of bounds values : 0 OK
[1,0]<stdout>:# Avg bus bandwidth    : 1.46923 
[1,0]<stdout>:#
[1,0]<stdout>:# Collective test concluded: all_reduce_perf_mpi
[1,0]<stdout>:#
[1,0]<stdout>:
```

#### Rep 2: `exp1-config-c-nthreads8-rep2.log`

```text
Authorization required, but no authorization protocol specified

Authorization required, but no authorization protocol specified

[1,0]<stderr>:Authorization required, but no authorization protocol specified
[1,0]<stderr>:
[1,1]<stderr>:Authorization required, but no authorization protocol specified
[1,1]<stderr>:
[1,0]<stdout>:# nccl-tests version 2.18.0 nccl-headers=22907 nccl-library=22907
[1,0]<stdout>:# Collective test starting: all_reduce_perf_mpi
[1,0]<stdout>:# nThread 1 nGpus 1 minBytes 1048576 maxBytes 134217728 step: 2(factor) warmup iters: 10 iters: 50 agg iters: 1 validation: 1 graph: 0
[1,0]<stdout>:#
[1,0]<stdout>:# Using devices
[1,0]<stdout>:#  Rank  0 Group  0 Pid 2997910 on        lab device  0 [0000:02:00] NVIDIA GeForce RTX 5090
[1,0]<stdout>:#  Rank  1 Group  0 Pid 2997911 on        lab device  0 [0000:02:00] NVIDIA GeForce RTX 5090
[1,0]<stdout>:#
[1,0]<stdout>:#                                                              out-of-place                       in-place          
[1,0]<stdout>:#       size         count      type   redop    root     time   algbw   busbw  #wrong     time   algbw   busbw  #wrong 
[1,0]<stdout>:#        (B)    (elements)                               (us)  (GB/s)  (GB/s)             (us)  (GB/s)  (GB/s)         
[1,0]<stdout>:     1048576        262144     float     sum      -1  4517.45    0.23    0.23       0  4522.44    0.23    0.23       0
[1,0]<stdout>:     2097152        524288     float     sum      -1  4794.56    0.44    0.44       0  4373.14    0.48    0.48       0
[1,0]<stdout>:     4194304       1048576     float     sum      -1  8455.43    0.50    0.50       0  4812.57    0.87    0.87       0
[1,0]<stdout>:     8388608       2097152     float     sum      -1  4709.62    1.78    1.78       0  4916.12    1.71    1.71       0
[1,0]<stdout>:    16777216       4194304     float     sum      -1  8105.07    2.07    2.07       0  8619.03    1.95    1.95       0
[1,0]<stdout>:    33554432       8388608     float     sum      -1  16330.9    2.05    2.05       0  15940.4    2.10    2.10       0
[1,0]<stdout>:    67108864      16777216     float     sum      -1  32519.2    2.06    2.06       0  30221.9    2.22    2.22       0
[1,0]<stdout>:   134217728      33554432     float     sum      -1  59400.5    2.26    2.26       0  59696.0    2.25    2.25       0
[1,0]<stdout>:# Out of bounds values : 0 OK
[1,0]<stdout>:# Avg bus bandwidth    : 1.45027 
[1,0]<stdout>:#
[1,0]<stdout>:# Collective test concluded: all_reduce_perf_mpi
[1,0]<stdout>:#
[1,0]<stdout>:
```

#### Rep 3: `exp1-config-c-nthreads8-rep3.log`

```text
Authorization required, but no authorization protocol specified

Authorization required, but no authorization protocol specified

[1,1]<stderr>:Authorization required, but no authorization protocol specified
[1,1]<stderr>:
[1,0]<stderr>:Authorization required, but no authorization protocol specified
[1,0]<stderr>:
[1,0]<stdout>:# nccl-tests version 2.18.0 nccl-headers=22907 nccl-library=22907
[1,0]<stdout>:# Collective test starting: all_reduce_perf_mpi
[1,0]<stdout>:# nThread 1 nGpus 1 minBytes 1048576 maxBytes 134217728 step: 2(factor) warmup iters: 10 iters: 50 agg iters: 1 validation: 1 graph: 0
[1,0]<stdout>:#
[1,0]<stdout>:# Using devices
[1,0]<stdout>:#  Rank  0 Group  0 Pid 2998847 on        lab device  0 [0000:02:00] NVIDIA GeForce RTX 5090
[1,0]<stdout>:#  Rank  1 Group  0 Pid 2998848 on        lab device  0 [0000:02:00] NVIDIA GeForce RTX 5090
[1,0]<stdout>:#
[1,0]<stdout>:#                                                              out-of-place                       in-place          
[1,0]<stdout>:#       size         count      type   redop    root     time   algbw   busbw  #wrong     time   algbw   busbw  #wrong 
[1,0]<stdout>:#        (B)    (elements)                               (us)  (GB/s)  (GB/s)             (us)  (GB/s)  (GB/s)         
[1,0]<stdout>:     1048576        262144     float     sum      -1  6665.40    0.16    0.16       0  6949.20    0.15    0.15       0
[1,0]<stdout>:     2097152        524288     float     sum      -1  4833.59    0.43    0.43       0  4833.01    0.43    0.43       0
[1,0]<stdout>:     4194304       1048576     float     sum      -1  4759.26    0.88    0.88       0  5963.01    0.70    0.70       0
[1,0]<stdout>:     8388608       2097152     float     sum      -1  4808.23    1.74    1.74       0  5398.93    1.55    1.55       0
[1,0]<stdout>:    16777216       4194304     float     sum      -1  8826.59    1.90    1.90       0  9077.32    1.85    1.85       0
[1,0]<stdout>:    33554432       8388608     float     sum      -1  16342.0    2.05    2.05       0  16248.1    2.07    2.07       0
[1,0]<stdout>:    67108864      16777216     float     sum      -1  30724.6    2.18    2.18       0  28962.8    2.32    2.32       0
[1,0]<stdout>:   134217728      33554432     float     sum      -1  59351.9    2.26    2.26       0  60796.8    2.21    2.21       0
[1,0]<stdout>:# Out of bounds values : 0 OK
[1,0]<stdout>:# Avg bus bandwidth    : 1.43105 
[1,0]<stdout>:#
[1,0]<stdout>:# Collective test concluded: all_reduce_perf_mpi
[1,0]<stdout>:#
[1,0]<stdout>:
```

## Experiment 2: AllGather DEFAULT vs Simple

### Summary

| Size | Config A: DEFAULT | Config B: NCCL_PROTO=Simple | NCCL_PROTO=Simple vs DEFAULT | >2σ? |
| --- | --- | --- | --- | --- |
| 1MB | 0.48±0.00 | 0.48±0.00 | +0.0% | No |
| 2MB | 0.96±0.00 | 0.96±0.00 | +0.0% | No |
| 4MB | 1.90±0.00 | 1.91±0.01 | +0.4% | No |
| 8MB | 3.47±0.09 | 3.63±0.06 | +4.6% | No |
| 16MB | 4.42±0.03 | 4.48±0.08 | +1.4% | No |
| 32MB | 4.45±0.03 | 4.42±0.05 | -0.7% | No |
| 64MB | 4.53±0.06 | 4.52±0.02 | -0.2% | No |
| 128MB | 4.57±0.04 | 4.62±0.03 | +0.9% | No |

### Raw Output

### Config A: DEFAULT

Command:
```bash
mpirun --oversubscribe --tag-output \
  -np 1 /usr/bin/env \
    LD_LIBRARY_PATH=/home/yunwei37/workspace/nccl-eBPF/nccl/build/lib \
    NCCL_TESTS_DEVICE=0 NCCL_P2P_DISABLE=1 NCCL_SHM_DISABLE=1 \
    NCCL_NET=Socket NCCL_HOSTID=st-exp2a-rank0 \
    /home/yunwei37/workspace/nccl-eBPF/nccl-tests/build/all_gather_perf_mpi \
      -b 1048576 -e 134217728 -f 2 -g 1 -n 50 -w 10 \
  : \
  -np 1 /usr/bin/env \
    LD_LIBRARY_PATH=/home/yunwei37/workspace/nccl-eBPF/nccl/build/lib \
    NCCL_TESTS_DEVICE=0 NCCL_P2P_DISABLE=1 NCCL_SHM_DISABLE=1 \
    NCCL_NET=Socket NCCL_HOSTID=st-exp2a-rank1 \
    /home/yunwei37/workspace/nccl-eBPF/nccl-tests/build/all_gather_perf_mpi \
      -b 1048576 -e 134217728 -f 2 -g 1 -n 50 -w 10
```

#### Rep 1: `exp2-config-a-default-rep1.log`

```text
Authorization required, but no authorization protocol specified

Authorization required, but no authorization protocol specified

[1,1]<stderr>:Authorization required, but no authorization protocol specified
[1,1]<stderr>:
[1,0]<stderr>:Authorization required, but no authorization protocol specified
[1,0]<stderr>:
[1,0]<stdout>:# nccl-tests version 2.18.0 nccl-headers=22907 nccl-library=22907
[1,0]<stdout>:# Collective test starting: all_gather_perf_mpi
[1,0]<stdout>:# nThread 1 nGpus 1 minBytes 1048576 maxBytes 134217728 step: 2(factor) warmup iters: 10 iters: 50 agg iters: 1 validation: 1 graph: 0
[1,0]<stdout>:#
[1,0]<stdout>:# Using devices
[1,0]<stdout>:#  Rank  0 Group  0 Pid 2999700 on        lab device  0 [0000:02:00] NVIDIA GeForce RTX 5090
[1,0]<stdout>:#  Rank  1 Group  0 Pid 2999701 on        lab device  0 [0000:02:00] NVIDIA GeForce RTX 5090
[1,0]<stdout>:#
[1,0]<stdout>:#                                                              out-of-place                       in-place          
[1,0]<stdout>:#       size         count      type   redop    root     time   algbw   busbw  #wrong     time   algbw   busbw  #wrong 
[1,0]<stdout>:#        (B)    (elements)                               (us)  (GB/s)  (GB/s)             (us)  (GB/s)  (GB/s)         
[1,0]<stdout>:     1048576        131072     float    none      -1  2191.72    0.48    0.24       0  2194.02    0.48    0.24       0
[1,0]<stdout>:     2097152        262144     float    none      -1  2190.49    0.96    0.48       0  2191.01    0.96    0.48       0
[1,0]<stdout>:     4194304        524288     float    none      -1  2203.69    1.90    0.95       0  2193.90    1.91    0.96       0
[1,0]<stdout>:     8388608       1048576     float    none      -1  2489.25    3.37    1.68       0  2355.03    3.56    1.78       0
[1,0]<stdout>:    16777216       2097152     float    none      -1  3822.09    4.39    2.19       0  3871.80    4.33    2.17       0
[1,0]<stdout>:    33554432       4194304     float    none      -1  7570.74    4.43    2.22       0  7538.26    4.45    2.23       0
[1,0]<stdout>:    67108864       8388608     float    none      -1  14987.7    4.48    2.24       0  14882.2    4.51    2.25       0
[1,0]<stdout>:   134217728      16777216     float    none      -1  29185.2    4.60    2.30       0  29349.9    4.57    2.29       0
[1,0]<stdout>:# Out of bounds values : 0 OK
[1,0]<stdout>:# Avg bus bandwidth    : 1.54321 
[1,0]<stdout>:#
[1,0]<stdout>:# Collective test concluded: all_gather_perf_mpi
[1,0]<stdout>:#
[1,0]<stdout>:
```

#### Rep 2: `exp2-config-a-default-rep2.log`

```text
Authorization required, but no authorization protocol specified

Authorization required, but no authorization protocol specified

[1,0]<stderr>:Authorization required, but no authorization protocol specified
[1,0]<stderr>:
[1,1]<stderr>:Authorization required, but no authorization protocol specified
[1,1]<stderr>:
[1,0]<stdout>:# nccl-tests version 2.18.0 nccl-headers=22907 nccl-library=22907
[1,0]<stdout>:# Collective test starting: all_gather_perf_mpi
[1,0]<stdout>:# nThread 1 nGpus 1 minBytes 1048576 maxBytes 134217728 step: 2(factor) warmup iters: 10 iters: 50 agg iters: 1 validation: 1 graph: 0
[1,0]<stdout>:#
[1,0]<stdout>:# Using devices
[1,0]<stdout>:#  Rank  0 Group  0 Pid 2999911 on        lab device  0 [0000:02:00] NVIDIA GeForce RTX 5090
[1,0]<stdout>:#  Rank  1 Group  0 Pid 2999912 on        lab device  0 [0000:02:00] NVIDIA GeForce RTX 5090
[1,0]<stdout>:#
[1,0]<stdout>:#                                                              out-of-place                       in-place          
[1,0]<stdout>:#       size         count      type   redop    root     time   algbw   busbw  #wrong     time   algbw   busbw  #wrong 
[1,0]<stdout>:#        (B)    (elements)                               (us)  (GB/s)  (GB/s)             (us)  (GB/s)  (GB/s)         
[1,0]<stdout>:     1048576        131072     float    none      -1  2188.10    0.48    0.24       0  2188.66    0.48    0.24       0
[1,0]<stdout>:     2097152        262144     float    none      -1  2190.67    0.96    0.48       0  2191.48    0.96    0.48       0
[1,0]<stdout>:     4194304        524288     float    none      -1  2203.53    1.90    0.95       0  2201.51    1.91    0.95       0
[1,0]<stdout>:     8388608       1048576     float    none      -1  2373.12    3.53    1.77       0  2309.92    3.63    1.82       0
[1,0]<stdout>:    16777216       2097152     float    none      -1  3797.73    4.42    2.21       0  3876.52    4.33    2.16       0
[1,0]<stdout>:    33554432       4194304     float    none      -1  7564.28    4.44    2.22       0  7544.85    4.45    2.22       0
[1,0]<stdout>:    67108864       8388608     float    none      -1  14855.9    4.52    2.26       0  14860.0    4.52    2.26       0
[1,0]<stdout>:   134217728      16777216     float    none      -1  29635.6    4.53    2.26       0  29242.8    4.59    2.29       0
[1,0]<stdout>:# Out of bounds values : 0 OK
[1,0]<stdout>:# Avg bus bandwidth    : 1.55089 
[1,0]<stdout>:#
[1,0]<stdout>:# Collective test concluded: all_gather_perf_mpi
[1,0]<stdout>:#
[1,0]<stdout>:
```

#### Rep 3: `exp2-config-a-default-rep3.log`

```text
Authorization required, but no authorization protocol specified

Authorization required, but no authorization protocol specified

[1,0]<stderr>:Authorization required, but no authorization protocol specified
[1,0]<stderr>:
[1,1]<stderr>:Authorization required, but no authorization protocol specified
[1,1]<stderr>:
[1,0]<stdout>:# nccl-tests version 2.18.0 nccl-headers=22907 nccl-library=22907
[1,0]<stdout>:# Collective test starting: all_gather_perf_mpi
[1,0]<stdout>:# nThread 1 nGpus 1 minBytes 1048576 maxBytes 134217728 step: 2(factor) warmup iters: 10 iters: 50 agg iters: 1 validation: 1 graph: 0
[1,0]<stdout>:#
[1,0]<stdout>:# Using devices
[1,0]<stdout>:#  Rank  0 Group  0 Pid 3000147 on        lab device  0 [0000:02:00] NVIDIA GeForce RTX 5090
[1,0]<stdout>:#  Rank  1 Group  0 Pid 3000148 on        lab device  0 [0000:02:00] NVIDIA GeForce RTX 5090
[1,0]<stdout>:#
[1,0]<stdout>:#                                                              out-of-place                       in-place          
[1,0]<stdout>:#       size         count      type   redop    root     time   algbw   busbw  #wrong     time   algbw   busbw  #wrong 
[1,0]<stdout>:#        (B)    (elements)                               (us)  (GB/s)  (GB/s)             (us)  (GB/s)  (GB/s)         
[1,0]<stdout>:     1048576        131072     float    none      -1  2190.31    0.48    0.24       0  2189.09    0.48    0.24       0
[1,0]<stdout>:     2097152        262144     float    none      -1  2190.85    0.96    0.48       0  2190.59    0.96    0.48       0
[1,0]<stdout>:     4194304        524288     float    none      -1  2204.73    1.90    0.95       0  2194.36    1.91    0.96       0
[1,0]<stdout>:     8388608       1048576     float    none      -1  2383.97    3.52    1.76       0  2396.91    3.50    1.75       0
[1,0]<stdout>:    16777216       2097152     float    none      -1  3772.28    4.45    2.22       0  3784.25    4.43    2.22       0
[1,0]<stdout>:    33554432       4194304     float    none      -1  7476.19    4.49    2.24       0  7504.53    4.47    2.24       0
[1,0]<stdout>:    67108864       8388608     float    none      -1  14626.4    4.59    2.29       0  14607.1    4.59    2.30       0
[1,0]<stdout>:   134217728      16777216     float    none      -1  29217.3    4.59    2.30       0  29083.8    4.61    2.31       0
[1,0]<stdout>:# Out of bounds values : 0 OK
[1,0]<stdout>:# Avg bus bandwidth    : 1.5605 
[1,0]<stdout>:#
[1,0]<stdout>:# Collective test concluded: all_gather_perf_mpi
[1,0]<stdout>:#
[1,0]<stdout>:
```

### Config B: NCCL_PROTO=Simple

Command:
```bash
mpirun --oversubscribe --tag-output \
  -np 1 /usr/bin/env \
    LD_LIBRARY_PATH=/home/yunwei37/workspace/nccl-eBPF/nccl/build/lib \
    NCCL_TESTS_DEVICE=0 NCCL_P2P_DISABLE=1 NCCL_SHM_DISABLE=1 \
    NCCL_NET=Socket NCCL_HOSTID=st-exp2b-rank0 \
    NCCL_PROTO=Simple \
    /home/yunwei37/workspace/nccl-eBPF/nccl-tests/build/all_gather_perf_mpi \
      -b 1048576 -e 134217728 -f 2 -g 1 -n 50 -w 10 \
  : \
  -np 1 /usr/bin/env \
    LD_LIBRARY_PATH=/home/yunwei37/workspace/nccl-eBPF/nccl/build/lib \
    NCCL_TESTS_DEVICE=0 NCCL_P2P_DISABLE=1 NCCL_SHM_DISABLE=1 \
    NCCL_NET=Socket NCCL_HOSTID=st-exp2b-rank1 \
    NCCL_PROTO=Simple \
    /home/yunwei37/workspace/nccl-eBPF/nccl-tests/build/all_gather_perf_mpi \
      -b 1048576 -e 134217728 -f 2 -g 1 -n 50 -w 10
```

#### Rep 1: `exp2-config-b-simple-rep1.log`

```text
Authorization required, but no authorization protocol specified

Authorization required, but no authorization protocol specified

[1,1]<stderr>:Authorization required, but no authorization protocol specified
[1,1]<stderr>:
[1,0]<stderr>:Authorization required, but no authorization protocol specified
[1,0]<stderr>:
[1,0]<stdout>:# nccl-tests version 2.18.0 nccl-headers=22907 nccl-library=22907
[1,0]<stdout>:# Collective test starting: all_gather_perf_mpi
[1,0]<stdout>:# nThread 1 nGpus 1 minBytes 1048576 maxBytes 134217728 step: 2(factor) warmup iters: 10 iters: 50 agg iters: 1 validation: 1 graph: 0
[1,0]<stdout>:#
[1,0]<stdout>:# Using devices
[1,0]<stdout>:#  Rank  0 Group  0 Pid 3000359 on        lab device  0 [0000:02:00] NVIDIA GeForce RTX 5090
[1,0]<stdout>:#  Rank  1 Group  0 Pid 3000360 on        lab device  0 [0000:02:00] NVIDIA GeForce RTX 5090
[1,0]<stdout>:#
[1,0]<stdout>:#                                                              out-of-place                       in-place          
[1,0]<stdout>:#       size         count      type   redop    root     time   algbw   busbw  #wrong     time   algbw   busbw  #wrong 
[1,0]<stdout>:#        (B)    (elements)                               (us)  (GB/s)  (GB/s)             (us)  (GB/s)  (GB/s)         
[1,0]<stdout>:     1048576        131072     float    none      -1  2187.10    0.48    0.24       0  2188.30    0.48    0.24       0
[1,0]<stdout>:     2097152        262144     float    none      -1  2191.15    0.96    0.48       0  2190.67    0.96    0.48       0
[1,0]<stdout>:     4194304        524288     float    none      -1  2194.53    1.91    0.96       0  2201.43    1.91    0.95       0
[1,0]<stdout>:     8388608       1048576     float    none      -1  2352.26    3.57    1.78       0  2354.94    3.56    1.78       0
[1,0]<stdout>:    16777216       2097152     float    none      -1  3744.79    4.48    2.24       0  3746.37    4.48    2.24       0
[1,0]<stdout>:    33554432       4194304     float    none      -1  7660.13    4.38    2.19       0  7464.34    4.50    2.25       0
[1,0]<stdout>:    67108864       8388608     float    none      -1  14923.9    4.50    2.25       0  14950.7    4.49    2.24       0
[1,0]<stdout>:   134217728      16777216     float    none      -1  29255.9    4.59    2.29       0  29479.4    4.55    2.28       0
[1,0]<stdout>:# Out of bounds values : 0 OK
[1,0]<stdout>:# Avg bus bandwidth    : 1.55556 
[1,0]<stdout>:#
[1,0]<stdout>:# Collective test concluded: all_gather_perf_mpi
[1,0]<stdout>:#
[1,0]<stdout>:
```

#### Rep 2: `exp2-config-b-simple-rep2.log`

```text
Authorization required, but no authorization protocol specified

Authorization required, but no authorization protocol specified

[1,0]<stderr>:Authorization required, but no authorization protocol specified
[1,0]<stderr>:
[1,1]<stderr>:Authorization required, but no authorization protocol specified
[1,1]<stderr>:
[1,0]<stdout>:# nccl-tests version 2.18.0 nccl-headers=22907 nccl-library=22907
[1,0]<stdout>:# Collective test starting: all_gather_perf_mpi
[1,0]<stdout>:# nThread 1 nGpus 1 minBytes 1048576 maxBytes 134217728 step: 2(factor) warmup iters: 10 iters: 50 agg iters: 1 validation: 1 graph: 0
[1,0]<stdout>:#
[1,0]<stdout>:# Using devices
[1,0]<stdout>:#  Rank  0 Group  0 Pid 3000576 on        lab device  0 [0000:02:00] NVIDIA GeForce RTX 5090
[1,0]<stdout>:#  Rank  1 Group  0 Pid 3000577 on        lab device  0 [0000:02:00] NVIDIA GeForce RTX 5090
[1,0]<stdout>:#
[1,0]<stdout>:#                                                              out-of-place                       in-place          
[1,0]<stdout>:#       size         count      type   redop    root     time   algbw   busbw  #wrong     time   algbw   busbw  #wrong 
[1,0]<stdout>:#        (B)    (elements)                               (us)  (GB/s)  (GB/s)             (us)  (GB/s)  (GB/s)         
[1,0]<stdout>:     1048576        131072     float    none      -1  2189.66    0.48    0.24       0  2186.59    0.48    0.24       0
[1,0]<stdout>:     2097152        262144     float    none      -1  2192.53    0.96    0.48       0  2194.60    0.96    0.48       0
[1,0]<stdout>:     4194304        524288     float    none      -1  2205.18    1.90    0.95       0  2204.75    1.90    0.95       0
[1,0]<stdout>:     8388608       1048576     float    none      -1  2299.05    3.65    1.82       0  2304.70    3.64    1.82       0
[1,0]<stdout>:    16777216       2097152     float    none      -1  3681.20    4.56    2.28       0  3773.04    4.45    2.22       0
[1,0]<stdout>:    33554432       4194304     float    none      -1  7607.28    4.41    2.21       0  7515.37    4.46    2.23       0
[1,0]<stdout>:    67108864       8388608     float    none      -1  14860.2    4.52    2.26       0  14724.8    4.56    2.28       0
[1,0]<stdout>:   134217728      16777216     float    none      -1  28905.3    4.64    2.32       0  29216.6    4.59    2.30       0
[1,0]<stdout>:# Out of bounds values : 0 OK
[1,0]<stdout>:# Avg bus bandwidth    : 1.56731 
[1,0]<stdout>:#
[1,0]<stdout>:# Collective test concluded: all_gather_perf_mpi
[1,0]<stdout>:#
[1,0]<stdout>:
```

#### Rep 3: `exp2-config-b-simple-rep3.log`

```text
Authorization required, but no authorization protocol specified

Authorization required, but no authorization protocol specified

[1,0]<stderr>:Authorization required, but no authorization protocol specified
[1,0]<stderr>:
[1,1]<stderr>:Authorization required, but no authorization protocol specified
[1,1]<stderr>:
[1,0]<stdout>:# nccl-tests version 2.18.0 nccl-headers=22907 nccl-library=22907
[1,0]<stdout>:# Collective test starting: all_gather_perf_mpi
[1,0]<stdout>:# nThread 1 nGpus 1 minBytes 1048576 maxBytes 134217728 step: 2(factor) warmup iters: 10 iters: 50 agg iters: 1 validation: 1 graph: 0
[1,0]<stdout>:#
[1,0]<stdout>:# Using devices
[1,0]<stdout>:#  Rank  0 Group  0 Pid 3000668 on        lab device  0 [0000:02:00] NVIDIA GeForce RTX 5090
[1,0]<stdout>:#  Rank  1 Group  0 Pid 3000669 on        lab device  0 [0000:02:00] NVIDIA GeForce RTX 5090
[1,0]<stdout>:#
[1,0]<stdout>:#                                                              out-of-place                       in-place          
[1,0]<stdout>:#       size         count      type   redop    root     time   algbw   busbw  #wrong     time   algbw   busbw  #wrong 
[1,0]<stdout>:#        (B)    (elements)                               (us)  (GB/s)  (GB/s)             (us)  (GB/s)  (GB/s)         
[1,0]<stdout>:     1048576        131072     float    none      -1  2190.17    0.48    0.24       0  2189.44    0.48    0.24       0
[1,0]<stdout>:     2097152        262144     float    none      -1  2190.33    0.96    0.48       0  2192.63    0.96    0.48       0
[1,0]<stdout>:     4194304        524288     float    none      -1  2196.65    1.91    0.95       0  2205.90    1.90    0.95       0
[1,0]<stdout>:     8388608       1048576     float    none      -1  2280.47    3.68    1.84       0  2424.55    3.46    1.73       0
[1,0]<stdout>:    16777216       2097152     float    none      -1  3807.65    4.41    2.20       0  3771.45    4.45    2.22       0
[1,0]<stdout>:    33554432       4194304     float    none      -1  7492.79    4.48    2.24       0  7426.69    4.52    2.26       0
[1,0]<stdout>:    67108864       8388608     float    none      -1  14776.0    4.54    2.27       0  15246.4    4.40    2.20       0
[1,0]<stdout>:   134217728      16777216     float    none      -1  29073.8    4.62    2.31       0  29330.0    4.58    2.29       0
[1,0]<stdout>:# Out of bounds values : 0 OK
[1,0]<stdout>:# Avg bus bandwidth    : 1.55649 
[1,0]<stdout>:#
[1,0]<stdout>:# Collective test concluded: all_gather_perf_mpi
[1,0]<stdout>:#
[1,0]<stdout>:
```
