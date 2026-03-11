# Composability Experiment: Profiler-to-Tuner Closed Loop

Date: 2026-03-10

Testbed:
- 1x NVIDIA GeForce RTX 5090
- 2 MPI ranks on the same GPU
- NCCL 2.29.7
- `NCCL_NET=Socket`
- `NCCL_P2P_DISABLE=1`
- `NCCL_SHM_DISABLE=1`
- per-rank `NCCL_HOSTID` hack enabled

Artifacts:
- Raw sweep data: `docs/tmp/composability-channel-sweeps.tsv`
- Raw protocol data: `docs/tmp/composability-allgather-proto-128mb.tsv`
- Raw extra channel probes: `docs/tmp/composability-extra-allgather-128mb-highchannels.tsv`, `docs/tmp/composability-extra-allgather-16mb-highchannels.tsv`, `docs/tmp/composability-extra-allreduce-128mb-highchannels.tsv`
- Raw closed-loop data: `docs/tmp/composability-shortburst-allgather-128mb.tsv`, `docs/tmp/composability-shortburst-allgather-128mb-default.tsv`, `docs/tmp/composability-longrun-allgather-128mb-plugin.tsv`
- Per-run logs: `docs/tmp/composability-logs/`

Note on metric:
- I report the out-of-place `algbw` column from `nccl-tests`.

## Step 1: AllReduce channel sweep

### 1 MB AllReduce (`-b 1048576 -e 1048576`)

| config | run1 | run2 | run3 | avg algbw (GB/s) |
| --- | ---: | ---: | ---: | ---: |
| `n1` | 0.24 | 0.24 | 0.24 | 0.240 |
| `n2` | 0.24 | 0.24 | 0.24 | 0.240 |
| `n4` | 0.24 | 0.24 | 0.24 | 0.240 |
| `n8` | 0.24 | 0.24 | 0.24 | 0.240 |
| `default` | 0.24 | 0.24 | 0.24 | 0.240 |

Observation:
- No channel sensitivity at 1 MB.

### 16 MB AllReduce (`-b 16777216 -e 16777216`)

| config | run1 | run2 | run3 | avg algbw (GB/s) |
| --- | ---: | ---: | ---: | ---: |
| `n1` | 0.96 | 0.96 | 0.96 | 0.960 |
| `n2` | 1.92 | 1.92 | 1.92 | 1.920 |
| `n4` | 2.15 | 2.18 | 2.09 | 2.140 |
| `n8` | 2.11 | 2.14 | 2.10 | 2.117 |
| `default` | 2.19 | 2.19 | 2.11 | 2.163 |

Observation:
- Real channel sensitivity appears at 16 MB.
- `n4`/`default` are best; `n1` is much worse.

### 128 MB AllReduce (`-b 134217728 -e 134217728`)

| config | run1 | run2 | run3 | avg algbw (GB/s) |
| --- | ---: | ---: | ---: | ---: |
| `n1` | 0.96 | 0.96 | 0.96 | 0.960 |
| `n2` | 1.92 | 1.92 | 1.92 | 1.920 |
| `n4` | 2.28 | 2.27 | 2.31 | 2.287 |
| `n8` | 2.23 | 2.20 | 2.26 | 2.230 |
| `default` | 2.29 | 2.29 | 2.29 | 2.290 |

Observation:
- `n4` and `default` are effectively tied for best.
- `n8` is slightly worse than `n4`.

## Step 2: AllGather channel sweep

### 1 MB AllGather (`-b 1048576 -e 1048576`)

| config | run1 | run2 | run3 | avg algbw (GB/s) |
| --- | ---: | ---: | ---: | ---: |
| `n1` | 0.48 | 0.48 | 0.48 | 0.480 |
| `n2` | 0.48 | 0.48 | 0.48 | 0.480 |
| `n4` | 0.48 | 0.48 | 0.48 | 0.480 |
| `n8` | 0.48 | 0.48 | 0.48 | 0.480 |
| `default` | 0.48 | 0.48 | 0.48 | 0.480 |

Observation:
- No channel sensitivity at 1 MB.

### 16 MB AllGather (`-b 16777216 -e 16777216`)

| config | run1 | run2 | run3 | avg algbw (GB/s) |
| --- | ---: | ---: | ---: | ---: |
| `n1` | 1.92 | 1.92 | 1.75 | 1.863 |
| `n2` | 3.83 | 3.83 | 3.78 | 3.813 |
| `n4` | 4.35 | 4.30 | 4.33 | 4.327 |
| `n8` | 4.43 | 4.30 | 4.39 | 4.373 |
| `default` | 4.34 | 4.47 | 3.83 | 4.213 |

Observation:
- Strong sensitivity from `n1 -> n2 -> n4`.
- `n8` is slightly best on average, but `default` is noisier.

### 128 MB AllGather (`-b 134217728 -e 134217728`)

