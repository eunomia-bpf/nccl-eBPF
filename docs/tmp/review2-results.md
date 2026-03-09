# Review #2 Results

Date: 2026-03-09

Overall assessment: the CPU microbenchmark is close to paper-usable for hot-path overhead claims, but the current GPU integration dataset is not paper-ready for per-collective policy overhead because `getCollInfo()` is never exercised on the collected single-rank path.

## 1. Data quality assessment

### CPU microbenchmark

- Trustworthiness: reasonably good for steady-state hot-path overhead. The harness times only the direct `plugin->getCollInfo(...)` call over `kIterations = 1,000,000`, with varied `collType`, `nBytes`, `numPipeOps`, and `regBuff`. That is a close proxy for the CPU-side tuner hot path.
- Warmup: adequate, but not ideal. There is no explicit untimed benchmark warmup loop in `src/nccl-policy-plugin/test/test_ebpf_plugin.c`; stateless policies get a one-shot init-time warmup in `src/nccl-policy-plugin/plugin.cpp`, while map-backed policies intentionally skip it. With 1M timed samples, first-call effects are heavily diluted, but repeated independent runs would still strengthen the claim.
- Variance: central tendency is stable. For plugin cases, P99 is only about 15-21% above P50, which is consistent with a stable steady-state path.
- Tail behavior: max is not stable enough to be a representative latency metric. Max is 52x-248x above P99 for the plugin cases, which strongly suggests rare scheduler or interrupt outliers rather than hot-path instability.
- Interpretation: the CPU results are good enough to support "direct `getCollInfo()` dispatch overhead is tens of nanoseconds." They are not, by themselves, evidence of end-to-end NCCL collective overhead.

### GPU integration

- Core validity problem: the plugin is loaded, but the single-rank NCCL path does not exercise `getCollInfo()`. The debug run in `docs/tmp/benchmarks/gpu_single_debug.txt` ends with `finalize calls=0`, and NCCL source confirms why: `nccl/src/enqueue.cc` short-circuits `comm->nRanks == 1` to `ncclLaunchOneRank(...)` instead of going through collective tuning.
- What the current GPU table really measures: plugin load/init/shm overhead and any process-level integration effects on a single-rank `nccl-tests` run. It does not measure runtime policy execution inside NCCL.
- Warmup/iterations: `-w 5 -n 100` is fine for a coarse nccl-tests sanity check, but it is too thin for sub-0.05 us claims from a single run.
- Variance: cannot be assessed properly. The GPU artifacts provide one aggregated `time (us)` value per size/config, not P50/P99/max and not repeated trials.
- Precision issue: the reported averages like `+0.0228 us` overstate confidence. The source values are rounded to `0.01 us`, and the sign flips at multiple sizes, so the data supports "no obvious overhead blow-up," not a precise tens-of-nanoseconds end-to-end claim.
- Visible anomalies: the main wiggles are at 512 B, 8 KiB, and 64 MiB, where deltas swing by about `0.1-0.33 us` or go negative. Without repeated runs, those look like measurement noise or nccl-tests rounding effects, not real policy effects.
- Data completeness gap: `docs/tmp/benchmark-latency.csv` omits `slo_enforcer`, so the figure-ready CSV does not cover the full table shown in `docs/tmp/benchmark-results.md`.

## 2. Gap analysis with specific fixes

### Main gap: `getCollInfo()` is not called in the collected GPU path

- This is a major paper gap, not a minor caveat. Any wording that implies the GPU table measures per-collective policy overhead is currently wrong.
- The right interpretation for the existing GPU table is: "single-rank plugin integration/load overhead is negligible on this machine."

### Why the obvious workarounds do not solve it

- `-t 2 -g 1` is not a fix on this host. `nccl-tests` enumerates `nThreads * nGpus` CUDA devices, so with one visible GPU, `-t 2 -g 1` effectively requests 2 GPUs and fails before the benchmark starts.
- `mpirun` with the default `nccl-tests` build is not enough. `nccl-tests/src/Makefile` defaults to `MPI=0`, so the earlier multi-process attempt still initialized each process as `nranks=1`.
- `mpirun` with the MPI-enabled binary is the correct direction, but it still needs unique GPUs per rank. The repo already reached a real `nranks=2` communicator and then failed on NCCL duplicate-GPU detection because both ranks were on the same visible device.
- `NCCL_COMM_ID` is only a bootstrap mechanism. It can replace MPI control-plane setup in a custom harness, but it cannot bypass NCCL's duplicate-GPU check on a one-GPU host.

