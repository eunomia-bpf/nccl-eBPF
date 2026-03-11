# Review (Round 2): NCCLPol -- Verified, Composable Policy Execution for GPU Collective Communication

Reviewer: Senior systems researcher (OSDI/SOSP PC perspective)
Date: 2026-03-09
Reviewed artifact: `docs/paper/paper.tex` (1644 lines)
Cross-referenced: previous review (`docs/tmp/paper-review-latex.md`), eval data (`docs/tmp/eval-improvement-results.md`, `docs/tmp/eval-improvement-test-output.txt`), all figure .tex files, references.bib

---

## 1. Overall Assessment

**Is this paper ready for submission?** No -- not for OSDI/SOSP. The writing has improved substantially since Round 1, but two critical problems remain: (1) the evaluation numbers in the paper do not match the latest experimental data, and (2) the evaluation still lacks any demonstration that eBPF-driven policy decisions improve an end-to-end outcome for a real workload. The paper is approaching the level of a good workshop paper (HotOS, HotNets, WORDS, or the EuroSys Workshop track), and with the fixes outlined below could be a solid APSys or similar short-paper submission.

**Single biggest problem remaining:** The paper's quantitative claims are based on stale numbers. The latest test harness run (after the "one minimal core fix" for map-name namespacing) reports substantially different overhead figures than what the paper presents. If the paper is submitted with the current numbers, any reviewer who reads the repo artifacts will notice immediately. This must be reconciled before any submission.

---

## 2. Writing Quality Check

### 2.1 Overclaims -- Substantially Improved, But Some Remain

The Round 1 review flagged pervasive overclaiming ("impossible," "complete isolation," "full control"). The revision addressed most of these. The following residual issues remain:

**Remaining problematic phrases:**

- **Line 137**: "rejects all seven tested classes of unsafe programs" -- the qualifier "tested" is present, which is good, but this is repeated so many times (abstract, contributions, eval, conclusion) that it reads as padding rather than evidence. Seven author-constructed test cases is not a strong evaluation anchor; repeating it everywhere does not make it stronger.

- **Line 251-252**: "achieving a 0.309 us swap window with zero lost calls across 400,000 active invocations" -- see Section 3 below; this number does not match the latest run.

- **Line 257-258**: "verifier coverage of common unsafe patterns (7/7 tested classes rejected)" -- "coverage" is still too strong a word for 7 hand-written test programs. "Detection" would be more accurate.

- **Line 670-673**: "A native C++ plugin implementing the same decision logic requires approximately 200 lines" -- the previous review flagged this as misleading. The current text adds a reasonable caveat (lines 674-678), but the "200 lines" claim itself is still unsubstantiated. Is this measured from a real native plugin? From NCCL's example tuner? The number should either be measured and cited or removed.

- **Line 1282-1283**: "10/10 adoption rate demonstrates that NCCLPol policies have effective influence" -- this is fine for the tested topology, but the phrase "effective influence" is still stronger than what a single-topology 100% adoption demonstrates.

### 2.2 Narrative Coherence

The narrative flow is now good: Problem (unsafe native plugins) -> Insight (NCCL decision path is latency-tolerant, eBPF fits) -> Design (embed bpftime, use maps for cross-plugin sharing) -> Eval (overhead, safety, hot-reload, expressiveness). This is a clear improvement over Round 1.

The "Design Tensions" framing (T1-T4) is a significant improvement over the original "Design Principles" (P1-P4). These now read as genuine design tradeoffs rather than feature statements. T1 (safety vs. overhead), T2 (structured sharing vs. flexibility), and T3 (availability vs. consistency) are particularly well-articulated.

### 2.3 Redundancies and Structural Issues

- **The abstract is effective but slightly too detailed.** The "42--70 ns" and "0.309 us" numbers in the abstract should be updated to match the latest data, but the abstract's structure (problem, solution, three mechanisms, key results) is solid.

