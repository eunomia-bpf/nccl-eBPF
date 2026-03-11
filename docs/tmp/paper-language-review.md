# Paper Language Review: NCCLPol (paper.tex)

Reviewed against: top-tier systems venue style (OSDI/NSDI/EuroSys), honest claims, em/en-dash elimination, and general style issues.

---

## 1. Overstated or Unsupported Claims

### 1.1 "bimodal" — Line 542 (table caption) and Line 567 (body)

**Line 542, table caption:**
> `DEFAULT exhibits bimodal behavior (2 of 3 runs enter ``slow mode'')`

**Line 567, body:**
> `AllGather under the default configuration exhibits \emph{non-deterministic bimodal behavior}: across 3~independent runs at 128\,MiB, 2~runs produced only 2.08--2.11\,GB/s while the third achieved 4.61\,GB/s`

**Problem:** "Bimodal" is a statistical claim about a distribution having two modes. Three data points (2 slow, 1 fast) cannot establish a bimodal distribution; they only show two observed outcomes. In top-tier systems venues, reviewers will flag this as an overclaim. Additionally, calling it "bimodal behavior" in the caption while also calling it "non-deterministic bimodal behavior" in the body is redundant.

**Suggested replacements:**

- Caption (line 542): Replace `DEFAULT exhibits bimodal behavior (2 of 3 runs enter ``slow mode'')` with:
  `DEFAULT exhibits high variance across runs (2 of 3 runs enter a slow configuration);`

- Body (line 567): Replace `exhibits \emph{non-deterministic bimodal behavior}` with:
  `exhibits \emph{high run-to-run variance}`
  or:
  `exhibits \emph{non-deterministic behavior}, switching between a fast and a slow configuration`

**Reason:** Three runs cannot establish bimodality. The claim is more accurately described as "high variance" or "two observed performance modes across three runs."

---

### 1.2 "deterministic" — Lines 543, 576, 583, 585–586

**Line 543 (table caption):**
> `Simple is deterministic.`

**Line 576:**
> `Forcing \texttt{NCCL\_PROTO=Simple} eliminates this variance entirely: all 3~runs produce 4.62$\pm$0.01\,GB/s at 128\,MiB.`

**Line 583:**
> `confirming that the degradation is not inherent to the protocol choice but to non-deterministic initialization.`

**Lines 585–586:**
> `\emph{stability guarantees} through deterministic protocol steering.`

**Problem:** "Deterministic" is an absolute term. Three runs producing consistent output does not establish determinism; it establishes consistency in a small sample. A reviewer will correctly point out that you cannot claim determinism from 3 runs. "Non-deterministic initialization" is a reasonable mechanistic hypothesis but is stated as confirmed fact.

**Suggested replacements:**

- Line 543 caption: `Simple is deterministic.` → `Simple produces consistent results across all runs.`

- Line 576: "eliminates this variance entirely" is already accurate (consistent with the data) — keep this phrasing; it is more defensible than "deterministic." No change needed here.

- Line 583: `non-deterministic initialization` → `topology-sensitive initialization that produces variable outcomes`

- Lines 585–586: `\emph{stability guarantees} through deterministic protocol steering` → `\emph{improved stability} through consistent protocol selection`

**Reason:** "Deterministic" and "stability guarantees" are too strong for a 3-run experiment. "Consistent" and "improved stability" are defensible.

---

### 1.3 "stability guarantees" — Line 585

**Current text:**
> `\emph{stability guarantees} through deterministic protocol steering`

**Problem (in addition to 1.2 above):** "Guarantees" is an absolute claim. The paper provides evidence of improved stability in 3 runs, not a formal guarantee. Italicizing it amplifies the overclaim.

**Suggested replacement:** `\emph{improved run-to-run stability} through consistent protocol selection`

---

### 1.4 "eliminates" — Lines 136–137, 192–193, 575–576, 583, 659–660, 684

The word "eliminates" appears multiple times. In some cases it is defensible; in others it overstates.

**Line 136–137 (abstract):**
> `elimination of intermittent 2$\times$ AllGather throughput degradation caused by non-deterministic default behavior`

