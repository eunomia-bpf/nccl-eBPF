# NCCLPol Evaluation Improvement Results

Run date: 2026-03-09

Workspace: `/home/yunwei37/workspace/nccl-eBPF`

Build command:

```bash
cmake --build src/nccl-policy-plugin/build-bpftime-migration-prebuilt -j24
```

Run command:

```bash
env SPDLOG_LEVEL=off stdbuf -oL -eL src/nccl-policy-plugin/build-bpftime-migration-prebuilt/test_ebpf_plugin | tee docs/tmp/eval-improvement-test-output.txt
```

Note: `SPDLOG_LEVEL=off` was set for the run. bpftime still emitted its own internal `[info]` lines to stdout/stderr, but the test binary completed successfully and the experiment tables below are copied from the captured output.

## Implementation note

To make Experiment 1 work with two simultaneous stateful communicator contexts in one process, the plugin needed one minimal core fix in `src/nccl-policy-plugin/plugin.cpp`: bpftime runtime map names are now namespaced per loaded policy instance. Without that change, the second communicator hit a bpftime shared-memory map-name collision when loading `slo_enforcer`.

## Experiment 1: Multi-Communicator Differentiated Policy

Observed output:

```text
multi-communicator differentiated policy:
same_process=yes policy=slo_enforcer calls_per_comm=100000 injected_p99_ns=5000000
config isolation: comm=1 target_p99_ns=1000000 comm=2 target_p99_ns=10000000
multi-communicator differentiation: PASS
```

Comparison table:

| message_bytes | comm=1 latency-sensitive target_p99=1ms | comm=2 throughput target_p99=10ms |
| --- | --- | --- |
| 1024 | algo=TREE proto=LL channels=8 | algo=TREE proto=SIMPLE channels=1 |
| 32768 | algo=TREE proto=LL channels=8 | algo=RING proto=SIMPLE channels=1 |
| 262144 | algo=RING proto=LL channels=8 | algo=RING proto=SIMPLE channels=1 |

Result: the same `slo_enforcer` eBPF policy produced different decisions for two communicator contexts in the same process after seeding per-communicator `config_map` state. The low-SLO communicator (`comm_id=1`, 1 ms target) converged to aggressive channel counts and LL protocol choices, while the throughput communicator (`comm_id=2`, 10 ms target) stayed at 1 channel and `SIMPLE` protocol.

## Experiment 2: GPU Contention Injection and Adaptive Response

Observed output:

```text
adaptive contention response:
phase_end_channels: baseline=12 contention=2 recovery=12 samples=30
adaptive contention response: PASS
```

Time series:

| sample | call_count | phase | injected_latency_ns | channels |
| --- | --- | --- | --- | --- |
| 1 | 10000 | baseline | 100 | 9 |
| 2 | 20000 | baseline | 100 | 10 |
| 3 | 30000 | baseline | 100 | 11 |
| 4 | 40000 | baseline | 100 | 12 |
| 5 | 50000 | baseline | 100 | 12 |
| 6 | 60000 | baseline | 100 | 12 |
| 7 | 70000 | baseline | 100 | 12 |
| 8 | 80000 | baseline | 100 | 12 |
| 9 | 90000 | baseline | 100 | 12 |
| 10 | 100000 | baseline | 100 | 12 |
| 11 | 110000 | contention | 10000 | 11 |
| 12 | 120000 | contention | 10000 | 10 |
| 13 | 130000 | contention | 10000 | 9 |
| 14 | 140000 | contention | 10000 | 8 |
| 15 | 150000 | contention | 10000 | 7 |
| 16 | 160000 | contention | 10000 | 6 |
| 17 | 170000 | contention | 10000 | 5 |
| 18 | 180000 | contention | 10000 | 4 |
| 19 | 190000 | contention | 10000 | 3 |
| 20 | 200000 | contention | 10000 | 2 |
| 21 | 210000 | recovery | 100 | 3 |
| 22 | 220000 | recovery | 100 | 4 |
| 23 | 230000 | recovery | 100 | 5 |
| 24 | 240000 | recovery | 100 | 6 |
| 25 | 250000 | recovery | 100 | 7 |
| 26 | 260000 | recovery | 100 | 8 |
| 27 | 270000 | recovery | 100 | 9 |
| 28 | 280000 | recovery | 100 | 10 |
| 29 | 290000 | recovery | 100 | 11 |
| 30 | 300000 | recovery | 100 | 12 |

Result: `adaptive_channels` showed the full closed loop requested in the demo. Channels rose to 12 under low latency, fell monotonically to 2 under injected contention, and recovered back to 12 once low latency was reintroduced.

## Experiment 3: Multi-Collective Type Coverage

Observed output:

```text
multi-collective coverage: PASS
```

Coverage table:

| collective | message_bytes | algo | proto | channels |
| --- | --- | --- | --- | --- |
| AllReduce | 1024 | TREE | SIMPLE | 2 |
| AllReduce | 32768 | TREE | LL | 4 |
| AllReduce | 1048576 | RING | SIMPLE | 10 |
| AllGather | 1024 | RING | SIMPLE | 2 |
| AllGather | 32768 | RING | LL | 4 |
| AllGather | 1048576 | RING | SIMPLE | 8 |
| ReduceScatter | 1024 | RING | SIMPLE | 2 |
| ReduceScatter | 32768 | RING | LL | 4 |
| ReduceScatter | 1048576 | RING | SIMPLE | 8 |
| Broadcast | 1024 | RING | SIMPLE | 2 |
| Broadcast | 32768 | RING | LL | 4 |
| Broadcast | 1048576 | RING | SIMPLE | 8 |

Result: the CPU harness now emits explicit policy behavior across `AllReduce`, `AllGather`, `ReduceScatter`, and `Broadcast`, including cases where `AllReduce` selects different algorithms or higher channel counts than the other collectives.

## Regression check

The full harness completed after the new experiments were added. Relevant existing summary lines from the same run:

```text
verifier matrix: PASS (14 programs)
size_aware correctness: PASS
adaptive_channels map state: PASS
profiler telemetry bridge: PASS
hot reload safety: load=7283.371 swap=0.774 total=7284.156 pre_p50_ns=172 pre_p99_ns=860 max_call_ns=113653 slow_calls=2 threshold_ns=10000 completed_calls=400000 failed_calls=0 zero_call_loss=yes trigger_call=200000 first_changed_call=254709 old_calls=254708 new_calls=145292 unexpected_calls=0 channels=10 algo=1 proto=2 bad_replacement_rc=-1 preserved_channels=10 preserved_algo=1 preserved_proto=2
```

Captured full stdout/stderr log: `docs/tmp/eval-improvement-test-output.txt`