- **Section 2.4 ("Comparison with Prior Approaches")** is thin -- just two paragraphs. This should be merged into Related Work (Section 7) or expanded. Having both a comparison subsection and a full Related Work section creates unnecessary redundancy.

- **The threat model section (3.2)** is excellent -- this is the single biggest structural improvement from Round 1. The "What is verified / What is NOT verified / Security posture" structure directly addresses the previous review's primary complaint about overstated safety. Well done.

- **Section 5.5 (RQ5: Policy Expressiveness)** is long (130+ lines) for what it demonstrates. The four sub-experiments (5a-5d) are useful demonstrations of the programming model's flexibility, but the section could be tightened by reducing the inline explanations of why each result matters.

- **The paper mentions "128 MB AllReduce, 175 us" in three places** (lines 373-374, 964, 1224). This single data point is being used as the overhead denominator throughout the paper. Having only one GPU measurement row in Table 3 (128 MB) weakens the end-to-end story. The text says "10 message sizes" were tested -- why is only one shown in the table?

### 2.4 Abstract Assessment

The abstract is reasonably effective. It is 24 lines, which is appropriate. It covers the problem, the solution, the three mechanisms, and the key results. Two issues:

1. The numbers (42--70 ns, 0.309 us) are stale (see Section 3).
2. The phrase "enables closed-loop profiler-to-tuner adaptation that is difficult and error-prone with native plugins alone" (lines 139-140) is the right claim -- this is properly hedged compared to Round 1's "impossible."

---

## 3. Technical Accuracy

### 3.1 CRITICAL: Numbers in the Paper Do Not Match Latest Experimental Data

This is the most serious technical issue. The paper uses numbers from `docs/tmp/revise2-harness-output.txt` (an earlier run), but the latest test run (`docs/tmp/eval-improvement-test-output.txt`) -- which was produced after a "minimal core fix" to the plugin code for map-name namespacing -- shows different numbers:

| Policy | Paper P50 (ns) | Latest P50 (ns) | Paper delta | Latest delta |
|--------|---------------|-----------------|-------------|-------------|
| native | 10 | 10 | -- | -- |
| noop | 51 | 61 | +41 | +51 |
| size_aware_v2 | 52 | 62 | +42 | +52 |
| lookup_only | 63 | 88 | +53 | +78 |
| lookup_update | 74 | 113 | +64 | +103 |
| adaptive_channels | 75 | 112 | +65 | +102 |
| slo_enforcer | 80 | 110 | +70 | +100 |

Key discrepancies:
- **Dispatch overhead**: paper says "+41 ns" (noop), latest shows +51 ns. Nontrivial.
- **Map operations**: paper implies "+11 ns per map op" but latest data shows +26-27 ns per map op (88-62=26 for lookup, 113-88=25 for update). The "clean staircase" narrative still holds but with different step heights.
- **Total range**: paper claims "42--70 ns" overhead. Latest data shows "52--100 ns." The paper's headline claim is wrong.
- **Hot-reload swap**: paper says 0.309 us; latest run shows 0.774 us. The total reload time also differs (paper: "~11 ms"; latest: "7.28 ms" -- curiously the latest is *faster* for total load but slower for the swap).
- **First changed call**: paper says call 298,126; latest shows 254,709. These differ by ~43,000 calls.

The Figure 2 (overhead staircase) annotations (+41 ns dispatch, +11 ns lookup, etc.) use the old numbers and would need to be redrawn. The Figure 4 (hot-reload timeline) also uses the old first_changed_call number.

**The paper's overhead formula "41 + 11n ns" is wrong under the latest data.** The correct formula from the latest run would be approximately "51 + 26n_lookup + 25n_update ns" -- a substantially different story.

### 3.2 Internal Contradictions

- **Table 3 vs. text (lines 1228, 1233)**: Table 3 shows only 128 MB. The text says "10 message sizes (1 KB to 128 MB)" and "Avg. delta across all 10 message sizes: <0.05 us." Why is only one row shown? Either show all 10 sizes or explain why only one is shown.

