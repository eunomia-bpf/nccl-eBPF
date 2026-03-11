# Proofreading Notes: paper.tex

Fine-grained word/phrase-level issues only. No structural suggestions.

---

## Abstract

**Issue 1 — Missing article**
- Line ~120: `"a plugin requires restarting the job"`
- Current: `"Updating a plugin requires restarting the job."`
- Suggested: `"Updating a plugin requires restarting the job, leaving operators no way to iterate safely at runtime."`
- Reason: No issue here — this is fine as is. (Skipped.)

**Issue 2 — Run-on / unclear antecedent**
- Lines 124–127: `"We observe that NCCL's CPU-side decision path is latency-tolerant: the GPU collective it configures takes microseconds to milliseconds, making eBPF's verification overhead negligible in exchange for safety, composability, and live update."`
- Current: `"in exchange for safety, composability, and live update"`
- Suggested: `"in exchange for safety, composability, and live updates"`
- Reason: "live update" is a noun/verb phrase used as a noun; pluralizing ("live updates") makes it parallel with "safety" and "composability" and reads more naturally.

---

## Section 1 — Introduction

**Issue 3 — Dangling "this setting" antecedent**
- Lines 169–170: `"We observe that eBPF's execution model is well-suited to this setting."`
- Current: `"this setting"`
- Suggested: `"this context"` or `"this scenario"`
- Reason: "this setting" is vague; the previous paragraph described the problem space but "setting" has not been introduced as a named concept. "context" or "scenario" is more precise.

**Issue 4 — Comma splice / missing comma after introductory clause**
- Lines 184–188: `"The system loads via standard \texttt{NCCL\_TUNER\_PLUGIN} and \texttt{NCCL\_PROFILER\_PLUGIN} environment variables, requiring zero modifications to NCCL. This paper makes four contributions:"`
- Current: `"This paper makes four contributions:"` (new sentence starts immediately after the previous)
- Suggested: Add one sentence of transition, OR keep as is — this is actually fine. (Skipped.)

**Issue 5 — Inconsistent hyphenation: "hot-reload" vs. "hot reload"**
- Lines 132, 200, 421, 515, 519, 671: The paper alternates between `hot-reload` (hyphenated, used as noun/verb) and `hot reload` (unhyphenated).
- Occurrences:
  - Line 132: `"atomic policy hot-reload"` (hyphenated, used as noun — correct as modifier)
  - Line 200: `"Atomic policy hot-reload"` (hyphenated noun — consistent)
  - Line 421: `"\paragraph{Hot-reload mechanism.}"` (hyphenated — OK as heading noun)
  - Line 515: `"Hot-reload swaps the active policy"` — here "Hot-reload" is used as a noun subject (hyphenated — acceptable)
  - Line 519: `"If the replacement fails verification"` — no issue here
  - Line 671: `"supports atomic hot-reload"` (hyphenated — consistent)
- Decision: The paper is consistently hyphenated throughout; no fix needed. (Skipped.)

**Issue 6 — Redundancy: "requiring zero modifications to NCCL" and "requiring no source modifications"**
- Line 187: `"requiring zero modifications to NCCL"`
- Line 281: `"requiring no source modifications"`
- Lines 673–674: `"requires zero NCCL modifications"`
- These three phrasings are not inconsistent per se, but mixing "zero modifications to NCCL," "no source modifications," and "zero NCCL modifications" for the same claim looks unpolished. Pick one form.
- Suggested: Standardize to `"requiring zero NCCL modifications"` throughout (matching the Conclusion, which is the most explicit).
  - Line 187: `"requiring zero modifications to NCCL"` → `"requiring zero NCCL modifications"`
  - Line 281: `"requiring no source modifications, no custom builds, and no kernel modules"` — this is more detailed and should stay as is.

---

## Section 2 — Background and Motivation

**Issue 7 — Missing comma after introductory "In all cases"**
- Lines 234–235: `"In all cases, the root cause is that \texttt{dlopen}-based extensions have no safety net."`
- Current: comma is present — no issue. (Skipped.)

