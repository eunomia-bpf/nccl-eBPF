# NCCLPol Paper Revision Plan: Comparative Analysis and Actionable Recommendations

Date: 2026-03-09
Analyst: Senior systems researcher

---

## Part 1: What Was Lost in Compression

The paper went from 1591 lines (13-page OSDI draft) to 663 lines (5-page workshop). The compressed version already fixed the stale-numbers problem identified in Round 2 review (it uses the latest data: 52--100 ns, 0.774 us swap, etc.). Below is a section-by-section analysis of what was cut, what should be restored, and what was correctly cut.

### 1.1 Abstract

**Old version** (24 lines, rich detail):
```
NCCL is the de facto standard library for GPU collective communication in
large-scale distributed training. It exposes a plugin system---tuner,
profiler, net, and env plugins---that allows operators to customize
collective behavior. However, these plugins execute as native code within
NCCL's address space, with no isolation, no verification, and no safe
mechanism for cross-plugin state sharing. A single null-pointer
dereference in a tuner plugin crashes the entire training job, potentially
wasting thousands of GPU-hours.
```

**New version** (12 lines, compressed):
```
NCCL's plugin system allows operators to customize collective
communication behavior, but plugins execute as native code with no
isolation, verification, or structured state sharing.
```

**Assessment**: The old abstract's opening paragraph was better -- it named "thousands of GPU-hours" as the cost, which grounds the problem concretely. The compressed version is too terse and loses the motivation's weight. The old abstract also mentioned all three mechanisms explicitly with clear phrasing. The new abstract is functionally adequate but lacks punch.

**Restore**: The opening sentence of the old abstract ("NCCL is the de facto standard...") and the "thousands of GPU-hours" phrasing. This adds ~3 lines but significantly strengthens motivation.

### 1.2 Introduction

**Old version** had three distinct paragraphs:
1. "The safety gap" (15 lines) -- vivid description of bugs in dlopen'd plugins
2. "Our insight" (15 lines) -- the latency-tolerance observation articulated carefully
3. "System overview" (12 lines) -- concretely naming NCCL_TUNER_PLUGIN/NCCL_PROFILER_PLUGIN env vars

**New version** collapses these into two paragraphs with less detail. The most significant losses:

1. **The fleet management angle** was cut:
   ```
   The situation worsens when operations teams manage plugin deployments
   across a fleet. Operators need to roll out, roll back, and iterate on
   tuning policies across heterogeneous clusters---but loading native code
   plugins provides no safety net against bugs introduced during these
   updates.
   ```
   This paragraph connects the problem to production operations and is valuable for motivation.

2. **The contrast with traditional eBPF domains** was cut:
   ```
   This contrasts sharply with eBPF's traditional domain (packet processing,
   syscall filtering) where nanosecond budgets are tight. In collective
   communication, eBPF's overhead budget is generous, and the safety and
   composability benefits are large.
   ```
   This is a key insight sentence that differentiates NCCLPol from prior eBPF work. It should be restored.

3. **The concrete env-var loading description** was cut:
   ```
   The entire system operates within NCCL's stock plugin ABI: NCCLPol is
   loaded via standard NCCL_TUNER_PLUGIN and NCCL_PROFILER_PLUGIN
   environment variables, requiring no modifications to NCCL itself.
   ```
   This makes the zero-modification claim concrete and believable.

**Restore**: Items 1-3 above, ~8 lines total.

### 1.3 Background and Motivation

**Old version** had rich subsections:
- 2.1: Detailed description of each plugin type (tuner, profiler, net, env) with API specifics
- 2.2: Three concrete failure scenarios (null deref, silent corruption, infinite loop) -- each ~6 lines with vivid detail
- 2.3: "Why eBPF Fits" with three numbered properties
- 2.4: "Comparison with Prior Approaches" (2 paragraphs)

**New version** condenses to three paragraphs. The biggest losses:

1. **The three failure scenarios** were reduced to a single paragraph. The old version's Scenario 2 (silent corruption) was particularly compelling:
   ```
   A tuner plugin has an off-by-one error that overwrites an adjacent NCCL
   internal structure. The corruption does not cause an immediate crash but
   produces subtly incorrect algorithm selections. Training proceeds but
   converges slowly or produces incorrect gradients. This failure mode is
   worse than a crash because it is silent.
   ```
   This vividly illustrates why verification matters beyond just crash prevention.

2. **The tuner API specifics** (cost arrays, not direct algorithm IDs) were cut from Background but partially preserved in Implementation. Still, the Background section should briefly mention the cost-array indirection because it is central to understanding the design.