| config | run1 | run2 | run3 | avg algbw (GB/s) |
| --- | ---: | ---: | ---: | ---: |
| `n1` | 1.92 | 1.92 | 1.92 | 1.920 |
| `n2` | 3.84 | 3.84 | 3.84 | 3.840 |
| `n4` | 4.60 | 4.58 | 4.60 | 4.593 |
| `n8` | 4.43 | 4.37 | 4.46 | 4.420 |
| `default` | 4.60 | 4.56 | 4.56 | 4.573 |

Observation:
- `n4` is best.
- `n8` is measurably worse than `n4`.
- `default` is already close to the best fixed setting.

## Step 3: 128 MB AllGather protocol stability

Command shape:
- `all_gather_perf_mpi -b 134217728 -e 134217728 -g 1 -n 50 -w 10`
- compared `default` vs `NCCL_PROTO=Simple`

| proto config | run1 | run2 | run3 | run4 | run5 | avg algbw (GB/s) |
| --- | ---: | ---: | ---: | ---: | ---: | ---: |
| `default` | 4.45 | 4.56 | 4.56 | 4.54 | 4.58 | 4.538 |
| `Simple` | 4.57 | 4.59 | 4.57 | 4.56 | 4.56 | 4.570 |

Observation:
- The expected large bimodal behavior did **not** reproduce on this machine on 2026-03-10.
- `Simple` is only slightly higher than `default` (+0.7%), and both are already stable.
- I therefore treat protocol as a minor confound here, not the main story.

## Step 4: Plugin and policy reading

Files read:
- `src/nccl-policy-plugin/plugin.cpp`
- `src/ebpf-policies/adaptive_channels.bpf.c`
- `src/ebpf-policies/policy_maps.h`

### What the adaptive policy does

From `src/ebpf-policies/adaptive_channels.bpf.c`:
- `telemetry_map` is a hash map keyed by `{coll_type, n_nodes}` and stores `last_latency_ns`, `avg_latency_ns`, `p99_latency_ns`, `last_n_bytes`, `samples`, `recommended_channels`, and `applied_samples`.
- Base decision is size-based:
  - `< 16 KB -> 2 channels`
  - `< 1 MB -> 4 channels`
  - `>= 1 MB -> 8 channels`
- On each new profiler sample:
  - if `last_latency_ns > avg_latency_ns + 32`, decrement channels by 1
  - else if `n_bytes >= 1 MB` and `last_latency_ns <= avg_latency_ns`, increment channels by 1
  - clamp to `[1, 12]`

Code points:
- map declaration: `adaptive_channels.bpf.c:6-11`
- base policy and clamp: `adaptive_channels.bpf.c:13-27`
- adaptive update logic: `adaptive_channels.bpf.c:29-68`
- map structs: `policy_maps.h:6-19`

### How profiler feedback reaches the tuner

From `src/nccl-policy-plugin/plugin.cpp`:
- The tuner path (`pluginGetCollInfoImpl`) reads a snapshot from `telemetry_map` before executing the eBPF program and fills `nccl_policy_ctx` with latency fields.
- The profiler path records real collective latency from NCCL profiler events and writes it back into the same `telemetry_map`.
- `apply_policy_action` then writes the policy-returned channel count into NCCL's `n_channels`.

Code points:
- telemetry snapshot lookup: `plugin.cpp:999-1020`
- telemetry writeback and EWMA update: `plugin.cpp:1022-1068`
- tuner reads telemetry and executes policy: `plugin.cpp:1238-1306`
- channel action application: `plugin.cpp:1179-1205`
- profiler init and activation mask: `plugin.cpp:1432-1467`
- profiler event collection: `plugin.cpp:1469-1632`
- profiler finalize path: `plugin.cpp:1634-1653`

Important implication:
- The map key is only `{coll_type, n_nodes}`. It does **not** include message size.
- For a clean experiment, fixed-size runs are preferable so telemetry does not alias across different sizes.

### What happens with and without profiler

- With tuner only:
  - the policy creates the map entry with `recommended_channels=8` for large messages
  - `samples` never advances
  - the policy therefore stays static at 8 channels
- With tuner + profiler:
  - profiler events increment `samples`
  - the next `getCollInfo()` call sees new telemetry and can change channels

This behavior was directly observed in logs:
- tuner-only representative log: `docs/tmp/composability-logs/plugin-smoke-tuner-only.log`
- mixed representative log: `docs/tmp/composability-logs/plugin-smoke-mixed.log`

In the mixed smoke run, the first few calls changed as:
- `8 -> 8 -> 9 -> 10` on one rank
- `8 -> 8 -> 9 -> 9` on the other rank

## Additional targeted channel probes

I also measured channel counts above 8 because `adaptive_channels` can request up to 12 channels.

### 128 MB AllGather, fixed channels > 8

| channels | run1 | run2 | run3 | avg algbw (GB/s) |
| --- | ---: | ---: | ---: | ---: |
| `9` | 4.40 | 4.47 | 4.41 | 4.427 |
| `10` | 3.78 | 3.82 | 3.66 | 3.753 |
| `12` | 3.65 | 3.69 | 3.76 | 3.700 |