- **Lines 1313-1315 vs. eval data**: The paper says channels adapt "8 -> 9 -> 10" with profiler feedback, based on a "first measured sample: 9,264,480 ns." This is from the real GPU integration test, but the contention experiment (5c) uses manually injected latency values (100 ns, 10000 ns) from the CPU harness. These two experiments use fundamentally different data sources (real GPU profiler vs. synthetic injection) and the paper should be clearer about this distinction.

### 3.3 Citation Issues

- **`ebpf-linux` (references.bib, line 64-69)**: This entry uses `@inproceedings` but has a `journal` field ("ACM Computing Surveys"). It should be `@article`. The author name "Vieira, Nelson Billing" appears to be the wrong citation for a general eBPF reference. A better citation would be the foundational eBPF documentation or the McCanne/Jacobson packet filter paper.

- **`msccl` (references.bib, line 29-34)**: The author is listed as "Cai, Zhihao and others" -- the same first author as AutoCCL. MSCCL's primary authors are Meghan Cowan et al. (or more recently, Abhinav Jangda et al. for MSCCLang). This appears to be an incorrect citation entry.

- **`autoccl` (references.bib, line 22-27)**: Listed at SC 2024. This should be verified -- AutoCCL may not have been published at SC.

- **General**: Several citation entries use "and others" instead of listing all authors. While this is acceptable in some contexts, top venue submissions should have complete author lists in the .bib file.

---

## 4. Figure Quality

### 4.1 Figure 1: Architecture (architecture.tex)

**Quality: Good.** This is a well-structured TikZ diagram showing the NCCL process, tuner/profiler adapters, bpftime runtime, maps, and policy programs. The flow arrows are labeled and the color coding distinguishes components clearly. The external `policy.bpf.o` loading path is a nice touch.

**Minor issues:**
- The node at (-0.72, 3.15) for `policy.bpf.o` extends outside the main bounding box, which may look awkward when rendered.
- The "Verifier -> JIT -> Maps -> Helpers" layout within the bpftime box implies a pipeline but these are actually components, not sequential stages. Consider a different layout that does not suggest sequentiality.

### 4.2 Figure 2: Overhead Staircase (overhead_staircase.tex)

**Quality: Good structure, but data is stale.** The bar chart with annotated delta callouts is an effective visualization of overhead decomposition. The incremental annotation style (+41 ns dispatch, +1 ns ctx, +11 ns lookup, etc.) tells a clear story.

**Issues:**
- **Data is wrong.** The bar heights and annotations match the old test run, not the latest. See Section 3.1. The current figure shows noop=51, but latest data shows noop=61. All subsequent bars are also wrong.
- The annotation "+1 ns ctx" between noop (51) and size_aware_v2 (52) is so small that it is not a meaningful measurement -- it is within noise. Claiming a "context access" cost of 1 ns is not credible.
- Similarly, "+1 ns logic" between lookup_update (74) and adaptive_channels (75) is noise, not signal.
- The x-axis labels at 28-degree rotation may be hard to read when printed.

### 4.3 Figure 3: Verifier Matrix (verifier_matrix.tex)

**Quality: Adequate.** The left panel showing 7 safe/7 unsafe is clear. The right panel comparing native crash vs. eBPF rejection for the same bug is a nice illustration of the core safety proposition.

**Issues:**
- The right panel's "same null-deref bug" comparison is effective but could be more impactful if it showed actual crash output vs. verifier error output (which it does in the paper text but not in the figure).
- The figure is diagram-style, not data-driven. For a systems paper, this is acceptable for a safety demonstration but it does not add much beyond what the table already shows.

### 4.4 Figure 4: Hot-Reload Timeline (hot_reload_timeline.tex)

**Quality: Good concept, stale data.** The timeline visualization effectively conveys the hot-reload flow -- old policy serving calls, verify+JIT running in parallel, atomic swap, new policy taking over.