**Issue 8 — Awkward "is compounded when"**
- Lines 235–237: `"The problem is compounded when operators must roll out plugin updates across a fleet without a mechanism for safe hot-reload."`
- Current: `"when operators must roll out plugin updates across a fleet without a mechanism for safe hot-reload"`
- Suggested: `"when operators must roll out plugin updates across a fleet without a safe hot-reload mechanism"`
- Reason: Moving "safe" before "hot-reload mechanism" avoids the awkward prepositional chain "without a mechanism for safe hot-reload" and reads more naturally.

**Issue 9 — Inconsistent capitalization: "Three properties"**
- Lines 240–246: `"Three properties make eBPF well-suited: (1)~a generous overhead budget..."`
- The paragraph is fine, but "(2)~typed maps that provide concurrent, verified state sharing between profiler and tuner without ad-hoc shared memory" — "ad-hoc" should be hyphenated as "ad hoc" (Latin phrase used adverbially; only hyphenated when used as a compound adjective before a noun, which it is not here).
- Current: `"without ad-hoc shared memory"`
- Suggested: `"without ad hoc shared memory"`
- Reason: "ad hoc" as an adverbial modifier of "shared" is not a compound adjective modifying a noun, so "ad-hoc" is a hyphenation error. However, note that later usage at line 268 also uses "ad-hoc" in the same grammatical position — both should be changed consistently.

  - Line 244: `"without ad-hoc shared memory"` → `"without ad hoc shared memory"`
  - Line 268: `"the locking bugs that plague ad-hoc native plugin state sharing"` → `"the locking bugs that plague ad hoc native plugin state sharing"`

---

## Section 3 — Design

**Issue 10 — Missing article before "abstract interpretation"**
- Lines 317–318: `"verified (abstract interpretation over all execution paths)"`
- Current: `"abstract interpretation over all execution paths"`
- Suggested: `"verified (via abstract interpretation over all execution paths)"`
- Reason: The parenthetical is a noun phrase without a verb; adding "via" makes the mechanism explicit and grammatically complete. Alternatively, "using abstract interpretation" works equally well.

**Issue 11 — Comma missing after "Hot-reload" in the sentence describing the mechanism**
- Lines 320–322: `"Hot-reload verifies and JIT-compiles a replacement, then atomically swaps the function pointer via compare-and-swap."`
- No issue — sentence is grammatically fine. (Skipped.)

**Issue 12 — "the same check that prevents" — unclear antecedent**
- Lines 328–330: `"Map lookups require null checks before dereference, the same check that prevents the most common native plugin crash (null-pointer dereference after failed lookup)."`
- Current: `"the same check that prevents the most common native plugin crash"`
- Suggested: `"the same check that would prevent the most common native plugin crash in native code"`
- Reason: In context, the null check is not preventing the native crash — it IS the check that native plugins fail to make. "Would prevent" more accurately conveys that native code lacks this check. Alternatively: `"the exact check that native plugins must perform manually but frequently omit"`.

**Issue 13 — Dangling modifier / awkward phrasing**
- Lines 373–377: `"This closed-loop control has no built-in support in NCCL's native plugin architecture, where tuner and profiler plugins share no state mechanism."`
- Current: `"share no state mechanism"`
- Suggested: `"share no state-sharing mechanism"` or more simply: `"have no shared state mechanism"`
- Reason: "share no state mechanism" is contradictory-sounding (they "share" nothing, including a "state mechanism"). "Have no shared state mechanism" reads more clearly.

---

## Section 4 — Implementation

**Issue 14 — "approximately 2{,}500 lines" — precision**
- Line 385: `"\sysname is implemented in approximately 2{,}500 lines of C/C++ (plugin host) and 500 lines of restricted C (policy programs)."`
- No issue — "approximately" covers both numbers, which is fine. (Skipped.)

**Issue 15 — "the same optimization passes as \texttt{clang -O2}" — missing article**
- Lines 395–397: `"the same optimization passes as \texttt{clang -O2}, reducing the gap between JIT-compiled eBPF and native code."`
- No article issue here. (Skipped.)

**Issue 16 — Vague "gracefully" without specifics**
- Lines 412–413: `"allowing NCCL to fall back gracefully if the requested combination is unavailable"`
- Current: `"fall back gracefully"`
- Suggested: `"fall back to its default selection"`
- Reason: "gracefully" is vague; what does NCCL fall back to? Specifying "to its default selection" or "to the next best choice" is more precise.