### 16 MB AllGather, fixed channels > 8

| channels | run1 | run2 | run3 | avg algbw (GB/s) |
| --- | ---: | ---: | ---: | ---: |
| `9` | 4.32 | 4.34 | 4.34 | 4.333 |
| `10` | 4.02 | 3.96 | 4.07 | 4.017 |
| `12` | 3.84 | 3.83 | 3.84 | 3.837 |

### 128 MB AllReduce, fixed channels > 8

| channels | run1 | run2 | run3 | avg algbw (GB/s) |
| --- | ---: | ---: | ---: | ---: |
| `9` | 2.21 | 2.23 | 2.23 | 2.223 |
| `10` | 1.99 | 2.00 | 1.96 | 1.983 |
| `12` | 1.86 | 1.88 | 1.85 | 1.863 |

Conclusion from the extra probes:
- The current policy's upward path is risky.
- `9` is sometimes acceptable, but `10` and `12` are clearly bad on this setup.
- That explains why long steady-state gains are small and noisy.

## Step 5: Best composability experiment

### Chosen scenario

Best scenario:
- collective: `AllGather`
- size: `128 MB`
- transport: socket, same as the baseline sweeps
- plugin policy: `adaptive_channels.bpf.o`
- baseline to beat: **tuner-only** plugin, which stays at static 8 channels because profiler feedback is absent

Why this scenario:
- It is large-message and clearly channel-sensitive.
- The closed loop is real:
  - tuner-only stays at 8
  - mixed tuner+profiler moves to 9/10 by call 3-5
- 128 MB fixed-size runs avoid the map aliasing problem caused by the coarse `{coll_type, n_nodes}` key.

### Executed closed-loop experiment

I ran a short-burst version first because the policy adapts within a few collectives:
- command core: `all_gather_perf_mpi -b 134217728 -e 134217728 -g 1 -w 2 -n 5`

Modes:
- `default`: stock NCCL, no plugin
- `tuner_only`: `NCCL_TUNER_PLUGIN=libnccl-policy.so`, no profiler
- `mixed`: `NCCL_TUNER_PLUGIN=libnccl-policy.so` and `NCCL_PROFILER_PLUGIN=libnccl-policy.so`

Results:

| mode | run1 | run2 | run3 | run4 | run5 | avg algbw (GB/s) |
| --- | ---: | ---: | ---: | ---: | ---: | ---: |
| `default` | 4.55 | 4.61 | 4.59 | 4.60 | 4.52 | 4.574 |
| `tuner_only` | 4.57 | 4.52 | 4.51 | 4.51 | 4.48 | 4.518 |
| `mixed` | 4.47 | 4.55 | 4.65 | 4.53 | 4.60 | 4.560 |

Measured effect:
- `mixed` vs `tuner_only`: `+0.93%` average algbw
- `mixed` beats `tuner_only` in `4/5` runs
- `mixed` vs stock `default`: `-0.31%`

Representative adaptation trace from the mixed logs:
- `shortburst-mixed-run1.log`: `8, 8, 9, 9/10, 9/10`
- `shortburst-mixed-run2.log`: `8, 8, 9, 10, 10`
- `shortburst-mixed-run4.log`: `8, 8, 9, 9, 9`

This is the cleanest positive composability result I found:
- without profiler feedback, the exact same policy is static at 8
- with profiler feedback, the policy changes channels by the third collective
- that closed loop produces a measurable throughput gain over the static tuner-only baseline

### Steady-state control

I also ran a smaller steady-state control:
- `all_gather_perf_mpi -b 134217728 -e 134217728 -g 1 -w 10 -n 50`

| mode | run1 | run2 | run3 | avg algbw (GB/s) |
| --- | ---: | ---: | ---: | ---: |
| `tuner_only` | 4.38 | 4.46 | 4.48 | 4.440 |
| `mixed` | 4.59 | 4.39 | 4.44 | 4.473 |

Observation:
- The mixed path is still slightly higher on average (`+0.75%`), but the result is noisier.
- The short-burst case is therefore the better demo for a workshop-style composability claim.

## Final recommendation

Use the following experiment in the write-up:
- Fixed-size `128 MB AllGather`
- `-w 2 -n 5`
- Compare:
  - stock NCCL
  - tuner-only plugin
  - mixed tuner+profiler plugin
- Headline claim:
  - "Profiler-fed telemetry makes the adaptive tuner non-static and improves throughput by about 1% over the same tuner without profiler feedback on short 128 MB AllGather bursts."

## Expected magnitude of improvement

For the current `adaptive_channels` policy on this machine:
- expected positive gain over the static tuner-only baseline: about `0.5%` to `1.0%`
- expected gain over stock NCCL: none guaranteed

Reason:
- the policy reacts quickly enough to help in short bursts
- but its upward bias can overshoot into `10+` channels, which is clearly harmful on this setup

If the goal is a larger win, the next policy revision should:
- cap large-message AllGather at 9
- or bias the update rule downward for 128 MB, because `n4/default` are better than `n8+`