**Restore**: Scenario 2 (silent corruption) as a 3-line example. Add one sentence about cost arrays to Background.

### 1.4 Design

**Old version** had:
- Design Tensions (T1-T4): preserved nearly verbatim (good)
- Threat Model: preserved but condensed (adequate)
- Architecture Overview with 4 detailed component descriptions
- Policy Programming Model with two code listings (context struct + size_aware + adaptive_channels)
- Multi-Tenant Policy Differentiation subsection
- Design Non-Goals subsection

**New version** preserved the tensions and threat model well. The biggest losses:

1. **The policy_context struct listing** was cut. The old version showed the context struct (Listing 1) which made the programming model concrete. The new version only describes it in prose. This listing should be restored -- it takes 8 lines and is high-information-density.

2. **The adaptive_channels code listing** (Listing 3, 18 lines) was cut. This was the most compelling code sample in the paper because it shows the map lookup, null check, and feedback loop. Only the size_aware listing remains. The adaptive_channels listing demonstrates the novel closed-loop capability.

3. **The "Design Non-Goals" subsection** was cut. The old version explicitly said NCCLPol does not optimize algorithms themselves and does not replace NCCL's internal logic. This is valuable framing that prevents reviewer misunderstanding.

**Restore**: The adaptive_channels listing (or a 10-line condensed version). The context struct could become an inline annotation rather than a full listing. One sentence from Design Non-Goals.

### 1.5 Implementation

**Old version** had detailed subsections: bpftime integration, plugin registration, NCCL integration challenges (cost array semantics, channel clamping, communicator identification), hot-reload implementation, native baseline for comparison.

**New version** preserves the key points adequately. The only notable loss is the **plugin registration detail** (naming ncclTunerPlugin_v3/v5 and ncclProfilerPlugin_v1/v6 explicitly). This is useful for reproducibility.

**Keep as-is** -- the current Implementation section is adequate for a workshop paper.

### 1.6 Evaluation

**Old version** had five RQs across four subsections:
- RQ1: CPU Overhead (with detailed decomposition itemized list)
- RQ2: Verifier Effectiveness (with SIGSEGV backtrace vs verifier error output)
- RQ3: Hot-Reload Integrity (separate subsection with Table 2)
- RQ4: End-to-End GPU Overhead (with policy adoption heatmap placeholder)
- RQ5: Policy Expressiveness (4 sub-experiments)

**New version** collapsed to three RQs:
- RQ1: CPU Overhead (preserved with updated numbers)
- RQ2: Safety and Hot-Reload (merged RQ2+RQ3)
- RQ3: Policy Expressiveness (preserved 4 sub-experiments)

**Losses**:

1. **The detailed overhead decomposition itemized list** was replaced by inline prose. The old version's three-bullet breakdown (dispatch, map lookup, map update) with precise explanations was clearer.

2. **The SIGSEGV vs verifier output comparison** was cut. The old version showed actual crash backtrace and verifier error message in code blocks. This is viscerally effective:
   ```
   Signal: SIGSEGV (address 0x0)
     in getCollInfo() at native_bad_plugin.so
   ```
   vs.
   ```
   VERIFIER REJECT: R0 is a pointer to
     map_value_or_null; must check != NULL
     before dereference at insn 7
   ```
   This 6-line comparison should be restored.

3. **The hot-reload subsection** was merged into RQ2. The old version had a dedicated table (Table 2) with swap window, active calls, failed calls, etc. The new version presents these as inline numbers. The old table format was cleaner.

4. **End-to-end Table 3**: The old version showed only 128 MB in the table. The new version shows 1 KB, 1 MB, and 128 MB -- this is an improvement. However, the full 25-row GPU benchmark data (from benchmark-results.md) remains unused.

5. **Policy adoption verification**: The old version had a placeholder figure for algo/proto adoption across 10 message sizes. The new version drops this entirely. This data exists (from Phase 4 logs) and is valuable -- it proves the policies actually influence NCCL.

**Restore**: The SIGSEGV vs verifier comparison (6 lines). Consider a compact 5-row subset of the full GPU benchmark table.

### 1.7 Related Work

**Old version** had extensive discussion of:
- Alternative safe-extension mechanisms (WebAssembly, out-of-process, declarative DSLs, MPK) -- each ~5 lines
- GPU runtime policy paragraph

**New version** preserves this content adequately in a more compressed form. The alternative-mechanisms discussion is actually well-done in the compressed version. No restoration needed.

### 1.8 Discussion and Conclusion

**Old version** had separate Discussion and Conclusion sections. Discussion covered: current limitations, eBPF expressiveness boundaries, broader applicability, EIM integration.

**New version** merges them. The key losses:

1. **eBPF expressiveness boundaries paragraph** was cut:
   ```
   Not all conceivable NCCL policies can be expressed in eBPF. Policies
   that require floating-point arithmetic, dynamic memory allocation, or
   access to GPU memory exceed eBPF's execution model. We argue that these
   limitations are acceptable for the policy decision layer.
   ```
   This is valuable because it preemptively addresses a common reviewer question.

2. **Broader applicability paragraph** was partially preserved but shortened. The old version mentioned MPI, PyTorch FSDP, DeepSpeed, RDMA middleware explicitly.

**Restore**: The expressiveness boundaries paragraph (4 lines).

### 1.9 Summary: Content Priority for Restoration

| Content | Lines needed | Impact | Priority |
|---------|-------------|--------|----------|
| Old abstract opening + "thousands of GPU-hours" | +3 | High | 1 |
| eBPF contrast with packet processing domains | +2 | High | 2 |
| SIGSEGV vs verifier output comparison | +6 | High | 3 |
| Silent corruption scenario (Scenario 2) | +3 | Medium | 4 |
| Adaptive channels code listing | +12 | Medium | 5 |
| Fleet management motivation | +3 | Medium | 6 |
| eBPF expressiveness boundaries | +4 | Medium | 7 |
| Concrete env-var loading | +2 | Low | 8 |
| Design non-goals sentence | +2 | Low | 9 |
| Total if all restored | +37 | | |

Budget: To reach 6 pages, we have ~100 lines of slack from the current 5-page version. All items above fit comfortably.

---

## Part 2: Figure Analysis

### 2.1 Architecture Figure (architecture.tex) -- KEPT

**Correctness**: The figure accurately represents the system architecture. It shows the NCCL process containing NCCL Core, Tuner v5 Adapter, Profiler v6 Adapter, bpftime runtime (with Verifier, JIT, Maps, Helpers), shared eBPF maps (telemetry_map, config_map), and eBPF policies (Profiler policy, Tuner policy). The flow arrows correctly show:
- NCCL Core -> Tuner (collective metadata) and -> Profiler (timestamps)
- Tuner -> Tuner policy and back (decision)
- Profiler -> Profiler policy
- Profiler policy -> telemetry_map -> Tuner policy (closed-loop)
- External policy.bpf.o -> load into bpftime

**Issues**:
1. The `policy.bpf.o` node at (-0.72, 3.15) is outside the NCCL process bounding box. This is intentional (external file loaded in) but the positioning is awkward -- it overlaps the left edge of the bpftime box.
2. The Verifier/JIT/Maps/Helpers are laid out horizontally, which might suggest they are pipeline stages. In reality they are components. This is a minor visual ambiguity.
3. The "Shared eBPF maps" label and the individual map chips (telemetry_map, config_map) are redundant with the "Maps" chip in the bpftime row above. The relationship is unclear -- are these the same maps or different?
4. Font sizes are good (\footnotesize, \scriptsize used appropriately).
5. Colors are well-chosen and distinguishable.

**Recommendations**:
- Move the `policy.bpf.o` node slightly right so it does not overlap the bpftime box edge, or add a small gap.
- Add a thin line or visual cue connecting the "Maps" chip in the bpftime row to the "Shared eBPF maps" section below, to show they are the same subsystem.
- The architecture figure is solid overall. It tells the story clearly.

### 2.2 Overhead Staircase (overhead_staircase.tex) -- KEPT

**Data correctness**: The bar heights match the latest experimental data:
- native: 10, noop: 61, size_aware_v2: 62, lookup_only: 88, lookup_update: 113, adaptive_ch.: 112, slo_enforcer: 110

**PROBLEM**: The adaptive_channels (112) and slo_enforcer (110) bars show slo_enforcer being *shorter* than adaptive_channels. This is backwards from the data in Table 1 of the paper. Looking at the eval-improvement-results.md data:
- adaptive_channels: P50=112
- slo_enforcer: P50=110

These are within noise of each other (2 ns difference). The ordering in the figure is correct -- it matches the data. But the staircase narrative breaks down because slo_enforcer (the "most complex" policy) is actually 2 ns *cheaper* than adaptive_channels. The paper text says "the most complex policy adds 100 ns over native code" -- this is correct (110-10=100).

The annotations are:
- "+51 ns dispatch" pointing from native to noop -- correct (61-10=51)
- "+26 ns lookup" pointing from size_aware_v2 to lookup_only -- correct (88-62=26)
- "+25 ns update" pointing from lookup_only to lookup_update -- correct (113-88=25)