This is defensible — the data shows 0 of 3 Simple-forced runs entered slow mode vs. 2 of 3 for default. However, "caused by non-deterministic default behavior" states a causal mechanism more confidently than the data support. The data shows correlation with protocol choice, not a confirmed causal chain.

**Suggested replacement:** `reduction of intermittent 2$\times$ AllGather throughput degradation observed under the default configuration`

**Lines 192–193:**
> `load-time verification eliminates crash-inducing bugs`

**Problem:** "Eliminates" is too strong; the verifier catches the classes of bugs it checks for, not all crash-inducing bugs (e.g., it cannot catch semantic bugs, bugs in the trusted computing base, or resource exhaustion). The paper itself admits this in §3.2.

**Suggested replacement:** `load-time verification prevents the common crash-inducing bugs checked by the verifier`
or: `load-time verification catches crash-inducing memory-safety and termination bugs`

**Lines 575–576:**
> `Forcing \texttt{NCCL\_PROTO=Simple} eliminates this variance entirely`

This phrasing is accurate and specific (refers to "this variance" in the observed data). It is defensible. No change strictly required, but consider: `reduces this variance to near-zero` to hedge slightly.

**Lines 659–660 (conclusion/discussion body):**
> `a size-aware eBPF policy eliminates intermittent 2$\times$ AllGather throughput degradation from non-deterministic default behavior`

Same issue as lines 136–137. Preferred replacement: `a size-aware eBPF policy removes the intermittent 2$\times$ AllGather throughput degradation observed under the default configuration`

**Line 684 (conclusion paragraph):**
> `eliminates non-deterministic AllGather throughput variance (50\% CoV $\to$ 0.2\%)`

The quantitative framing here (CoV 50% → 0.2%) is the most defensible form, since it's a concrete number rather than an absolute claim. The word "eliminates" is still aggressive; consider: `reduces AllGather throughput variance from 50\% to 0.2\% CoV`. But this instance is the most justifiable of the group.

---

### 1.5 "confirming" — Line 578–580

**Current text:**
> `When DEFAULT operates normally (run~3), its throughput matches Simple (4.61 vs.\ 4.63\,GB/s), confirming that the degradation is not inherent to the protocol choice but to non-deterministic initialization.`

**Problem:** "Confirming" overstates what one data point establishes. One matching run does not confirm a causal claim about initialization.

**Suggested replacement:** `When DEFAULT operates normally (run~3), its throughput matches the Simple-forced result (4.61 vs.\ 4.63\,GB/s), suggesting that the degradation arises from initialization rather than from an inherent protocol throughput difference.`

---

### 1.6 "making cross-vendor portability straightforward" — Line 668

**Current text:**
> `AMD's RCCL~\cite{rccl} shares the same plugin architecture, making cross-vendor portability straightforward.`

**Problem:** This claim is asserted without evidence; the paper has no RCCL experiments. "Straightforward" is imprecise for an academic paper.

**Suggested replacement:** `AMD's RCCL~\cite{rccl} shares a similar plugin architecture, suggesting that cross-vendor portability is feasible; we leave this as future work.`

---

### 1.7 "zero call loss" / "zero measurable overhead" — Lines 530, 501, 587–588

These phrasings are fine given the experimental context and caveats given. No change needed.

---

### 1.8 "catches common crash-inducing bugs before execution" — Abstract, Line 131

**Current text:**
> `load-time verification that catches crash-inducing bugs before execution`

Minor point: this is slightly overclaimed because "crash-inducing bugs" could include bugs the verifier does not check (e.g., semantic). Better to scope it:

**Suggested replacement:** `load-time verification that catches memory-safety and termination bugs before execution`

---

## 2. Em-Dashes and En-Dashes Used as Punctuation

The user requests ALL em-dashes/en-dashes used as punctuation be replaced with other punctuation.

Note: `--` in LaTeX produces an en-dash; `---` produces an em-dash. Numeric ranges (e.g., `52--100\ns`) should NOT be changed. Only dashes used as clause-separating punctuation should be replaced.