**Issues:**
- **Stale numbers**: "0.309 us" swap and "298,126" first-changed-call do not match the latest run (0.774 us, 254,709).
- The "400,000 / 400,000 complete" annotation makes it look like all calls were under the old policy, but actually 254,709 were old and 145,292 were new (latest run). The figure should clarify this.
- The "bad replacement rejected" annotation in the upper right is somewhat disconnected from the main timeline -- it's not clear when this event occurs relative to the timeline.

### 4.5 Figure 5: Contention Time Series (contention_timeseries.tex)

**Quality: Good.** This is the most data-rich figure and tells a clear story. The three phases (baseline, contention, recovery) are visually distinct with colored backgrounds. The data points match the eval data exactly.

**Issues:**
- The x-axis label "Call count (x10^3)" combined with tick values {50, 100, ..., 300} means the actual call counts are 50K-300K. The eval data shows samples every 10K calls starting at 10K. The figure matches the eval data.
- No confidence intervals or error bars. Are these single-run numbers?
- The symmetric ramp-up/ramp-down is visually compelling but is an artifact of the policy's fixed +1/-1 adjustment logic, not evidence of feedback stability.

### 4.6 Figure 6: Closed Loop (closed_loop.tex)

**Quality: Weak.** This figure shows only 3 data points per line. A figure with 6 total data points on a single axis is not very informative and barely justifies the visual space. The "first sample 9.26 ms" and "no profiler updates" annotations add some context but the figure still feels thin.

**Issues:**
- Only 3 logged calls. This is an extremely small sample for demonstrating closed-loop behavior.
- Y-axis range is 7.7--10.3 with only integer tick marks at 8, 9, 10. The plot area is mostly whitespace.
- The "profiler disabled" line at flat 8 is conceptually useful but trivial.
- Consider replacing this with a longer time series or merging it with the contention figure.

---

## 5. Evaluation Gaps

### 5.1 Missing Experiments (in priority order)

1. **No outcome metrics.** The most glaring gap is that no experiment shows the eBPF-driven policy decisions *improving* any end-to-end metric (latency, throughput, job completion time, SLO satisfaction). The paper demonstrates that the mechanism works (policies can change parameters) but not that using it is beneficial. The paper acknowledges this (line 1301-1304), but acknowledging a gap does not fill it.

2. **No multi-GPU experiment.** The paper's motivating scenarios involve multi-GPU, multi-node training clusters with thousands of GPUs. The evaluation uses 1 GPU with 2 emulated ranks over socket transport. At minimum, a 2-GPU or 4-GPU experiment on a real multi-GPU system would strengthen the paper. The paper's argument that "CPU-side policy overhead is independent of rank count" (line 885) is reasonable but unvalidated.

3. **No comparison with alternative approaches.** The related work section (Section 7) now discusses WebAssembly, out-of-process services, declarative DSLs, and MPK -- a significant improvement over Round 1. However, there is no experimental comparison. Even a rough latency comparison (eBPF dispatch vs. IPC round-trip vs. Wasm call) would anchor the design choice empirically.

4. **No real-world plugin bug corpus.** The verifier evaluation remains 14 author-constructed programs. The paper now acknowledges this (line 1110-1119), which is good, but the evaluation would be much stronger with either (a) known bugs from NCCL GitHub issues or (b) systematically generated mutants.

5. **No reproducibility information.** The paper does not describe how to reproduce the experiments, what hardware specifications (CPU model, clock, cache sizes) were used for CPU microbenchmarks, or how core isolation was configured.

### 5.2 Research Questions

The five RQs are well-framed. RQ1-RQ3 are clearly scoped and answered with appropriate data (modulo the stale numbers issue). RQ4 is thin (one table row). RQ5 is framed as "policy expressiveness demonstration" rather than "policy effectiveness evaluation," which is honest -- but it also means the paper's strongest claim (verified extensibility) lacks a "so what?" answer.

### 5.3 Limitations