**Issues**:
1. The staircase is no longer clean: the last three bars (113, 112, 110) are essentially flat. The "staircase" metaphor works for the first four bars but breaks for the last three. The text acknowledges this with the "approximately 51 + 26n_lookup + 25n_update" formula.
2. The annotation arrows (nccl alert style, orange) are hard to read because they point from the annotation chip to the bar, crossing over other bars. The visual path is cluttered.
3. x-axis labels at 28-degree rotation are readable but tight. Consider abbreviating further.
4. The bar for native (10 ns) is very short relative to the y-axis max (130), making it hard to see. Consider starting y-axis at 0 (already done) -- this is fine.
5. The two separate \addplot calls (one for native in gray, one for eBPF policies in blue) create a correct visual distinction.

**Recommendations**:
- Remove the annotation for "+25 ns update" between bars 3 and 4 -- it is correctly annotated.
- The three flat bars at the end (113, 112, 110) could benefit from a horizontal bracket or annotation saying "map-heavy policies cluster at ~110 ns" rather than trying to show incremental differences that are within noise.
- Consider using a stacked bar or grouped bar format to show the decomposition more directly: base dispatch (gray), map lookup contribution (blue), map update contribution (teal).

### 2.3 Contention Timeseries (contention_timeseries.tex) -- KEPT

**Data correctness**: The data matches eval-improvement-results.md exactly:
- Baseline phase: channels rise from 9 to 12 by call 40K, stay at 12 through call 100K
- Contention phase: channels drop from 11 to 2, call 110K to 200K
- Recovery phase: channels rise from 3 to 12, call 210K to 300K

The figure uses 30 data points matching the 30 samples in the eval data.

**Issues**:
1. The x-axis label says "Call count ($\times 10^3$)" but the tick labels are {50, 100, ..., 300}. So the actual call counts are 50K, 100K, etc. However, the data starts at call 10K (first sample at (10,9)). This means the leftmost data point is at x=10 (representing call 10K), but the first major tick is at x=50 (call 50K). The xmin=10 is correct.
2. The three colored background regions effectively communicate the phase structure.
3. **No error bars or confidence intervals.** These are single-run numbers from the CPU harness with synthetic latency injection. This should be explicitly stated in the caption or text.
4. The symmetric ramp-up/ramp-down is a direct consequence of the +1/-1 adjustment logic in the policy, not evidence of feedback-loop stability. The paper text acknowledges this.
5. Mark size (1.9pt) and line width (1.4pt) are appropriate.

**Recommendations**:
- Add a note in the caption or text: "Single run; contention injected via map write."
- The figure is well-executed and tells its story effectively. No major changes needed.

### 2.4 Verifier Matrix (verifier_matrix.tex) -- REMOVED

**Assessment**: This figure was a two-panel TikZ diagram:
- Left panel: 7 green ACCEPT chips, 7 orange REJECT chips
- Right panel: "Same null-deref bug" comparison -- Native tuner .so -> Runtime SIGSEGV vs eBPF bad_lookup -> Load-time reject

**Should it be restored?** YES, in condensed form. The right panel (native crash vs eBPF rejection) is the single most compelling visual in the paper. It directly illustrates the core safety proposition. The left panel (accept/reject list) is redundant with the table and can be dropped.

**Recommendation**: Restore only the right panel as a small inline figure or merge it into the architecture figure as an inset. Alternatively, restore the SIGSEGV vs verifier-error code comparison in text form (6 lines, as noted in Part 1).

### 2.5 Hot-Reload Timeline (hot_reload_timeline.tex) -- REMOVED

**Assessment**: This figure showed:
- A timeline with old policy serving calls (blue bar), then new policy (teal bar)
- Reload path (verify + JIT, dashed gold line) running in parallel
- Atomic swap marker (orange vertical bar, 0.309 us -- now stale, should be 0.774 us)
- "400,000 / 400,000 complete" and "0 lost calls" annotations
- "Bad replacement rejected" annotation

**Should it be restored?** MAYBE. The timeline visualization is conceptually effective, but the numbers need updating (0.774 us, 254709 first_changed_call, 7.28 ms total reload). The current paper presents the hot-reload results as inline text, which works for a 6-page paper. If space allows, restoring this figure would strengthen the hot-reload story.

**Recommendation**: If restored, update all numbers. Simplify the "bad replacement" annotation to avoid visual clutter. Low priority -- the text description is adequate.

### 2.6 Closed-Loop Figure (closed_loop.tex) -- REMOVED

**Assessment**: This figure showed only 3 data points per line (calls 1, 2, 3) on a very narrow axis (7.7--10.3 channels). "Profiler enabled" line: 8, 9, 10. "Profiler disabled" line: 8, 8, 8.