The following are em/en-dashes used as punctuation (not numeric ranges):

---

**Line 124:**
> `We observe that NCCL's CPU-side decision path is latency-tolerant---the`

`---` used as an em-dash to introduce a parenthetical explanation.

**Suggested replacement:** `We observe that NCCL's CPU-side decision path is latency-tolerant: the`

**Reason:** A colon correctly introduces the elaboration.

---

**Line 254–255:**
> `Three properties make eBPF well-suited: (1)~a generous overhead budget---\texttt{getCollInfo()} runs on the CPU before GPU-side collectives that take microseconds to milliseconds, so 50--100\ns of eBPF overhead is negligible;`

The `---` after `budget` is an em-dash used to introduce a parenthetical. Note that `50--100\ns` is a numeric range and should NOT be changed.

**Suggested replacement:** `(1)~a generous overhead budget (specifically, \texttt{getCollInfo()} runs on the CPU before GPU-side collectives that take microseconds to milliseconds, so 50--100\ns of eBPF overhead is negligible);`

**Reason:** Parentheses correctly mark the elaboration; keeps the sentence flowing.

---

**Lines 311–312:**
> `This provides defense in depth---eliminating crashes, hangs, and memory corruption---analogous to the Linux kernel's eBPF model.`

Two em-dashes used as parenthetical delimiters.

**Suggested replacement:** `This provides defense in depth (eliminating crashes, hangs, and memory corruption), analogous to the Linux kernel's eBPF model.`

**Reason:** Parentheses work cleanly here; restructured to avoid double-dash pair.

---

**Lines 629–630:**
> `Out-of-process services provide strong isolation but add microseconds of IPC latency---an order of magnitude more than \sysname's 52--100\ns in-process overhead.`

`---` introduces an elaboration. `52--100\ns` is a numeric range, keep as-is.

**Suggested replacement:** `Out-of-process services provide strong isolation but add microseconds of IPC latency, which is an order of magnitude more than \sysname's 52--100\ns in-process overhead.`

**Reason:** Relative clause avoids the em-dash and reads more formally.

---

**Lines 439:**
> `The total reload cost is $\sim$7.3\,ms; only the final swap (0.774\us) touches the hot path.`

This uses a semicolon, not a dash — no change needed here.

---

**Lines 480:**
> `Native baseline    &  10 &  16 & --- \\`

This `---` is in a table cell meaning "not applicable." This is correct typographic usage and should NOT be changed.

---

**Summary of em-dash replacements needed (punctuation only):**

| Line | Current | Suggested |
|------|---------|-----------|
| 124 | `latency-tolerant---the` | `latency-tolerant: the` |
| 255 | `budget---\texttt{getCollInfo()}` | `budget (\texttt{getCollInfo()}` ... `negligible);` |
| 311–312 | `depth---eliminating...corruption---analogous` | `depth (eliminating...corruption), analogous` |
| 629–630 | `latency---an order` | `latency, which is an order` |

---

## 3. Language Quality and Academic Style Issues

### 3.1 "makes four contributions" / numbered list redundancy — Lines 202–221

**Current text (line 202):**
> `This paper makes four contributions:`

followed by a numbered list where item 1 describes NCCLPol itself (the whole system), items 2–4 describe specific results. Item 1 says:

> `\textbf{\sysname}, a system that embeds verified eBPF policy execution inside NCCL's plugin interface with no NCCL modifications (\S\ref{sec:design}--\ref{sec:impl}).`

**Problem:** In top-tier systems papers, the contributions list should be action-oriented and specific. Item 1 ("a system that...") is too high-level and restates the abstract. It is common practice in OSDI/NSDI to have each contribution be a technical novelty claim.

**Suggested revision for item 1:**
> `\textbf{Design and implementation of \sysname}, embedding verified eBPF policy execution inside NCCL's plugin interface with no NCCL modifications (\S\ref{sec:design}--\ref{sec:impl}).`

---

### 3.2 Informal phrasing: "makes X contributions" — Line 202