### Specific fixes

1. Use hardware with at least 2 visible GPUs, or 2 nodes with 1 GPU each, and rerun the MPI-enabled integration benchmark.
2. Keep an explicit proof of hook execution in the benchmark logs, not only in a separate debug run. The simplest fix is to preserve the plugin finalizer summary or a first-call log line in the collected GPU artifacts.
3. If `nccl-tests` is awkward, write a minimal 2-process harness around `ncclCommInitRank` and bootstrap it with `NCCL_COMM_ID`. This removes nccl-tests build/runtime ambiguity, but it still requires unique GPUs per rank.
4. Relabel the current GPU section in the paper draft as single-rank integration overhead until a nonzero-`calls` dataset exists.
5. Extend `benchmark-latency.csv` to include `slo_enforcer` and metadata such as `nranks`, command line, and observed plugin `call_count`.

## 3. Priority-ranked list of additional experiments needed

1. P0: Successful real NCCL integration with `nranks > 1` and nonzero `getCollInfo()` calls.
Reason: this is the missing experiment that turns the GPU story from "plugin loads" into "policy executes in NCCL."

2. P0: CPU-side overhead breakdown.
Reason: the current microbenchmark shows end-to-end plugin cost, but the paper still needs a clean decomposition such as native baseline, noop plugin, stateless policy, lookup-only, lookup+update, and full map-backed policy. This is the shortest path to explaining where the extra nanoseconds go.

3. P0: Hot-reload latency under active `getCollInfo()` traffic.
Reason: if hot-swap/reload is a system claim, it needs a direct measurement: swap latency, whether calls stall, and whether post-swap decisions change correctly.

4. P1: Real telemetry or controlled synthetic telemetry for map-backed policies.
Reason: `adaptive_channels` and `slo_enforcer` currently benchmark mostly control-plane skeleton cost. They need a run where nonzero latency telemetry drives actual policy adaptation.

5. P1: Repeated-run variance study with confidence intervals.
Reason: one run per config is enough for bring-up, not for publishable sub-0.1 us conclusions. Repeat CPU and multi-rank GPU experiments across at least several independent runs.

6. P2: Multi-policy comparison under load.
Reason: a workshop paper will be stronger if it shows not just overhead, but differentiated behavior across at least one stateless policy and one adaptive policy under real pressure.

### Minimum needed for a credible workshop submission

- Keep the current CPU microbenchmark, but present it explicitly as direct hot-path overhead evidence.
- Add item 1 so the GPU section proves real NCCL hook execution.
- Add item 2 so the overhead story is explainable, not just observable.
- Add item 3 if hot-reload is part of the contribution claim.
- Add item 4 if the paper wants to claim adaptive or SLO-aware behavior rather than only safe low-overhead extensibility.

If item 1 is missing, the submission can still make a CPU-hot-path argument, but it cannot credibly claim exercised NCCL runtime overhead on GPU integration.

## 4. Optimization suggestions

- The most obvious bottleneck is map-helper traffic, not pure policy logic. `size_aware_v2` is essentially equal to `noop` (`44 ns` vs `43 ns`), so arithmetic and branching are almost free compared with the plugin baseline.
- `adaptive_channels` adds one telemetry lookup plus one update and lands at `66 ns`; `slo_enforcer` adds two lookups (`config_map` and `telemetry_map`) plus one update and lands at `81 ns`. The extra `38 ns` over `noop` is very plausibly dominated by helper-backed map operations.
- Replace `config_map` with cached native config or a tiny array map keyed by `coll_type`. A hash lookup on every collective for mostly static config is hard to justify.
- Replace the hash `telemetry_map` with a direct-indexed structure when the key space is tiny. The current key is effectively low-cardinality (`coll_type`, `n_nodes`), so a hash map is likely paying unnecessary overhead.
- Keep fast-moving telemetry in native `PluginContext` and pass it through `nccl_policy_ctx`; only synchronize to BPF-visible maps when shared-state or hot-reload semantics really need it.
- Add synthetic micro-policies such as `pack_only`, `lookup_only`, and `lookup_update` to quantify each component directly. That will let the paper state where bpftime execution ends and map cost begins.
- Watch the `std::mutex` around the entire `getCollInfo()` path. It is not the main culprit in the current single-thread microbenchmark, but it can become a real bottleneck once NCCL calls the tuner concurrently from multiple host threads.