**Should it be restored?** NO. The Round 2 review correctly identified this as the weakest figure: 6 total data points on a nearly empty plot. The contention timeseries (30 points) already demonstrates closed-loop behavior more convincingly. The profiler-enabled vs profiler-disabled comparison is adequately described in 3 lines of text.

### 2.7 Figures Summary

| Figure | Status | Action |
|--------|--------|--------|
| Architecture | KEPT | Minor fixes: policy.bpf.o positioning, Maps linkage |
| Overhead Staircase | KEPT | Consider stacked bars; annotate the flat cluster at ~110 ns |
| Contention Timeseries | KEPT | Add "single run, synthetic injection" note |
| Verifier Matrix | REMOVED | Restore RIGHT PANEL ONLY (crash vs reject comparison) |
| Hot-Reload Timeline | REMOVED | Restore if space allows (update numbers) |
| Closed Loop | REMOVED | Do NOT restore |

### 2.8 Missing Figure: End-to-End GPU Latency

The paper has Table 2 showing GPU latency at 3 sizes. The benchmark-results.md has data for 25 message sizes with 4 configurations (no plugin, noop, size_aware_v2, slo_enforcer). A line plot showing these 4 configurations across message sizes would be a strong addition. It would visually demonstrate that all lines overlap (confirming negligible overhead) and would be far more convincing than a 3-row table.

**Recommendation**: Add a new figure: GPU end-to-end AllReduce latency vs message size, 4 lines. Log-log scale. This uses existing data from benchmark-results.md.

---

## Part 3: Evaluation Improvement Analysis

### 3.1 Current Hardware Capabilities

Hardware: 1x RTX 5090, 24-core x86_64, CUDA 12.9, NCCL 2.29.7, OpenMPI

The 2-rank path is established via mpirun with NCCL_HOSTID trick over socket transport. This exercises real NCCL getCollInfo() calls.

### 3.2 Experiments That Can Be Done RIGHT NOW

#### 3.2.1 Outcome Benefit: size_aware_v2 vs NCCL Defaults on 2-Rank Path

The benchmark-results.md shows that size_aware_v2 reduced bus bandwidth compared to no-plugin on the socket-only topology. But that is because the policy was not tuned for that topology. A more compelling experiment:

1. Run the 2-rank allreduce benchmark with NO plugin (NCCL defaults)
2. Run with size_aware_v2
3. Run with a policy specifically tuned to match NCCL's own default selections for this topology

If the tuned policy matches NCCL defaults and the "mis-tuned" policy shows worse performance, this demonstrates that (a) policy choices matter and (b) the mechanism faithfully transmits those choices. This is a "mechanism correctness" result, not an "optimization" result, but it is still valuable.

**Command**:
```bash
# Baseline (no plugin)
mpirun --oversubscribe \
  -np 1 env LD_LIBRARY_PATH=nccl/build/lib NCCL_TESTS_DEVICE=0 NCCL_P2P_DISABLE=1 NCCL_SHM_DISABLE=1 NCCL_NET=Socket NCCL_HOSTID=rank0 nccl-tests/build/all_reduce_perf_mpi -b 1K -e 128M -f 2 -g 1 -n 100 -w 10 : \
  -np 1 env LD_LIBRARY_PATH=nccl/build/lib NCCL_TESTS_DEVICE=0 NCCL_P2P_DISABLE=1 NCCL_SHM_DISABLE=1 NCCL_NET=Socket NCCL_HOSTID=rank1 nccl-tests/build/all_reduce_perf_mpi -b 1K -e 128M -f 2 -g 1 -n 100 -w 10

# With size_aware_v2
# (same command but add NCCL_TUNER_PLUGIN=... NCCL_POLICY_BPF_PATH=...size_aware_v2.bpf.o)

# With noop (to isolate plugin overhead)
# (same command but with noop.bpf.o)
```

Run each 3-5 times and compute mean/stddev. This provides error bars.

#### 3.2.2 Multiple Runs for Error Bars

All current GPU benchmarks appear to be single-run. Run each configuration 5 times and report mean +/- stddev. This addresses the Round 2 review concern about missing confidence intervals.

The CPU microbenchmark already uses 1M calls, so its P50/P99 are statistically robust. But the GPU end-to-end measurements could benefit from 5-run averaging.

#### 3.2.3 Different Collective Types on 2-Rank Path

The benchmark-results.md only tests AllReduce. NCCL-tests includes allgather_perf, reduce_scatter_perf, broadcast_perf. Running these with and without the plugin would demonstrate multi-collective coverage at the GPU level (currently only shown in the CPU harness).