**Current:** `This paper makes four contributions:`

**Preferred academic phrasing:** `This paper makes the following contributions:`

**Reason:** "Four contributions" followed by a list of four items is slightly redundant and reads as slightly informal. "The following contributions" is more standard and survives if the list changes.

---

### 3.3 "The situation worsens when" — Lines 249–251

**Current text:**
> `The situation worsens when operators must roll out plugin updates across a fleet without a mechanism for safe hot-reload.`

**Problem:** "The situation worsens" is informal/journalistic. In academic writing, this should be more precise.

**Suggested replacement:** `This problem is compounded when operators must deploy plugin updates across a production fleet without a mechanism for safe live update.`

---

### 3.4 "fits" — Line 183 (section heading paragraph)

**Current text:**
> `We observe that eBPF's execution model is a natural fit for this setting.`

**Problem:** "Natural fit" is informal.

**Suggested replacement:** `We observe that eBPF's execution model aligns well with the requirements of this setting.`

---

### 3.5 Vague quantifier "approximately" — Lines 399–400

**Current text:**
> `\sysname is implemented in approximately 2{,}500 lines of C/C++ (plugin host) and 500 lines of restricted C (policy programs).`

This is fine for a systems paper. No change needed.

---

### 3.6 "This contrasts with eBPF's traditional domain" — Lines 188–190

**Current text:**
> `This contrasts with eBPF's traditional domain---packet processing and syscall filtering---where nanosecond budgets are tight.`

This is a good observation and the sentence is well-constructed, except for the em-dashes (already flagged above in §2). After dash replacement:

`This contrasts with eBPF's traditional domain (packet processing and syscall filtering), where nanosecond budgets are tight.`

---

### 3.7 "ruling out closed-loop control" — Lines 174–175

**Current text:**
> `A profiler that measures collective latency cannot pass that information to a tuner that could adapt---ruling out closed-loop control.`

Em-dash already flagged. After replacement:

**Suggested:** `A profiler that measures collective latency cannot pass that information to a tuner that could adapt, precluding closed-loop control.`

**Reason:** "Ruling out" is slightly informal; "precluding" is more academic. Also removes the em-dash.

---

### 3.8 Redundant statement about zero NCCL modifications — Multiple locations

The claim "zero NCCL modifications" appears in:
- Abstract (line 129)
- Introduction contributions list (line 206)
- Broader applicability / conclusion paragraph (line 687)

Three mentions across 6 pages is excessive and makes it feel like a marketing pitch. Consider dropping one (the contributions list mention is the most redundant since the design section covers it).

---

### 3.9 "garbage" comparison data ("--- " in table) — Line 480

Already noted in §2: `---` in the table is correct "not applicable" notation. No change.

---

### 3.10 Inconsistent verb tense in design tensions — §3.1

The Design Tensions section uses present tense throughout, which is correct. No issue.

---

### 3.11 "which is why JIT-compiled eBPF approaches native-code performance" — Lines 410–411

**Current text:**
> `The LLVM JIT produces optimized x86-64 code that benefits from the same optimization passes as \texttt{clang -O2}, which is why JIT-compiled eBPF approaches native-code performance.`

**Problem:** "Which is why" is a causal connector that is slightly informal and states this as a known conclusion rather than a measured result. In reality, the paper does not show that JIT-compiled eBPF matches native performance — Table 1 shows 10ns native vs. 61ns for noop, which is 6x slower at the microbenchmark level.

**Suggested replacement:** `The LLVM JIT produces optimized x86-64 code using the same optimization passes as \texttt{clang -O2}.`

**Reason:** Remove the unsupported causal claim about "native-code performance" — the data does not support it (61ns vs. 10ns baseline).

---

### 3.12 "approaches native-code performance" is factually inconsistent with Table 1

Building on 3.11: Table 1 shows noop = 61ns vs. native = 10ns — a 6x gap. Claiming the JIT "approaches native-code performance" contradicts the paper's own data. This must be fixed.

**Replacement as above:** Simply delete the "which is why..." clause.

---

### 3.13 "as a side effect" — Line 583