**Issue 17 — Awkward relative clause: "which is acceptable because..."**
- Lines 276–277: `"which is acceptable because collective decisions are independent per call"`
- Current: `"are independent per call"`
- Suggested: `"are independent across calls"`
- Reason: "independent per call" is redundant — if each call is independent, you don't need "per." "Independent across calls" is the standard phrasing for this concept.

---

## Section 5 — Evaluation

**Issue 18 — Missing comma in parenthetical list**
- Lines 486–488: `"all tested configurations (no plugin, \texttt{noop}, \texttt{size\_aware\_v2}, \texttt{slo\_enforcer}) produce statistically indistinguishable AllReduce latencies."`
- The list is correct with Oxford comma. No issue. (Skipped.)

**Issue 19 — "We hypothesize that NCCL's topology-dependent initialization intermittently assigns a suboptimal internal configuration."**
- Lines 558–560: Current text is acceptable but "intermittently assigns a suboptimal internal configuration" is somewhat vague.
- Current: `"intermittently assigns a suboptimal internal configuration"`
- Suggested: `"intermittently selects a suboptimal internal protocol configuration"`
- Reason: "assigns" implies active delivery from outside; "selects" is more accurate for an initialization routine making a choice. "Protocol configuration" is more specific than "internal configuration" given the context.

**Issue 20 — "as a side effect eliminates" — missing comma**
- Lines 569–572: `"Our \texttt{size\_aware} policy (Listing~\ref{lst:size-aware}) always selects Simple protocol, which as a side effect eliminates this run-to-run variance."`
- Current: `"which as a side effect eliminates"`
- Suggested: `"which, as a side effect, eliminates"`
- Reason: "as a side effect" is a parenthetical expression and should be set off with commas.

**Issue 21 — "The policy adds zero measurable GPU-level overhead versus NCCL's default when DEFAULT operates in its fast mode."**
- Lines 573–575: `"The policy adds zero measurable GPU-level overhead versus NCCL's default when DEFAULT operates in its fast mode."`
- Current: `"when DEFAULT operates in its fast mode"`
- Suggested: `"when the default operates in its fast mode"` (lowercase "default" for consistency) OR clarify with `"when NCCL's default configuration operates in its fast mode"`
- Reason: "DEFAULT" in all-caps is used as a label in Table 2, but here it refers to a configuration state; switching to lowercase "default" (or "NCCL's default") reduces visual confusion. The capitalized "DEFAULT" appears in the table column head but not elsewhere in prose.

---

## Section 6 — Related Work

**Issue 22 — Comma splice: "they do not address runtime policy selection or safety"**
- Lines 591–592: `"MSCCL~\cite{msccl} and TACCL~\cite{taccl} synthesize static schedules offline; they do not address runtime policy selection or safety."`
- The semicolon is correct here. No issue. (Skipped.)

**Issue 23 — Inconsistent: "same insight" used twice**
- Line 603: `"exploiting the same insight that certain system paths are latency-tolerant enough for eBPF overhead"`
- Current: `"certain system paths are latency-tolerant enough for eBPF overhead"`
- Suggested: `"certain system paths are latency-tolerant enough to absorb eBPF overhead"`
- Reason: "latency-tolerant enough for eBPF overhead" is slightly elliptical. "Enough to absorb eBPF overhead" is the clearer phrasing used in the Conclusion (line 667: "latency-tolerant enough to absorb eBPF overhead"). Align these for consistency.

**Issue 24 — "its runtime overhead is typically higher due to bounds checks that eBPF eliminates statically"**
- Lines 613–615: `"its runtime overhead is typically higher due to bounds checks that eBPF eliminates statically"`
- Current: `"bounds checks that eBPF eliminates statically"`
- Suggested: `"bounds checks that eBPF's static verifier eliminates"`
- Reason: "eliminates statically" sounds like the adverb modifies the action (eliminating), which is correct but informal. "eBPF's static verifier eliminates" makes the agent explicit and reads more precisely.

---

## Section 7 — Discussion and Conclusion

**Issue 25 — "does not map cleanly to eBPF's restricted model"**
- Lines 631–632: `"the net plugin involves complex state (RDMA queue pairs, memory registrations) that does not map cleanly to eBPF's restricted model."`
- Current: `"does not map cleanly to eBPF's restricted model"`
- Suggested: `"does not map cleanly to eBPF's restricted execution model"`
- Reason: "restricted model" is ambiguous (could be a data model, programming model, etc.). "Restricted execution model" is precise.