**Command**:
```bash
# AllGather
mpirun ... nccl-tests/build/allgather_perf_mpi -b 1K -e 128M -f 2 -g 1 -n 100 -w 10

# ReduceScatter
mpirun ... nccl-tests/build/reduce_scatter_perf_mpi -b 1K -e 128M -f 2 -g 1 -n 100 -w 10

# Broadcast
mpirun ... nccl-tests/build/broadcast_perf_mpi -b 1K -e 128M -f 2 -g 1 -n 100 -w 10
```

#### 3.2.4 JIT Compilation Time and Verification Time Breakdown

The hot-reload test reports total reload time (~7.28 ms) but does not break it into verification time vs JIT compilation time vs swap time. Adding timing instrumentation to the load path would produce a 3-component breakdown that enriches the paper:
- Verification: ~X ms
- JIT compilation: ~Y ms
- Atomic swap: 0.774 us

This can be done by adding timestamps around the verifier call and JIT call in plugin.cpp.

#### 3.2.5 Hot-Reload Under Real NCCL Traffic

The current hot-reload test uses the CPU harness. Running the 2-rank allreduce benchmark while triggering a hot-reload mid-run would be a stronger test. This requires:
1. Start a long-running allreduce benchmark (e.g., 1000 iterations)
2. After ~500 iterations, trigger a reload via file replacement + signal
3. Observe that the benchmark completes without error

This may not be trivial to implement but would significantly strengthen the hot-reload claim.

#### 3.2.6 Adaptive Channels with Real Profiler on 2-Rank Path

The profiler-adapter-results.md shows this was already done and channels went 8->9->10. But the benchmark was short (5 iterations). Running a longer benchmark (100+ iterations) with the adaptive_channels policy and profiler would show a longer-term adaptation trajectory.

#### 3.2.7 Verification Time Per Policy

Run the verifier on each of the 14 programs and report individual verification times. This would show that verification scales with program complexity and is always in the low-millisecond range.

### 3.3 What CANNOT Be Done Right Now

- Multi-node experiments (only 1 node)
- InfiniBand/RoCE transport (socket only)
- Comparison with Wasm/out-of-process alternatives (not implemented)
- Real multi-tenant workloads (no second GPU, no real tenants)
- Training-throughput impact (no training framework integration)

### 3.4 Priority Ranking of New Experiments

| Experiment | Effort | Impact | Priority |
|-----------|--------|--------|----------|
| 5x runs for error bars (GPU benchmarks) | Low (scripting) | High | 1 |
| Verification/JIT time breakdown | Low (add timestamps) | High | 2 |
| Multi-collective GPU benchmarks | Low (run existing tools) | Medium | 3 |
| Outcome comparison: tuned vs mis-tuned policy | Medium | Medium | 4 |
| Longer adaptive_channels with profiler | Medium | Medium | 5 |
| Hot-reload under real NCCL traffic | High | High | 6 |
| Per-policy verification times | Low | Low | 7 |

---

## Part 4: Specific Revision Plan

### Priority 1: CRITICAL (must do)

#### 1.1 Expand the Abstract (+3 lines)

Restore the old abstract's opening framing. Current abstract starts too abruptly with "NCCL's plugin system allows operators to customize..." This should become:

```
NCCL is the de facto standard library for GPU collective communication
in large-scale distributed training. Its plugin system allows operators
to customize collective behavior, but plugins execute as native code
with no isolation, verification, or structured state sharing. A single
null-pointer dereference crashes the entire training job, potentially
wasting thousands of GPU-hours.
```

#### 1.2 Restore Key Insight Sentence in Introduction (+2 lines)

After the "generous overhead budget" sentence, add:
```
This contrasts sharply with eBPF's traditional domain---packet processing
and syscall filtering---where nanosecond budgets are tight. In collective
communication, eBPF's overhead budget is generous.
```

#### 1.3 Restore SIGSEGV vs Verifier Error Comparison (+6 lines)

In RQ2 (Safety), after "This is the core safety proposition", add:
```
Loading the native equivalent into NCCL produces:
  Signal: SIGSEGV (address 0x0) in getCollInfo()
The eBPF version is rejected at load time with:
  VERIFIER REJECT: R0 is a pointer to
  map_value_or_null; must check != NULL
  before dereference at insn 7
```

This is the most visceral demonstration in the paper and was the strongest element of the old version.

#### 1.4 Add Error Bars to GPU Benchmarks (experiment)

Run each GPU benchmark configuration 5 times. Report mean +/- stddev in Table 2. This addresses the Round 2 review's criticism about no variance information.

#### 1.5 Fix the Overhead Staircase Figure