**Current text:**
> `Our \texttt{size\_aware} policy (Listing~\ref{lst:size-aware}) always selects Simple protocol, which as a side effect eliminates this bimodal behavior.`

**Problem:** "As a side effect" is informal and downplays the result. Also "eliminates this bimodal behavior" inherits the problems of "bimodal" and "eliminates" already flagged.

**Suggested replacement:** `Our \texttt{size\_aware} policy (Listing~\ref{lst:size-aware}) always selects Simple protocol; in doing so, it also removes the high run-to-run variance observed in the default configuration.`

---

### 3.14 "more than \sysname's 52--100\ns" — formatting note

The form `\sysname's` (using an apostrophe-s on a macro) is correct LaTeX. No change needed.

---

### 3.15 Repeated phrase "zero measurable GPU-level overhead" — Lines 138, 587

**Line 138 (abstract):** `zero measurable GPU-level overhead`
**Line 587:** `zero measurable GPU-level overhead`

This phrase appears verbatim in both the abstract and the stability section. Consider varying the phrasing in one location.

**Line 587 suggested variation:** `no detectable impact on GPU-level throughput`

---

### 3.16 "Testbed" paragraph structure — Lines 458–462

**Current text:**
> `CPU microbenchmarks run on a 24-core x86-64 platform with isolated cores; each benchmark performs 1\,million calls and reports P50/P99 latencies. GPU experiments use a single NVIDIA RTX~5090 with CUDA~12.9 and NCCL~2.29.7, emulating 2~ranks over socket transport.`

**Problem:** "Emulating 2 ranks" is slightly imprecise. The paper uses 2 MPI ranks on one GPU with NCCL_HOSTID hack. The word "emulating" may invite reviewer questions about whether results are representative.

**Suggested replacement:** `GPU experiments use a single NVIDIA RTX~5090 with CUDA~12.9 and NCCL~2.29.7; two MPI ranks communicate over socket transport on the same host, which isolates collective policy overhead from network variability.`

**Reason:** More precise, and the framing "isolates overhead" turns a potential weakness into a methodological strength.

---

### 3.17 Missing hedging for the 400k invocations hot-reload claim — Line 530

**Current text:**
> `Across 400{,}000 continuous invocations, we observe zero lost calls.`

This is precise and fine. No change needed.

---

### 3.18 "operations teams" — Line 178

**Current text:**
> `Operations teams that must iterate on tuning policies across heterogeneous clusters have no way to roll out changes safely at runtime.`

**Problem:** "Operations teams" is the only place in the paper that uses this informal/industry term. Academic papers typically say "cluster operators" or "system operators."

**Suggested replacement:** `Cluster operators who must iterate on tuning policies across heterogeneous clusters have no mechanism to deploy updates safely at runtime.`

---

### 3.19 "present a barrier" — Line 649

**Current text:**
> `The restricted C programming model is familiar to eBPF developers but presents a barrier for ML engineers`

This is acceptable. No change strictly needed. Alternatively: "imposes a barrier" for a slightly stronger register.

---

### 3.20 Comma splice — Lines 163–169

The bullet items in the safety problems list are well-constructed. No issues.

---

### 3.21 Overuse of "significant" — Line 155

**Current text:**
> `The choice of algorithm (ring vs.\ tree), protocol (LL vs.\ Simple), and channel count significantly affects collective latency`

"Significantly" is vague in academic writing (it could mean "statistically significant" or just "a lot"). The citation `\cite{autoccl, msccl}` helps, but the word is imprecise.

**Suggested replacement:** `The choice of algorithm (ring vs.\ tree), protocol (LL vs.\ Simple), and channel count has substantial impact on collective latency`

---

### 3.22 "with no NCCL modifications" vs "zero NCCL modifications" inconsistency

- Line 129 (abstract): "requiring zero NCCL modifications"
- Line 206 (contributions): "with no NCCL modifications"
- Line 687 (conclusion): "zero NCCL modifications"

Choose one form and use it consistently. "Requires no NCCL modifications" is cleanest.

---

