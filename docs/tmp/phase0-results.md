# Phase 0 Results

All Phase 0 steps completed successfully in order. No failure was encountered, so execution proceeded through step 6.

## 1. Build NCCL core library

- Status: `SUCCESS`
- Command: `make -j24 src.build`
- Workdir: `/home/yunwei37/workspace/nccl-eBPF/nccl`
- Output path:
  - Symlink: `/home/yunwei37/workspace/nccl-eBPF/nccl/build/lib/libnccl.so`
  - Real file: `/home/yunwei37/workspace/nccl-eBPF/nccl/build/lib/libnccl.so.2.29.7`
- Log: `/home/yunwei37/workspace/nccl-eBPF/docs/tmp/phase0-logs/step1-nccl-build.log`

## 2. Build example tuner plugin

- Status: `SUCCESS`
- Command: `make -C nccl/plugins/tuner/example`
- Output `.so` path: `/home/yunwei37/workspace/nccl-eBPF/nccl/plugins/tuner/example/libnccl-tuner-example.so`
- Log: `/home/yunwei37/workspace/nccl-eBPF/docs/tmp/phase0-logs/step2-tuner-plugin.log`

## 3. Build and run CPU-only tuner plugin test

- Status: `SUCCESS`
- Build method: project Makefile in `/home/yunwei37/workspace/nccl-eBPF/nccl/plugins/tuner/example/test`
- Build command: `make`
- Run command: `./test_plugin`
- Log files:
  - Build: `/home/yunwei37/workspace/nccl-eBPF/docs/tmp/phase0-logs/step3-test-build.log`
  - Run: `/home/yunwei37/workspace/nccl-eBPF/docs/tmp/phase0-logs/step3-test-run.log`

Output:

```text
Running NCCL Tuner Plugin Unit Tests
=====================================
Running test: init
PASS: test_plugin_init
Running test: config-valid
PASS: test_config_parsing_valid
Running test: config-invalid
PASS: test_config_parsing_invalid
Running test: collective
PASS: test_collective_matching
Running test: size
PASS: test_size_matching
Running test: topology
PASS: test_topology_matching
Running test: channels
PASS: test_default_channels
Running test: regbuff
PASS: test_regbuff_matching
Running test: pipeops
PASS: test_pipeops_matching
Running test: fallback
PASS: test_no_match_fallback
Running test: large-config
PASS: test_large_config
Running test: stress-config
PASS: test_very_large_config_stress
Running test: empty-config
PASS: test_empty_config
Running test: nvl-domain
Testing NVLink domain info handling...
NVLink domain info test passed!
PASS: test_nvl_domain_info
Running test: constants
PASS: test_tuner_constants

=====================================
Test Results: 15/15 tests passed
All tests PASSED!
```

## 4. Clone/build nccl-tests

- Clone status: repo already existed at `/home/yunwei37/workspace/nccl-eBPF/nccl-tests`, so no clone was needed.
- Build status: `SUCCESS`
- Build command: `make -j24 NCCL_HOME=/home/yunwei37/workspace/nccl-eBPF/nccl/build MPI=1`
- Built binary: `/home/yunwei37/workspace/nccl-eBPF/nccl-tests/build/all_reduce_perf`
- Log: `/home/yunwei37/workspace/nccl-eBPF/docs/tmp/phase0-logs/step4-nccl-tests-build.log`

## 5. Quick single-GPU nccl-tests run

- Status: `SUCCESS`
- Command:
  - `NCCL_DEBUG=INFO LD_LIBRARY_PATH=/home/yunwei37/workspace/nccl-eBPF/nccl/build/lib ./nccl-tests/build/all_reduce_perf -b 8 -e 128M -f 2 -g 1 -n 5`
- GPU detected: `NVIDIA GeForce RTX 5090`
- Log: `/home/yunwei37/workspace/nccl-eBPF/docs/tmp/phase0-logs/step5-single-gpu.log`

First 50 lines:

```text
# nccl-tests version 2.18.0 nccl-headers=22907 nccl-library=22907
# Collective test starting: all_reduce_perf
# nThread 1 nGpus 1 minBytes 8 maxBytes 134217728 step: 2(factor) warmup iters: 1 iters: 5 agg iters: 1 validation: 1 graph: 0
#
# Using devices
#  Rank  0 Group  0 Pid 1425048 on        lab device  0 [0000:02:00] NVIDIA GeForce RTX 5090
lab:1425048:1425048 [0] NCCL INFO ENV/Plugin: Could not find: libnccl-env.so
lab:1425048:1425048 [0] NCCL INFO Bootstrap: Using enp129s0:128.114.59.195<0>
lab:1425048:1425048 [0] NCCL INFO cudaDriverVersion 12090
lab:1425048:1425048 [0] NCCL INFO NCCL version 2.29.7+cuda12.9
lab:1425048:1425048 [0] NCCL INFO NCCL git version master 3619159
lab:1425048:1425048 [0] NCCL INFO Comm config nvlinkCentricSched set to 1
lab:1425048:1425048 [0] NCCL INFO NET/Plugin: Could not find: libnccl-net.so
lab:1425048:1425048 [0] NCCL INFO NET/IB : No device found.
lab:1425048:1425048 [0] NCCL INFO NET/IB : Using [RO]; OOB enp129s0:128.114.59.195<0>
lab:1425048:1425048 [0] NCCL INFO Failed to initialize NET plugin IB
lab:1425048:1425048 [0] NCCL INFO NET/Socket : Using [0]enp129s0:128.114.59.195<0> [1]wlp128s20f3:169.233.112.15<0> [2]tailscale0:fe80::56c4:f535:9d9e:1004%tailscale0<0>
lab:1425048:1425048 [0] NCCL INFO Initialized NET plugin Socket
lab:1425048:1425048 [0] NCCL INFO Assigned NET plugin Socket to comm
lab:1425048:1425048 [0] NCCL INFO GIN/Plugin: Could not find: libnccl-gin.so
lab:1425048:1425048 [0] NCCL INFO Failed to initialize any GIN plugin
lab:1425048:1425048 [0] NCCL INFO Using network Socket
lab:1425048:1425048 [0] NCCL INFO [Rank 0] ncclCommInitRankConfig comm 0x5967f7993f30 rank 0 nranks 1 cudaDev 0 nvmlDev 0 busId 2000 commId 0x9557dc94fa58b484 - Init START
lab:1425048:1425048 [0] NCCL INFO RAS client listening socket at 127.0.0.1<28028>
lab:1425048:1425048 [0] NCCL INFO Bootstrap timings total 0.000643 (create 0.000018, send 0.000067, recv 0.000400, ring 0.000001, delay 0.000000)
lab:1425048:1425048 [0] NCCL INFO ncclTopoGetCpuAffinity: Affinity for GPU 0 is empty, ignoring. (GPU affinity =  ; CPU affinity = 0-23).
lab:1425048:1425048 [0] NCCL INFO comm 0x5967f7993f30 rank 0 nRanks 1 nNodes 1 localRanks 1 localRank 0 MNNVL 0
lab:1425048:1425048 [0] NCCL INFO Channel 00/64 : 0
lab:1425048:1425048 [0] NCCL INFO Channel 01/64 : 0
lab:1425048:1425048 [0] NCCL INFO Channel 02/64 : 0
lab:1425048:1425048 [0] NCCL INFO Channel 03/64 : 0
lab:1425048:1425048 [0] NCCL INFO Channel 04/64 : 0
lab:1425048:1425048 [0] NCCL INFO Channel 05/64 : 0
lab:1425048:1425048 [0] NCCL INFO Channel 06/64 : 0
lab:1425048:1425048 [0] NCCL INFO Channel 07/64 : 0
lab:1425048:1425048 [0] NCCL INFO Channel 08/64 : 0
lab:1425048:1425048 [0] NCCL INFO Channel 09/64 : 0
lab:1425048:1425048 [0] NCCL INFO Channel 10/64 : 0
lab:1425048:1425048 [0] NCCL INFO Channel 11/64 : 0
lab:1425048:1425048 [0] NCCL INFO Channel 12/64 : 0
lab:1425048:1425048 [0] NCCL INFO Channel 13/64 : 0
lab:1425048:1425048 [0] NCCL INFO Channel 14/64 : 0
lab:1425048:1425048 [0] NCCL INFO Channel 15/64 : 0
lab:1425048:1425048 [0] NCCL INFO Channel 16/64 : 0
lab:1425048:1425048 [0] NCCL INFO Channel 17/64 : 0
lab:1425048:1425048 [0] NCCL INFO Channel 18/64 : 0
lab:1425048:1425048 [0] NCCL INFO Channel 19/64 : 0
lab:1425048:1425048 [0] NCCL INFO Channel 20/64 : 0
lab:1425048:1425048 [0] NCCL INFO Channel 21/64 : 0
lab:1425048:1425048 [0] NCCL INFO Channel 22/64 : 0
```

