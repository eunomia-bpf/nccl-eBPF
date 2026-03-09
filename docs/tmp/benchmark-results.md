# Comprehensive Performance Testing Results

Date: 2026-03-09

Environment:
- Plugin rebuild: `src/nccl-policy-plugin/build-bpftime-migration-prebuilt`
- NCCL: `nccl/build/lib/libnccl.so` (2.29.7)
- nccl-tests: `nccl-tests/build/all_reduce_perf`
- Hardware: 1x NVIDIA GeForce RTX 5090, 24 CPU cores, CUDA 12.9
- Raw logs: `docs/tmp/benchmarks/`
- CSV for figures: `docs/tmp/benchmark-latency.csv`

## CPU Microbenchmark

Raw output: `docs/tmp/benchmarks/cpu_microbenchmark.txt`

`SPDLOG_LEVEL=off` was used for the harness. `bpftime` still emitted SHM info lines, but the benchmark result lines were captured intact.

| Case | Verify mode | P50 (ns) | P99 (ns) | Max (ns) | Delta P50 vs native (ns) | Delta P99 vs native (ns) | Decision |
| --- | --- | ---: | ---: | ---: | ---: | ---: | --- |
| native baseline | n/a | 10 | 16 | 1622 | 0 | 0 | channels=2 algo=0 proto=2 |
| noop | strict | 43 | 51 | 2985 | 33 | 35 | channels=1 algo=-1 proto=-1 |
| size_aware | strict | 43 | 52 | 12875 | 33 | 36 | channels=2 algo=0 proto=2 |
| size_aware_v2 | strict | 44 | 53 | 3257 | 34 | 37 | channels=2 algo=0 proto=2 |
| adaptive_channels | strict | 66 | 76 | 14284 | 56 | 60 | channels=4 algo=-1 proto=-1 |
| slo_enforcer | strict | 81 | 93 | 4875 | 71 | 77 | channels=1 algo=0 proto=2 |

Validation and verifier checks:
- strict verifier acceptance: `PASS (5 strict policies)`
- `size_aware` correctness: `PASS`
- `adaptive_channels` map state: `PASS`
- verifier rejection negative test: `PASS` for `bad_lookup.bpf.o`

## Single-GPU Integration

Command shape for baseline/plugin runs:
- `all_reduce_perf -b 8 -e 128M -f 2 -g 1 -n 100 -w 5`

The table below uses the out-of-place `time (us)` column from `nccl-tests`.

| Message Size (B) | No Plugin (us) | Noop Plugin (us) | size_aware_v2 (us) | slo_enforcer (us) |
| ---: | ---: | ---: | ---: | ---: |
| 8 | 1.85 | 1.93 | 1.87 | 1.80 |
| 16 | 2.11 | 2.11 | 2.16 | 2.11 |
| 32 | 2.13 | 2.13 | 2.16 | 2.16 |
| 64 | 2.15 | 2.11 | 2.17 | 2.19 |
| 128 | 2.15 | 2.10 | 2.09 | 2.18 |
| 256 | 2.13 | 2.09 | 2.09 | 2.15 |
| 512 | 2.09 | 2.24 | 2.21 | 2.26 |
| 1024 | 2.16 | 2.21 | 2.13 | 2.09 |
| 2048 | 2.19 | 2.17 | 2.22 | 2.21 |
| 4096 | 2.17 | 2.11 | 2.15 | 2.21 |
| 8192 | 1.92 | 2.11 | 2.17 | 2.25 |
| 16384 | 2.09 | 2.10 | 2.19 | 2.24 |
| 32768 | 2.18 | 2.14 | 2.21 | 2.17 |
| 65536 | 2.12 | 2.20 | 2.27 | 2.18 |
| 131072 | 2.22 | 2.22 | 2.28 | 2.34 |
| 262144 | 2.34 | 2.34 | 2.31 | 2.42 |
| 524288 | 3.04 | 3.13 | 3.06 | 3.17 |
| 1048576 | 3.98 | 4.02 | 4.03 | 3.97 |
| 2097152 | 4.20 | 4.27 | 4.26 | 4.27 |
| 4194304 | 6.15 | 6.14 | 6.19 | 6.22 |
| 8388608 | 9.93 | 9.95 | 9.95 | 9.94 |
| 16777216 | 20.68 | 20.73 | 20.73 | 20.72 |
| 33554432 | 42.72 | 42.74 | 42.70 | 42.71 |
| 67108864 | 87.00 | 86.89 | 86.93 | 86.99 |
| 134217728 | 175.42 | 175.51 | 175.47 | 175.49 |

Raw outputs:
- `docs/tmp/benchmarks/gpu_baseline.txt`
- `docs/tmp/benchmarks/gpu_noop.txt`
- `docs/tmp/benchmarks/gpu_size_aware_v2.txt`
- `docs/tmp/benchmarks/gpu_slo_enforcer.txt`

## Multi-Thread Debug Attempt

Raw outputs:
- `docs/tmp/benchmarks/gpu_multithread_debug.txt`
- `docs/tmp/benchmarks/gpu_single_debug.txt`

Observed behavior:
- `-t 2 -g 1` failed before the benchmark ran: `Invalid number of GPUs: 2 requested but only 1 were found.`
- A logged single-thread rerun confirmed NCCL loaded the plugin successfully.
- The same logged single-thread rerun ended with `finalize calls=0`, so the tuner hook was not exercised on the 1-GPU path.

## CSV Data

CSV path: `docs/tmp/benchmark-latency.csv`

CSV columns:
- `message_size`
- `no_plugin_latency_us`
- `noop_latency_us`
- `size_aware_latency_us`

Note: `size_aware_latency_us` is populated from the `size_aware_v2.bpf.o` run, matching the requested Step 5 configuration.

## Analysis

CPU-side policy execution remains in the tens-of-nanoseconds range above the native baseline:
- `noop`: `+33ns` P50, `+35ns` P99
- `size_aware_v2`: `+34ns` P50, `+37ns` P99
- `adaptive_channels`: `+56ns` P50, `+60ns` P99
- `slo_enforcer`: `+71ns` P50, `+77ns` P99

Single-GPU NCCL end-to-end overhead is negligible in the collected measurements:
- Across all message sizes, average delta vs no plugin was `+0.0228us` for `noop`, `+0.0352us` for `size_aware_v2`, and `+0.0528us` for `slo_enforcer`.
- For message sizes `>= 1 MiB`, average delta stayed between `+0.0213us` and `+0.0288us`, all below `0.5%`.
- The largest observed absolute delta was `+0.33us` (`slo_enforcer` at `8192B`), still very small in absolute terms.

Conclusion:
- Yes, the observed overhead is negligible for the measured single-GPU collectives on this machine.
- Important caveat: on the 1-GPU `nccl-tests` path, the plugin loaded but reported `finalize calls=0`. That means these results demonstrate negligible integration/load overhead in this setup, but they do not demonstrate exercised `getCollInfo` runtime overhead inside NCCL.