## 4. Summary Table of All Issues

| # | Lines | Category | Severity | Issue |
|---|-------|----------|----------|-------|
| 1.1 | 542, 567 | Overclaim | High | "bimodal" from 3 runs |
| 1.2 | 543, 576, 583, 585–586 | Overclaim | High | "deterministic" from 3 runs |
| 1.3 | 585 | Overclaim | High | "stability guarantees" — too absolute |
| 1.4a | 136–137 | Overclaim | Medium | "elimination" / causal claim in abstract |
| 1.4b | 192–193 | Overclaim | Medium | "eliminates crash-inducing bugs" — too broad |
| 1.4c | 659–660 | Overclaim | Medium | "eliminates... degradation from non-deterministic behavior" |
| 1.5 | 578–580 | Overclaim | Medium | "confirming" from one run |
| 1.6 | 668 | Overclaim | Medium | RCCL portability "straightforward" — unverified |
| 1.8 | 131 | Overclaim | Low | "crash-inducing bugs" without scoping |
| 2 | 124 | Em-dash | Required | `---` as punctuation → colon |
| 2 | 255 | Em-dash | Required | `---` as punctuation → parentheses |
| 2 | 311–312 | Em-dash | Required | double `---` → parentheses |
| 2 | 629–630 | Em-dash | Required | `---` as punctuation → relative clause |
| 2 | 174–175 | Em-dash | Required | `---` in item body → comma |
| 2 | 188–190 | Em-dash | Required | `---...---` → parentheses |
| 3.2 | 202 | Style | Low | "four contributions" → "the following contributions" |
| 3.3 | 249 | Style | Low | "situation worsens" informal |
| 3.4 | 183 | Style | Low | "natural fit" informal |
| 3.11 | 410–411 | Factual error | High | "approaches native-code performance" contradicts Table 1 |
| 3.12 | 410–411 | Factual error | High | Same — 6x gap in data |
| 3.13 | 583 | Style + overclaim | Medium | "as a side effect eliminates this bimodal behavior" |
| 3.15 | 138, 587 | Style | Low | Exact phrase repeated verbatim |
| 3.16 | 458–462 | Precision | Medium | "emulating 2 ranks" — should clarify methodology |
| 3.18 | 178 | Style | Low | "operations teams" → "cluster operators" |
| 3.21 | 155 | Style | Low | "significantly" — vague |
| 3.22 | 129, 206, 687 | Consistency | Low | Inconsistent phrasing of "zero/no NCCL modifications" |

---

## 5. Priority Order for Fixes

**Must fix before submission (correctness / reviewer rejection risk):**
1. Remove "bimodal" (lines 542, 567) — replace with "high run-to-run variance" or "two observed performance modes"
2. Remove "deterministic" (lines 543, 585–586) — replace with "consistent" / "improved stability"
3. Remove "stability guarantees" (line 585) — replace with "improved stability"
4. Fix "approaches native-code performance" (lines 410–411) — contradicts own Table 1 data
5. Fix all 6 em-dashes used as punctuation (lines 124, 174–175, 188–190, 255, 311–312, 629–630)

**Should fix (moderate overclaim or style):**
6. Soften "elimination of... degradation" in abstract and conclusion (lines 136–137, 659–660, 684) → "reduction of"
7. Soften "eliminates crash-inducing bugs" (line 192–193) → "catches memory-safety and termination bugs"
8. Replace "confirming" (line 578) → "suggesting"
9. Fix RCCL "straightforward" claim (line 668)
10. Fix "emulating 2 ranks" testbed phrasing (lines 458–462)

**Polish (style, consistency):**
11. "The following contributions" (line 202)
12. "situation worsens" → "is compounded" (line 249)
13. "operations teams" → "cluster operators" (line 178)
14. Deduplicate "zero measurable GPU-level overhead" (lines 138, 587)
15. "significantly affects" → "has substantial impact on" (line 155)
16. Harmonize "zero/no NCCL modifications" wording (lines 129, 206, 687)
17. "as a side effect" → "in doing so, it also" (line 583)