**Issue 26 — "a higher-level policy DSL compiled to BPF would improve usability"**
- Lines 636–637: `"a higher-level policy DSL compiled to BPF would improve usability"`
- Current: `"compiled to BPF"`
- Suggested: `"compiled to BPF bytecode"`
- Reason: "BPF" alone is ambiguous; "BPF bytecode" is the standard, unambiguous term used elsewhere in systems papers and in this paper's own abstract.

**Issue 27 — "Full evaluation on multi-node production workloads at scale is the key next step."**
- Lines 648–650: `"Full evaluation on multi-node production workloads at scale is the key next step."`
- Current: `"Full evaluation on multi-node production workloads at scale"`
- Suggested: `"Full evaluation on multi-node production workloads at scale remains the key next step."`
- Reason: "is the key next step" reads abruptly. "Remains" acknowledges that this was already anticipated and signals it as the forward-looking priority, which is the intended tone.

**Issue 28 — "reduces AllGather throughput variance from 50\% to 0.2\% coefficient of variation on our testbed"**
- Lines 672–673: `"reduces AllGather throughput variance from 50\% to 0.2\% coefficient of variation on our testbed"`
- Current: `"from 50\% to 0.2\% coefficient of variation"`
- Suggested: `"from 50\% to 0.2\% (coefficient of variation) on our testbed"`
- Reason: The parenthetical clarifier "(coefficient of variation)" should be set off with parentheses rather than following the numbers inline, to make clear that the CV metric applies to both the 50% and 0.2% figures.

---

## Summary of Action-Required Fixes (prioritized)

| # | Location | Current | Fix | Type |
|---|----------|---------|-----|------|
| 2 | Abstract, line ~127 | "live update" | "live updates" | Parallel structure |
| 8 | §2, line ~236 | "without a mechanism for safe hot-reload" | "without a safe hot-reload mechanism" | Awkward phrasing |
| 9 | §2, lines ~244, ~268 | "ad-hoc shared memory" / "ad-hoc native plugin" | "ad hoc shared memory" / "ad hoc native plugin" | Hyphenation error (×2) |
| 10 | §3, line ~317 | "verified (abstract interpretation...)" | "verified (via abstract interpretation...)" | Missing preposition |
| 12 | §3, lines ~328–330 | "the same check that prevents" | "the same check that native plugins must perform manually but frequently omit" (or similar) | Unclear antecedent |
| 13 | §3, lines ~373–377 | "share no state mechanism" | "have no shared state mechanism" | Contradictory phrasing |
| 16 | §4, lines ~412–413 | "fall back gracefully" | "fall back to its default selection" | Vague word |
| 17 | §3, lines ~276–277 | "independent per call" | "independent across calls" | Awkward phrasing |
| 19 | §5, lines ~558–560 | "intermittently assigns a suboptimal internal configuration" | "intermittently selects a suboptimal internal protocol configuration" | Precision + word choice |
| 20 | §5, lines ~569–572 | "which as a side effect eliminates" | "which, as a side effect, eliminates" | Missing commas |
| 21 | §5, lines ~573–575 | "when DEFAULT operates" | "when the default operates" | Inconsistent capitalization |
| 23 | §6, line ~603 | "latency-tolerant enough for eBPF overhead" | "latency-tolerant enough to absorb eBPF overhead" | Elliptical phrasing; align with Conclusion |
| 24 | §6, lines ~613–615 | "bounds checks that eBPF eliminates statically" | "bounds checks that eBPF's static verifier eliminates" | Precision |
| 25 | §7, lines ~631–632 | "eBPF's restricted model" | "eBPF's restricted execution model" | Ambiguous noun |
| 26 | §7, lines ~636–637 | "compiled to BPF" | "compiled to BPF bytecode" | Imprecise term |
| 27 | §7, lines ~648–650 | "is the key next step" | "remains the key next step" | Tone/precision |
| 28 | §7, lines ~672–673 | "50\% to 0.2\% coefficient of variation" | "50\% to 0.2\% (coefficient of variation)" | Parenthetical clarity |

Also flag for consistency:
- Line ~187: `"requiring zero modifications to NCCL"` → `"requiring zero NCCL modifications"` to match the Conclusion phrasing (line ~673).