Limitations are now honestly stated in each subsection, which is a major improvement over Round 1. The discussion section (Section 6) acknowledges the restricted programming model, the limited evaluation scale, and the limited action space. This is commendable.

However, the limitation that the paper does not demonstrate *benefit* from the adaptive policies is buried in line 1301-1304 within RQ5, rather than in the Discussion. This is the most important limitation and should be prominent.

---

## 6. Specific Actionable Fixes (Prioritized by Impact)

### Critical (must fix before any submission)

1. **Reconcile all numbers with the latest test run.** The paper's overhead numbers (Table 1, Figure 2), overhead formula, hot-reload numbers (Table 2, Figure 4), and all textual mentions of "42--70 ns," "0.309 us," etc. must be updated to match the latest data. If the older numbers are preferred, the paper must explain which code version produced them and why those numbers are representative. Files affected: `paper.tex` lines 137, 203, 251-252, 257, 375-377, 613, 836, 896-910, 918-958, 1136, 1152, 1158, 1167, 1173, 1176, 1624; `figures/overhead_staircase.tex` all coordinates; `figures/hot_reload_timeline.tex` swap time and call boundary.

2. **Fix the MSCCL citation.** `references.bib` line 30: the MSCCL author is not "Cai, Zhihao." MSCCL (NSDI 2023) is by Meghan Cowan, Saeed Maleki, et al. (or possibly the MSCCLang paper by Jangda et al.). Verify all .bib entries against actual publications.

3. **Fix the ebpf-linux citation.** `references.bib` line 64: uses `@inproceedings` with a `journal` field. Should be `@article`. Verify this is the citation you actually want for eBPF background.

### High Priority (significantly improves the paper)

4. **Show all 10 message sizes in Table 3.** Currently only 128 MB is shown. If all 10 were measured, show them. This is easy and directly strengthens RQ4.

5. **Add a "Benefit" experiment or reframe.** Either (a) run one experiment showing that a policy-driven decision (e.g., switching from RING to TREE for small messages) actually improves latency compared to NCCL defaults, or (b) explicitly reframe the paper as a "mechanism paper" in the abstract and contributions. Currently the paper is positioned between mechanism and end-to-end but delivers only mechanism.

6. **Tighten the overhead formula.** With the latest numbers, the formula "41 + 11n ns" no longer holds. Either (a) update to match the latest data, or (b) re-run the experiments on the codebase corresponding to the paper's numbers and document which code version was used.

7. **Reduce redundancy in verifier coverage claims.** The "7/7 tested classes rejected" fact appears in the abstract (line 138), contributions (line 258), Table 2, Figure 3, the eval text (line 1044), and the conclusion (line 1625). Mentioning it in the abstract and contributions is sufficient; the eval section should present the detail. Remove from the conclusion or vary the phrasing.

### Medium Priority (improves readability and rigor)

8. **Merge Section 2.4 into Section 7.** The "Comparison with Prior Approaches" subsection (lines 402-420) duplicates material that appears more fully in Related Work. Move these two paragraphs into Section 7 and replace Section 2.4 with a forward reference.

9. **Add hardware specification.** The testbed description (lines 875-885) should include CPU model, clock speed, L1/L2/L3 cache sizes, and core isolation method. "Intel platform with isolated cores" is insufficient.

10. **Strengthen Figure 6 or remove it.** The closed-loop figure has only 3 data points per line. Either extend the experiment to show a longer time series (10+ data points) or remove the figure and describe the result in text only.

11. **Add error bars or confidence intervals.** None of the CPU microbenchmark numbers include variance information. If the 1M-call benchmarks were run once, say so. If multiple runs were performed, show standard deviation.

12. **Clarify the two data sources in RQ5.** The closed-loop experiment (5a) uses real GPU profiler timestamps. The contention experiment (5c) uses manually injected values in the CPU harness. These are qualitatively different experiments and should be clearly distinguished in the text.

### Low Priority (polish)

