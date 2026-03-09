# Revise #2 Results

Date: 2026-03-09

Build:
`cmake --build src/nccl-policy-plugin/build-bpftime-migration-prebuilt -j24`

Run:
`SPDLOG_LEVEL=off src/nccl-policy-plugin/build-bpftime-migration-prebuilt/test_ebpf_plugin`

Raw harness log:
`docs/tmp/revise2-harness-output.txt`

## Part A: CPU overhead breakdown

Order required by Review #2:

| Policy | P50 (ns) | P99 (ns) | Max (ns) | Delta vs native P50 (ns) |
| --- | ---: | ---: | ---: | ---: |
| native | 10 | 16 | 43001 | 0 |
| noop | 51 | 61 | 5708 | +41 |
| size_aware_v2 | 52 | 64 | 3129 | +42 |
| lookup_only | 63 | 74 | 3681 | +53 |
| lookup_update | 74 | 87 | 6165 | +64 |
| adaptive_channels | 75 | 88 | 16176 | +65 |
| slo_enforcer | 80 | 95 | 6363 | +70 |

Staircase decomposition from the P50s:

- native -> noop: +41 ns
- noop -> size_aware_v2: +1 ns
- size_aware_v2 -> lookup_only: +11 ns
- lookup_only -> lookup_update: +11 ns
- lookup_update -> adaptive_channels: +1 ns
- adaptive_channels -> slo_enforcer: +5 ns

Interpretation:

- Context reads + branching remain effectively free relative to the plugin dispatch baseline (`51 ns` -> `52 ns`).
- One telemetry map lookup adds about `11 ns` at P50.
- Adding one update on top of the lookup adds another `11 ns` at P50.
- `adaptive_channels` is essentially equal to `lookup_update` in this synthetic microbenchmark.
- `slo_enforcer` remains the most expensive path, but still stays at `80 ns` P50 and `95 ns` P99.

## Part B: hot-reload latency under active traffic

Implementation note:

- `src/nccl-policy-plugin/plugin.cpp` now keeps the loaded policy in an immutable `LoadedPolicyState`.
- Reload prepares a fresh state off-path, then atomically swaps a `shared_ptr` to it.
- In-flight calls keep the old state alive until they finish; there was no call loss.

Measured result:

- Reload load time: `11056.287 us`
- Atomic swap window: `0.309 us`
- End-to-end reload time: `11056.607 us`
- Pre-reload call latency: `P50=114 ns`, `P99=142 ns`
- Max call latency during test: `110378 ns`
- Calls above `10 us`: `3`
- Completed calls: `400000 / 400000`
- Failed calls: `0`
- Zero-call-loss: `yes`
- Reload trigger point: call `200000`
- First observed post-swap behavior: call `298126`
- Post-reload decision: `channels=10`, `algo=ring`, `proto=simple`

Interpretation:

- The swap itself is sub-microsecond.
- The expensive part is loading/verifying/JITing the replacement policy (`~11.06 ms`).
- During that preparation interval, the old policy continues serving calls; once the swap happens, behavior changes correctly without dropping calls.
- There were a few long-tail call-latency outliers (`3` calls > `10 us`), but not a global stall and not any lost calls.

## Part C: adaptive policy with synthetic telemetry

Setup:

- Seeded `telemetry_map` for `coll_type=ALLREDUCE`, `n_nodes=1`.
- First `500K` calls used synthetic low latency (`100 ns`), which drives channel increases.
- Next `500K` calls used synthetic high latency (`10000 ns`), which drives channel decreases.
- Sampled the adaptation state every `10K` calls.

Boundary check:

- Channel count immediately before boundary: `12`
- Channel count immediately after boundary: `11`

Observed behavior:

- Low-latency phase saturates at `12` channels.
- High-latency phase steps down `12 -> 11 -> 10 -> ... -> 1` across the first `110K` calls after the boundary.
- After call `610000`, the policy stays pinned at `1` channel for the rest of the run.

Sampled curve (`call,phase,channels,map_channels`):

```text
10000,low,12,12
20000,low,12,12
30000,low,12,12
40000,low,12,12
50000,low,12,12
60000,low,12,12
70000,low,12,12
80000,low,12,12
90000,low,12,12
100000,low,12,12
110000,low,12,12
120000,low,12,12
130000,low,12,12
140000,low,12,12
150000,low,12,12
160000,low,12,12
170000,low,12,12
180000,low,12,12
190000,low,12,12
200000,low,12,12
210000,low,12,12
220000,low,12,12
230000,low,12,12
240000,low,12,12
250000,low,12,12
260000,low,12,12
270000,low,12,12
280000,low,12,12
290000,low,12,12
300000,low,12,12
310000,low,12,12
320000,low,12,12
330000,low,12,12
340000,low,12,12
350000,low,12,12
360000,low,12,12
370000,low,12,12
380000,low,12,12
390000,low,12,12
400000,low,12,12
410000,low,12,12
420000,low,12,12
430000,low,12,12
440000,low,12,12
450000,low,12,12
460000,low,12,12
470000,low,12,12
480000,low,12,12
490000,low,12,12
500000,low,12,12
510000,high,11,11
520000,high,10,10
530000,high,9,9
540000,high,8,8
550000,high,7,7
560000,high,6,6
570000,high,5,5
580000,high,4,4
590000,high,3,3
600000,high,2,2
610000,high,1,1
620000,high,1,1
630000,high,1,1
640000,high,1,1
650000,high,1,1
660000,high,1,1
670000,high,1,1
680000,high,1,1
690000,high,1,1
700000,high,1,1
710000,high,1,1
720000,high,1,1
730000,high,1,1
740000,high,1,1
750000,high,1,1
760000,high,1,1
770000,high,1,1
780000,high,1,1
790000,high,1,1
800000,high,1,1
810000,high,1,1
820000,high,1,1
830000,high,1,1
840000,high,1,1
850000,high,1,1
860000,high,1,1
870000,high,1,1
880000,high,1,1
890000,high,1,1
900000,high,1,1
910000,high,1,1
920000,high,1,1
930000,high,1,1
940000,high,1,1
950000,high,1,1
960000,high,1,1
970000,high,1,1
980000,high,1,1
990000,high,1,1
1000000,high,1,1
```

## Part D: config_map array-map optimization

Change made:

- `slo_enforcer.bpf.c` now uses `BPF_MAP_TYPE_ARRAY` for `config_map`, keyed by `coll_type`.

Observed end-to-end result:

- `slo_enforcer` now measures `P50=80 ns`, `P99=95 ns`.
- Review #2 had `slo_enforcer` at roughly `81 ns` P50.

Conclusion:

- The array-map version is not measurably worse, but the end-to-end improvement is negligible (`~1 ns` at P50, likely within noise).
- The main overhead story remains the telemetry map helper path, not config lookup logic.

## Files changed

- `src/ebpf-policies/lookup_only.bpf.c`
- `src/ebpf-policies/lookup_update.bpf.c`
- `src/ebpf-policies/slo_enforcer.bpf.c`
- `src/nccl-policy-plugin/CMakeLists.txt`
- `src/nccl-policy-plugin/plugin.cpp`
- `src/nccl-policy-plugin/test/policy_test_paths.h.in`
- `src/nccl-policy-plugin/test/test_ebpf_plugin.c`