Last 20 lines:

```text
       65536         16384     float     sum      -1     2.84   23.06    0.00       0     0.10  631.37    0.00       0
      131072         32768     float     sum      -1     2.95   44.44    0.00       0     0.11  1191.6    0.00       0
      262144         65536     float     sum      -1     3.08   85.18    0.00       0     0.10  2713.7    0.00       0
      524288        131072     float     sum      -1     3.56  147.41    0.00       0     0.10  5416.2    0.00       0
     1048576        262144     float     sum      -1     4.08  256.99    0.00       0     0.09   11061    0.00       0
     2097152        524288     float     sum      -1     4.24  494.87    0.00       0     0.09   22358    0.00       0
     4194304       1048576     float     sum      -1     5.60  748.56    0.00       0     0.10   42452    0.00       0
     8388608       2097152     float     sum      -1     6.92  1212.6    0.00       0     0.17   48828    0.00       0
    16777216       4194304     float     sum      -1    14.86  1128.7    0.00       0     0.09  183157    0.00       0
    33554432       8388608     float     sum      -1    34.55  971.32    0.00       0     0.10  348075    0.00       0
    67108864      16777216     float     sum      -1    81.21  826.34    0.00       0     0.10  656643    0.00       0
   134217728      33554432     float     sum      -1   176.33  761.18    0.00       0     0.10   1e+06    0.00       0
lab:1425048:1425048 [0] NCCL INFO comm 0x5967f7993f30 rank 0 nranks 1 cudaDev 0 busId 2000 - Destroy COMPLETE
# Out of bounds values : 0 OK
# Avg bus bandwidth    : 0 
#
# Collective test concluded: all_reduce_perf
#

lab:1425048:1425048 [0] NCCL INFO ENV/Plugin: Closing env plugin ncclEnvDefault
```

## 6. Plugin loading test

- Status: `SUCCESS`
- Command:
  - `NCCL_DEBUG=INFO NCCL_TUNER_PLUGIN=/home/yunwei37/workspace/nccl-eBPF/nccl/plugins/tuner/example/libnccl-tuner-example.so LD_LIBRARY_PATH=/home/yunwei37/workspace/nccl-eBPF/nccl/build/lib ./nccl-tests/build/all_reduce_perf -b 1M -e 1M -g 1 -n 2`
- Plugin loaded: `YES`
- Log: `/home/yunwei37/workspace/nccl-eBPF/docs/tmp/phase0-logs/step6-plugin-load.log`

Relevant tuner log lines:

```text
lab:1425120:1425120 [0] NCCL INFO NCCL_TUNER_PLUGIN set by environment to /home/yunwei37/workspace/nccl-eBPF/nccl/plugins/tuner/example/libnccl-tuner-example.so
lab:1425120:1425120 [0] NCCL INFO TUNER/Plugin: Using Example (v5)
lab:1425120:1425120 [0] NCCL INFO Successfully loaded external tuner plugin /home/yunwei37/workspace/nccl-eBPF/nccl/plugins/tuner/example/libnccl-tuner-example.so
lab:1425120:1425120 [0] NCCL INFO TUNER/Plugin: Closing tuner: 'Example'
```

## Overall Summary

- NCCL core library built successfully.
- Example tuner plugin built successfully.
- CPU-only plugin unit test passed: `15/15 tests passed`.
- `nccl-tests` was already cloned and built successfully against the local NCCL build.
- Single-GPU `all_reduce_perf` run completed successfully.
- External tuner plugin loaded successfully in NCCL.