The last three bars (113, 112, 110) break the staircase metaphor. Options:
- (a) Add a bracket annotation: "map-heavy policies ~110 ns" across the last three bars
- (b) Reorder bars so slo_enforcer is last (it is semantically the "most complex")
- (c) Accept the non-monotonicity and note it in text: "Within-noise ordering among map-heavy policies"

Recommendation: option (c) -- add a sentence to the text acknowledging the plateau.

### Priority 2: HIGH (significantly improves the paper)

#### 2.1 Add GPU End-to-End Latency Line Plot (new figure)

Create a new pgfplots figure: AllReduce latency vs message size on log-log axes, with 4 lines (no plugin, noop, size_aware_v2, slo_enforcer). Data from benchmark-results.md. This replaces or supplements the 3-row Table 2 and visually demonstrates overlapping lines (=negligible overhead).

Suggested figure code:
```latex
\begin{figure}[t]
  \centering
  \begin{tikzpicture}
    \begin{axis}[
      nccl axis,
      width=\columnwidth,
      height=0.65\columnwidth,
      xmode=log, ymode=log,
      log basis x=2,
      xlabel={Message size (bytes)},
      ylabel={AllReduce latency (\textmu{}s)},
      legend style={font=\scriptsize, at={(0.03,0.97)}, anchor=north west},
    ]
      % Plot 4 lines from benchmark-results.md
      % Use a subset: 8B, 64B, 1KB, 8KB, 64KB, 512KB, 1MB, 8MB, 64MB, 128MB
    \end{axis}
  \end{tikzpicture}
  \caption{End-to-end AllReduce latency across message sizes. All four
  configurations (no plugin, noop, size\_aware\_v2, slo\_enforcer) are
  visually indistinguishable, confirming negligible GPU-level overhead.}
\end{figure}
```

Select ~10 representative message sizes from the 25 available rows to avoid clutter.

#### 2.2 Restore the Adaptive Channels Code Listing (+10 lines)

This is the paper's most novel code sample. Compress to 10 lines:
```c
SEC("policy")
int adaptive_channels(struct policy_context *ctx) {
  __u32 key = ctx->comm_id;
  struct latency_state *st =
    bpf_map_lookup_elem(&latency_map, &key);
  if (!st) { /* first call */
    ctx->n_channels = 8; return 0;
  }
  if (st->avg_latency_ns > 1000000)
    st->channels = min(st->channels + 1, 16);
  ctx->n_channels = st->channels;
  return 0;
}
```

Add a one-sentence annotation: "The mandatory null check on line 5 is enforced by the verifier; omitting it would be rejected at load time."

#### 2.3 Add Verification/JIT Time Breakdown (experiment + table row)

Instrument plugin.cpp to measure verification time and JIT time separately. Add to the hot-reload discussion:
```
The full reload cost decomposes: verification X ms, JIT compilation Y ms,
atomic swap 0.774 us. Verification and JIT run concurrently with normal
policy execution; only the swap touches the hot path.
```

#### 2.4 Restore Silent Corruption Scenario (+3 lines)

In the Background "safety gap" paragraph, add after "terminating the training job":
```
Worse, an off-by-one error can silently corrupt NCCL internal state,
producing subtly incorrect algorithm selections that degrade training
convergence without any crash or error message.
```

#### 2.5 Add Fleet Management Motivation (+3 lines)

In the Introduction, after mentioning plugin updates:
```
Operations teams must roll out, roll back, and iterate on tuning policies
across heterogeneous clusters. Loading native code provides no safety net
against bugs introduced during these updates.
```

### Priority 3: MEDIUM (improves quality)

#### 3.1 Restore the Hot-Reload Timeline Figure (if space allows)

Update numbers: 0.774 us swap, 254709 first_changed_call, 7.28 ms total reload. This is a good figure if it fits within the 6-page budget.

#### 3.2 Add eBPF Expressiveness Boundaries Paragraph (+4 lines)

In Discussion:
```
Not all conceivable policies can be expressed in eBPF: floating-point
arithmetic, dynamic memory allocation, and GPU memory access exceed the
execution model. These limitations are acceptable because policy decisions
operate on metadata (message size, rank count, latency), not tensor content.
```

#### 3.3 Run Multi-Collective GPU Benchmarks (experiment)

Run allgather_perf, reduce_scatter_perf, broadcast_perf through the 2-rank path. Add a sentence: "We verified that the plugin integrates correctly with AllGather, ReduceScatter, and Broadcast on the GPU path as well."

#### 3.4 Add One Sentence About Design Non-Goals

In Design section: "NCCLPol does not optimize collective algorithms themselves; it selects among NCCL's built-in algorithms at runtime. A custom MSCCL schedule can be activated by a policy that detects appropriate conditions."