13. **Line 673**: Substantiate or remove the "200 lines" claim for the native plugin comparison.

14. **Line 958**: The formula "41 + 11n_lookup + 11n_update ns" uses subscripts that will render oddly in LaTeX outside math mode. Use $41 + 11n_{\text{lookup}} + 11n_{\text{update}}$ -- actually, this is already done correctly in the LaTeX source. No change needed here.

15. **References formatting**: Several entries use "and others." Replace with full author lists for a camera-ready submission.

16. **Line 189-194**: "While we do not have deployment-scale failure data for NCCL plugins specifically, these failure modes are well-known from decades of experience with kernel modules..." -- this is a reasonable hedge, but it would be stronger to cite at least one NCCL GitHub issue involving a plugin-related crash or hang.

---

## 7. Venue Recommendation

### Current State: Workshop Paper

The paper in its current form is appropriate for:

- **HotOS 2026** (if positioned as a provocative insight paper about eBPF for GPU runtimes)
- **APSys 2026** (as a short systems paper with the current evaluation)
- **EuroSys Workshop** (e.g., eBPF Workshop, GPU Systems Workshop)
- **arXiv** (as a technical report documenting the system and evaluation)

The paper is **not ready for OSDI, SOSP, NSDI, EuroSys, or ATC** as a full paper. These venues require:

1. **An evaluation that demonstrates end-to-end benefit**, not just mechanism feasibility. The paper currently shows that eBPF policies *can* run in NCCL with low overhead, but not that they *should* -- i.e., that the safety, hot-reload, and composability properties enable new capabilities that produce measurable improvements for real workloads.

2. **A multi-node, multi-GPU evaluation** at a scale where NCCL policy matters. Single-node, socket-transport, 2-rank experiments are too far from the deployment environments that motivate the paper.

3. **A comparison with at least one alternative approach** (out-of-process service, Wasm, MPK). The related work section discusses these qualitatively, but without experimental comparison, reviewers will question whether eBPF is the right choice.

### What Would Elevate to a Full Paper

To target OSDI/SOSP, the authors would need to:

1. **Demonstrate benefit at scale.** Run on a multi-node cluster with InfiniBand, hundreds of ranks, real training workloads (e.g., GPT training with FSDP). Show that an adaptive eBPF policy improves training throughput, reduces tail latency, or enforces SLO constraints compared to NCCL defaults and/or static native plugins.

2. **Build a richer policy ecosystem.** The current 7 policies are all relatively simple. A compelling full paper would include policies for fault resilience (e.g., detecting a slow link and re-routing), multi-tenant fairness (e.g., weighted bandwidth allocation), or workload-aware algorithm selection (e.g., MoE vs. dense model detection).

3. **Quantify the safety/usability improvement.** Beyond the 14-program verifier test, show: (a) how many real NCCL plugin bugs would be caught, (b) how much faster policy development is with eBPF vs. native (developer study or case study), (c) how hot-reload reduces downtime in a realistic operational scenario.

4. **Compare against alternatives experimentally.** Implement the same adaptive policy as a Wasm module, an out-of-process service, and a native plugin with custom shared memory. Compare overhead, safety, and developer effort.

5. **Clean up data integrity.** Use a single, final set of experimental numbers throughout the paper. Document the exact code version, build flags, and hardware used.

### Summary

The paper has a sound core idea (eBPF for NCCL plugins is feasible and the overhead is tolerable), a well-structured implementation, and honest limitation statements. The writing has improved substantially since Round 1. However, the evaluation remains insufficient for a top venue: it demonstrates mechanism but not benefit, operates at toy scale, and now has a data integrity problem with stale numbers. The most productive path forward is to (a) fix the numbers immediately, (b) target a workshop venue with the current artifact, and (c) invest in a multi-node evaluation with outcome metrics for a future full-paper submission.

**Decision: Reject for OSDI/SOSP. Resubmit to workshop venue after fixing data integrity issues (Section 6, Critical items 1-3).**