#### 3.5 Move "Outcome evaluation is future work" to Discussion

The current paper mentions this in RQ3 (Policy Expressiveness). It should also appear prominently in the Discussion section as the #1 limitation.

### Priority 4: LOW (polish)

#### 4.1 Fix Architecture Figure Positioning

Move policy.bpf.o node slightly right to avoid overlapping the bpftime box edge.

#### 4.2 Add Reproducibility Details

In the Testbed paragraph, add CPU model (e.g., "Intel Core i9-xxxxx" or "AMD Ryzen ..."), clock speed, and core isolation method (isolcpus= or taskset).

#### 4.3 Add "Single run" Note to Contention Figure Caption

#### 4.4 Verify All .bib Entries

The Round 2 review identified:
- MSCCL citation has wrong authors (should be Cowan/Maleki, not Cai)
- ebpf-linux citation uses @inproceedings with journal field
- Several entries use "and others" instead of full author lists

---

## Appendix: Data Reconciliation Check

The current paper (compressed version) uses the following numbers:

| Metric | Paper value | Source | Correct? |
|--------|------------|--------|----------|
| noop P50 | 61 ns | eval-improvement-results.md | YES |
| size_aware_v2 P50 | 62 ns | eval-improvement-results.md | YES |
| slo_enforcer P50 | 110 ns | eval-improvement-results.md | YES |
| Overhead range | 52--100 ns | eval-improvement-results.md | YES |
| Dispatch cost | +51 ns | 61-10 | YES |
| Map lookup cost | +26 ns | 88-62 | YES |
| Map update cost | +25 ns | 113-88 | YES |
| Overhead formula | 51 + 26n_lookup + 25n_update | derived | YES |
| Swap time | 0.774 us | eval-improvement-results.md | YES |
| Total reload | ~7.3 ms | eval-improvement-results.md (7283.371 us) | YES |
| First changed call | 254709 | eval-improvement-results.md | YES |
| GPU 1KB | 2.16/2.21/2.13 us | benchmark-results.md | YES |
| GPU 1MB | 3.98/4.02/4.03 us | benchmark-results.md | YES |
| GPU 128MB | 175.42/175.51/175.47 us | benchmark-results.md | YES |
| Contention channels | 12->2->12 | eval-improvement-results.md | YES |
| Verifier results | 14/14 | eval-improvement-results.md | YES |

**Conclusion**: The compressed paper has already reconciled all numbers with the latest experimental data. The Round 2 review's #1 critical issue (stale numbers) has been fixed. No further number reconciliation is needed.

---

## Appendix: Key Quotes from Old Version Worth Preserving

### Quote 1 (Introduction -- insight):
> "This contrasts sharply with eBPF's traditional domain (packet processing, syscall filtering) where nanosecond budgets are tight. In collective communication, eBPF's overhead budget is generous, and the safety and composability benefits are large."

### Quote 2 (Background -- silent corruption):
> "The corruption does not cause an immediate crash but produces subtly incorrect algorithm selections. Training proceeds but converges slowly or produces incorrect gradients. This failure mode is worse than a crash because it is silent."

### Quote 3 (Eval -- crash vs rejection):
> Loading this native plugin into NCCL produces an immediate SIGSEGV [...] The equivalent eBPF program is rejected at load time with: "VERIFIER REJECT: R0 is a pointer to map_value_or_null; must check != NULL before dereference at insn 7"

### Quote 4 (Discussion -- expressiveness):
> "Not all conceivable NCCL policies can be expressed in eBPF. Policies that require floating-point arithmetic, dynamic memory allocation, or access to GPU memory exceed eBPF's execution model. We argue that these limitations are acceptable for the policy decision layer."

### Quote 5 (Old adaptive_channels listing caption):
> "Profiler-informed channel adaptation. The latency_map is written by the profiler plugin and read by the tuner policy."

---

## Final Assessment

The compressed paper is a solid 5-page draft that fixed the data integrity issues from Round 2. To reach a strong 6-page workshop paper, the highest-impact actions are:

1. **Restore visceral examples** (SIGSEGV comparison, silent corruption scenario)
2. **Add the GPU latency line plot** (converts a weak 3-row table into a strong visual)
3. **Add error bars** to GPU measurements (5 runs)
4. **Restore the adaptive_channels listing** (demonstrates the novel closed-loop model in code)
5. **Measure verification/JIT time breakdown** (enriches the overhead story)

These five actions require approximately 2-3 hours of work (1 hour experiments, 1-2 hours editing) and would meaningfully improve the paper's persuasiveness for a workshop submission.
