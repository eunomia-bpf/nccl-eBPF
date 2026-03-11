# Citation Audit: NCCLbpf Paper

**Paper:** `docs/paper/paper.tex`
**Bibliography:** `docs/paper/references.bib`
**Audit date:** 2026-03-10

---

## 1. Citation Table

Each row covers: the cite key used in the .tex, where it appears, whether the key resolves to a bib entry, and accuracy notes.

| # | Cite Key | Where Used in Paper | Key in .bib? | Accuracy Assessment |
|---|----------|---------------------|:---:|---------------------|
| 1 | `megatron` | §1 – Megatron-LM framework | YES | **Accurate.** Authors (Shoeybi, Patwary, Puri, LeGresley, Casper, Catanzaro), title, arXiv 1909.08053, 2019 – all correct. Venue is arXiv preprint (no conference published version of original), which is fine as `@article`. |
| 2 | `deepspeed` | §1 – DeepSpeed framework | YES | **Partially accurate.** Authors (Rasley, Rajbhandari, Ruwase, He) and title are correct. Venue listed as "KDD 2020 (ACM SIGKDD)" is the correct venue. Bib entry says `booktitle = {Proceedings of the 26th ACM SIGKDD...}` – correct. Year 2020 correct. **OK.** |
| 3 | `pytorch-fsdp` | §1 – PyTorch FSDP framework | YES | **Venue is wrong.** Bib entry says `booktitle = {Proceedings of the VLDB Endowment}` and `year = {2023}`. PyTorch FSDP was indeed published in PVLDB vol. 16, 2023, pages 3848–3860. However, the entry uses `@inproceedings` with `booktitle` but PVLDB is a journal – should be `@article` with `journal = {Proc. VLDB Endow.}`, `volume = {16}`, `number = {12}`. Minor formatting issue, not a factual error. The author list in the bib entry is accurate (all 18 authors). |
| 4 | `nccl` | §1, §2 – NCCL library | YES | **Accurate.** NVIDIA NCCL GitHub repo, `@misc` with URL is appropriate. Year 2024 is reasonable. |
| 5 | `autoccl` | §1, §2, §6 – AutoCCL custom tuner plugins, cloud providers ship custom tuner plugins | YES | **UNVERIFIED / POTENTIALLY WRONG.** Bib entry lists `booktitle = {SC 2024}`, year 2024, author `Cai, Zhihao and others`. Could not locate an "AutoCCL" paper at SC 2024 or any major venue with these details. The paper is used to support the claim that "major cloud providers already ship custom tuner plugins" – this claim may be accurate (e.g., AWS/Azure use custom tuners) but if AutoCCL is the wrong citation for this claim, it is misleading. **Needs verification of the actual AutoCCL publication venue and DOI.** |
| 6 | `msccl` | §1, §6 – MSCCL synthesizes static schedules | YES | **Venue is wrong.** Bib entry says `booktitle = {NSDI 2023}`, but no MSCCL paper (by Cowan, Maleki, Musuvathi, Saarikivi, Xiong) was found at NSDI 2023. The related work at NSDI 2023 is TACCL (by Shah et al., which is separately cited). MSCCL originated as a Microsoft Research tool with publications at PPoPP'21 ("Synthesizing Optimal Collective Algorithms") and a compiler paper (GC3) at ASPLOS'23. The specific paper by Cowan, Maleki, Musuvathi, Saarikivi, Xiong titled "MSCCL: Programmable Communication Schedules for GPU Clusters" at NSDI'23 **could not be confirmed.** This is a significant accuracy concern. |
| 7 | `demystifying-nccl` | §2, §6 – NCCL protocols analysis | YES | **Accurate.** Authors (Hu, Shen, Bonato, Jeaugey, Alexander, Spada, Dinan, Hammond, Hoefler), title, arXiv 2507.04786, 2025 – verified correct. Note: the bib entry omits several co-authors (Alexander, Spada, Dinan, Hammond, Hoefler) but uses "others" which is acceptable for arXiv preprints. |
| 8 | `nccl-inspector-bug` | §2 – Safety gap: UAF/deadlock crash | YES | **Accurate.** Issue #2000 in NVIDIA/nccl GitHub, titled "Deadlock/crash due to UAF in inspector plugin", opened January 26, 2026, reporting deadlock and use-after-free in the inspector plugin. The bib title matches the issue title. Year 2026 is correct. **The bib entry title is "Deadlock/crash due to UAF in inspector plugin"** – the actual GitHub issue title is "[Issue]: Deadlock/crash due to UAF in inspector plugin" – close enough. **OK.** |
| 9 | `nccl-profiler-bug` | §2 – Profiler plugin segfaults | YES | **Partially accurate.** Issue #1992 in NVIDIA/nccl GitHub exists, opened January 22, 2026. However, the bib entry title is "Inspector bug: segfault encountered during training" while the actual issue title is "[Question]: Inspector Bug: Do RecordEvents recall after StopEvent? This results in a 'segfault encountered' error". Also, the bug is about the inspector/profiler, **not a "profiler plugin" on multi-GPU nodes** in the general sense – it's a specific inspector feature. The claim in the paper text "profiler plugins have triggered segfaults on multi-GPU nodes" is somewhat supported (H200 system), though the issue is more specifically about the inspector-as-profiler feature, not a general profiler plugin. Minor framing discrepancy. |
| 10 | `llama3` | §2 – 16,384 GPUs, 466 interruptions, 54 days | YES | **Claim needs verification.** The bib entry is arXiv:2407.21783 (Dubey et al., 2024). The Llama 3 paper is real and does document training failures at scale (the paper is 92 pages with extensive training infrastructure discussion). The specific numbers "466 job interruptions in 54 days on 16,384 GPUs" are plausible and widely cited from this paper's infrastructure section. The full author list starts with Abhimanyu Dubey but the paper has 559 authors from Meta; the bib entry uses "Dubey, Abhimanyu and others" which is acceptable. **Likely accurate but the 16,384 GPU / 466 / 54 day numbers should be confirmed from the full PDF.** |
| 11 | `philly` | §2 – ~30% of training jobs fail, programming/config errors dominant | YES | **Accurate.** Jeon, Venkataraman, Phanishayee, Qian, Xiao, Yang. "Analysis of Large-Scale Multi-Tenant GPU Clusters for DNN Training Workloads." USENIX ATC 2019. This is the Philly cluster study from Microsoft Research. The paper does analyze job failures; the ~30% failure rate claim is plausible from this study. Authors, venue, year are all correct. **OK.** |
| 12 | `ebpf-linux` | §2 – eBPF execution framework background | YES | **Inaccurate entry.** Bib entry cites "Fast Packet Processing with eBPF and XDP" by Vieira, Nelson Billing et al., ACM Computing Surveys 2020. This is a survey paper, not the primary reference for "eBPF as an execution framework originally implemented in the Linux kernel." The proper citation for eBPF-as-kernel-mechanism should be the original Linux kernel networking paper or a more authoritative source (e.g., the eBPF syscall, Starovoitov's work, or a systems paper like "The eXpress Data Path"). The cited survey is a secondary source and does not define eBPF as an execution framework. Also, the bib entry author "Vieira, Nelson Billing" appears to be wrong – the lead author of the ACM CSUR 2020 survey on eBPF/XDP is Toke Høiland-Jørgensen et al. or Vieira et al. (a Brazilian group); this needs verification. |
| 13 | `xrp` | §2 – eBPF for storage | YES | **Accurate.** XRP paper by Zhong, Li, Wu, Zarkadas, Tao, Mesterhazy, Makris, Yang, Tai, Stutsman, Cidon. "XRP: In-Kernel Storage Functions with eBPF." OSDI 2022. Verified: Best Paper Award at OSDI'22, correct authors and venue. Bib entry only lists "Zhong, Yuhong and Zhu, Haoyu and others" – "Zhu, Haoyu" is **wrong**; the second author is Haoyu Li, not Haoyu Zhu. This is a factual error in the bib entry author field. |
| 14 | `electrode` | §2 – eBPF for networking | YES | **Accurate.** Electrode: Accelerating Distributed Protocols with eBPF. Authors: Yang Zhou, Zezhou Wang, Sowmya Dharanipragada, Minlan Yu. NSDI 2023. Verified correct venue and year. Bib entry says `author = {Zhou, Yu and others}` – the first author is **Yang Zhou**, not "Yu Zhou". This is a minor author name issue (reversed given/family order) but the citation still identifies the right paper. |
| 15 | `etran` | §2 – eBPF for networking | YES | **Venue unverified.** Bib entry lists "eTran: Efficient and Extensible Transparent Network Functions" at OSDI 2024. Could not locate this paper in the OSDI 2024 program using web searches. The OSDI 2024 eBPF-related paper found was "Validating the eBPF Verifier via State Embedding." An "eTran" paper at OSDI 2024 may exist but could not be confirmed. **Needs verification.** |
| 16 | `pageflex` | §2 – eBPF for memory management | YES | **Venue unverified.** Bib entry lists "PageFlex: eBPF-Based Programmable Page Management" at ASPLOS 2025. Could not locate this paper in ASPLOS 2025 proceedings via web search. **Needs verification.** |
| 17 | `gpu-ext` | §2, §6 – eBPF for GPU resource management | YES | **Accurate.** `gpu_ext: Extensible OS Policies for GPUs via eBPF` by Zheng, Yu, Yang et al. arXiv:2512.12615, 2025. Verified correct. Note the first two listed authors in the bib (Zheng, Yusheng and Yu, Tong) match the verified paper. |
| 18 | `bpftime` | §1, §3, §4 – bpftime userspace eBPF runtime | YES | **Inaccurate title/venue.** Bib entry says title = "bpftime: Userspace eBPF Runtime for Uprobe, Syscall and Kernel-Function Tracing" at OSDI 2025. The verified OSDI 2025 paper is titled **"Extending Applications Safely and Efficiently"** (not "bpftime: Userspace eBPF Runtime for Uprobe..."). Authors are Yusheng Zheng, Tong Yu, Yiwei Yang, Yanpeng Hu, Xiaozheng Lai, Dan Williams, Andi Quinn. The bib entry also lists only "Tong Yu and Zheng, Yusheng and others" which reverses first/second author (Zheng is first). **The title in the bib is wrong.** The correct OSDI 2025 title is "Extending Applications Safely and Efficiently." |
| 19 | `prevail` | §4 – PREVAIL verifier | YES | **Partially accurate.** Bib entry: Gershuni, Amit et al., "Simple and Precise Static Analysis of Untrusted Linux Kernel Extensions," PLDI 2019. This is the correct paper and venue (PLDI 2019) for PREVAIL. Authors Elazar Gershuni and Nadav Amit are confirmed. **OK in substance**, though the bib only lists two named authors plus "others." |
| 20 | `mscclpp` | §6 – MSCCL++ GPU-driven primitives | YES | **Venue and title may be wrong.** Bib entry: "MSCCL++: A GPU-driven Communication Stack for Scalable AI Training," EuroSys 2025. The verified arXiv paper (2504.09014) has a different title: **"MSCCL++: Rethinking GPU Communication Abstractions for AI Inference"** and appears to be at **ASPLOS 2026** (not EuroSys 2025) based on a DOI hint (10.1145/3779212.3790188). Also the listed first author "Salehi, Mehrdad" in the bib could not be confirmed – the verified authors start with Changho Hwang. **Both title and venue appear incorrect.** |
| 21 | `taccl` | §6 – TACCL collective synthesis | YES | **Accurate.** "TACCL: Guiding Collective Algorithm Synthesis using Communication Sketches" at NSDI 2023. Authors include Shah, Chidambaram, Cowan, Maleki, Musuvathi, Mytkowicz, Nelson, Saarikivi, Singh. Bib entry lists "Shah, Aashaka and others" at NSDI 2023 – venue confirmed correct. **OK.** |
| 22 | `rccl` | §7 – AMD RCCL cross-vendor portability | YES | **Accurate.** AMD RCCL GitHub repo, `@misc` with URL. Year 2024 reasonable. **OK.** |
| 23 | `pjrt` | NOT USED in .tex | YES | The `pjrt` bib entry (which is actually the JAX paper, not PJRT) is present in references.bib but **never cited** in the paper. This is a stale/orphan entry. Should be removed or the citation key is misleading (it's labeled `pjrt` but the entry is JAX). |
| 24 | `eim` | NOT USED in .tex | YES | The `eim` bib entry is present but **never cited** in the paper. The paper mentions EIM in the CLAUDE.md project overview but does not cite it in the paper text. |

---

## 2. Uncited Claims That Need Citations

The following claims in the paper lack citations but are specific enough to require them:

### §2 – Alternative Extension Mechanisms paragraph (no citations at all)

The entire "Alternative extension mechanisms" paragraph in §6 makes comparative claims without a single citation:

1. **"WebAssembly provides sandboxed execution but lacks eBPF's static termination and memory-safety guarantees; its overhead is typically higher due to bounds checks that eBPF eliminates statically."**
   - No citation for WebAssembly as an extension mechanism. This needs a citation. Suitable refs: the WebAssembly spec paper (Haas et al., PLDI 2017), or a systems paper using Wasm as an extension mechanism (e.g., WasmEdge, Lucet, or Wasm for kernel extensions).

2. **"Out-of-process services provide strong isolation but add microseconds of IPC latency."**
   - No citation. Should cite a paper on microkernel/exokernel IPC costs, or a systems measurement paper. Even a well-known result (e.g., L4 IPC benchmarks) would suffice.

3. **"Declarative DSLs cannot express stateful policies or feedback loops."**
   - No citation. Should cite an example DSL (e.g., P4 for network policy, or a configuration DSL) to make this concrete.

4. **"Hardware isolation (Intel MPK) provides memory separation without verification."**
   - No citation for Intel MPK. Should cite either Intel's Memory Protection Keys documentation or a systems paper using MPK (e.g., ERIM, Hodor, or CHERI).

### §1 – NCCL as "de facto standard"

The claim "NCCL is the de facto standard library for GPU collective communication in large-scale distributed training" is made in the abstract and §1 without a citation. While widely known, a citation to NCCL's own paper/documentation is already present (`\cite{nccl}`). The adjacent sentence citing Megatron-LM, DeepSpeed, and PyTorch FSDP effectively provides context. **Acceptable as-is**, but a survey or benchmark paper confirming NCCL's dominance would strengthen the claim.

### §2 – Llama 3 job interruption specifics

The specific numbers "466 job interruptions in 54 days" and "16,384 GPUs" are attributed to `\cite{llama3}`. This is likely correct (from the Llama 3 paper's infrastructure section), but the numbers should be confirmed against the full PDF. If wrong, the claim would be seriously misleading.

### §2 – Philly cluster "~30% of training jobs fail"

The ~30% figure is attributed to `\cite{philly}`. The Philly paper studies GPU cluster workloads at Microsoft and does discuss job failures, but the exact 30% figure should be verified against the paper's results section.

---

## 3. Errors in Existing Bib Entries

| Cite Key | Error Type | Details |
|----------|-----------|---------|
| `bpftime` | **Wrong title** | Bib title is "bpftime: Userspace eBPF Runtime for Uprobe, Syscall and Kernel-Function Tracing". OSDI 2025 actual title is **"Extending Applications Safely and Efficiently"**. |
| `bpftime` | **Wrong author order** | Bib lists "Tong Yu and Zheng, Yusheng" – correct order is Yusheng Zheng (first), Tong Yu (second). |
| `mscclpp` | **Wrong title** | Bib: "MSCCL++: A GPU-driven Communication Stack for Scalable AI Training". Actual: **"MSCCL++: Rethinking GPU Communication Abstractions for AI Inference"**. |
| `mscclpp` | **Wrong venue** | Bib: EuroSys 2025. Likely actual venue: **ASPLOS 2026** (DOI hint 10.1145/3779212.3790188). |
| `mscclpp` | **Wrong first author** | Bib: "Salehi, Mehrdad". Actual first author: **Changho Hwang**. |
| `msccl` | **Venue unconfirmed** | Bib: NSDI 2023 with authors Cowan, Maleki, Musuvathi, Saarikivi, Xiong. No MSCCL paper by these authors at NSDI 2023 could be confirmed. TACCL (Shah et al.) is at NSDI 2023. The MSCCL project's main algorithmic paper appeared at **PPoPP 2021** ("Synthesizing Optimal Collective Algorithms"). |
| `xrp` | **Wrong author** | Second author listed as "Zhu, Haoyu" but actual second author is **Haoyu Li**. |
| `electrode` | **Author name** | Listed as "Zhou, Yu" but correct name is **Yang Zhou**. |
| `ebpf-linux` | **Inappropriate reference** | The ACM CSUR survey "Fast Packet Processing with eBPF and XDP" is a secondary/survey source; for the claim that "eBPF is an execution framework originally implemented in the Linux kernel," a more authoritative primary reference is needed. Also, author "Vieira, Nelson Billing" needs verification. |
| `autoccl` | **Unverified** | Cannot confirm SC 2024 paper by Cai et al. titled "AutoCCL." The claim it supports (cloud providers ship custom tuner plugins) may need a different or additional citation. |
| `etran` | **Unverified venue** | Cannot confirm OSDI 2024 publication. |
| `pageflex` | **Unverified venue** | Cannot confirm ASPLOS 2025 publication. |
| `pjrt` | **Key mismatch** | Entry key is `pjrt` but the entry is for JAX (Bradbury et al. 2018). Also this entry is unused in the paper. |

---

## 4. Suggested New Citations

### 4a. WebAssembly (for §6 "Alternative extension mechanisms")

```bibtex
@inproceedings{wasm,
  author    = {Haas, Andreas and Rossberg, Andreas and Schuff, Derek L. and
               Titzer, Ben L. and Holman, Michael and Gohman, Dan and
               Wagner, Luke and Zakai, Alon and Bastien, JF},
  title     = {Bringing the Web up to Speed with {WebAssembly}},
  booktitle = {Proceedings of the 38th ACM SIGPLAN Conference on Programming
               Language Design and Implementation (PLDI)},
  year      = {2017},
  pages     = {185--200},
}
```

Or for Wasm as an OS-extension mechanism specifically:

```bibtex
@inproceedings{sledge,
  author    = {Mendki, Abhijit and others},
  title     = {Evaluating {WebAssembly} Enabled Microservices},
  booktitle = {...},
  year      = {2020},
}
```

A more fitting citation would be a paper that uses Wasm for plugin/extension sandboxing, such as:

```bibtex
@inproceedings{wasm-plugin,
  author    = {Narayan, Shravan and Disselkoen, Craig and Garfinkel, Tal and
               Froyd, Nathan and Rahm, Eric and Lerner, Sorin and Shacham, Hovav
               and Stefan, Deian},
  title     = {Retrofitting Fine Grain Isolation in the {Firefox} Renderer},
  booktitle = {Proceedings of the 29th USENIX Security Symposium},
  year      = {2020},
}
```

### 4b. Intel MPK (for §6)

```bibtex
@inproceedings{erim,
  author    = {Vahldiek-Oberwagner, Anjo and Elnikety, Eslam and Duarte,
               Nuno O. and Druschel, Peter and Garg, Deepak},
  title     = {{ERIM}: Secure, Efficient In-Process Isolation with Protection
               Keys ({MPK})},
  booktitle = {Proceedings of the 28th USENIX Security Symposium},
  year      = {2019},
  pages     = {1221--1238},
}
```

### 4c. eBPF primary reference (replacement or supplement for `ebpf-linux`)

```bibtex
@inproceedings{ebpf-osdi,
  author    = {Gregg, Brendan},
  title     = {{BPF} Performance Tools},
  ...
}
```

A better primary citation for "eBPF originally in the Linux kernel for packet filtering and tracing" would be:

```bibtex
@inproceedings{bpf-original,
  author    = {McCanne, Steven and Jacobson, Van},
  title     = {The {BSD} Packet Filter: A New Architecture for User-Level
               Packet Capture},
  booktitle = {Proceedings of the USENIX Winter Technical Conference},
  year      = {1993},
  pages     = {259--269},
}
```

Or a modern reference paper specifically for eBPF (not XDP survey):

```bibtex
@misc{ebpf-foundation,
  author       = {{eBPF Foundation}},
  title        = {{eBPF} --- Introduction, Tutorials \& Community Resources},
  howpublished = {\url{https://ebpf.io}},
  year         = {2023},
}
```

### 4d. MSCCL correct citation

Based on available evidence, the correct PPoPP 2021 paper for the MSCCL algorithmic work is:

```bibtex
@inproceedings{msccl-ppopp,
  author    = {Cai, Zixian and Liu, Zhengyang and Maleki, Saeed and
               Musuvathi, Madanlal and Mytkowicz, Todd and Nelson, Jacob and
               Saarikivi, Olli},
  title     = {Synthesizing Optimal Collective Algorithms},
  booktitle = {Proceedings of the 26th ACM SIGPLAN Symposium on Principles
               and Practice of Parallel Programming (PPoPP)},
  year      = {2021},
  pages     = {62--75},
  doi       = {10.1145/3437801.3441620},
}
```

### 4e. Corrected bpftime citation

```bibtex
@inproceedings{bpftime,
  author    = {Zheng, Yusheng and Yu, Tong and Yang, Yiwei and Hu, Yanpeng and
               Lai, Xiaozheng and Williams, Dan and Quinn, Andi},
  title     = {Extending Applications Safely and Efficiently},
  booktitle = {Proceedings of the 19th USENIX Symposium on Operating Systems
               Design and Implementation (OSDI)},
  year      = {2025},
}
```

### 4f. Corrected MSCCL++ citation

```bibtex
@inproceedings{mscclpp,
  author    = {Hwang, Changho and Cheng, Peng and Dathathri, Roshan and
               Jangda, Abhinav and Maleki, Saeed and Musuvathi, Madan and
               Saarikivi, Olli and Shah, Aashaka and Yang, Ziyue and
               Li, Binyang and Rocha, Caio and Zhou, Qinghua and
               Ghazimirsaeed, Mahdieh and Anantharamu, Sreevatsa and Jose, Jithin},
  title     = {{MSCCL++}: Rethinking {GPU} Communication Abstractions for {AI} Inference},
  booktitle = {Proceedings of the 30th ACM International Conference on
               Architectural Support for Programming Languages and Operating
               Systems (ASPLOS)},
  year      = {2026},
  doi       = {10.1145/3779212.3790188},
}
```

---

## 5. Summary of Priority Fixes

**Critical (factually wrong, visible to readers):**
1. `bpftime` – Wrong paper title. The OSDI 2025 paper is "Extending Applications Safely and Efficiently," not the title currently in the bib.
2. `mscclpp` – Wrong title, wrong venue (EuroSys 2025 vs ASPLOS 2026), wrong first author.
3. `msccl` – Cannot confirm NSDI 2023 publication by Cowan et al. with that title. May need to switch to PPoPP 2021 "Synthesizing Optimal Collective Algorithms" or GC3 at ASPLOS'23.

**Important (misleading or unverified):**
4. `autoccl` – Unverified SC 2024 paper. The claim about "major cloud providers ship custom tuner plugins" may need a different source.
5. `etran` – Unverified OSDI 2024 placement.
6. `pageflex` – Unverified ASPLOS 2025 placement.
7. `ebpf-linux` – Survey paper is a poor fit for the claim about eBPF's origins; author name may be wrong.

**Minor (clean up):**
8. `xrp` – Second author "Zhu, Haoyu" should be "Li, Haoyu."
9. `electrode` – First author "Zhou, Yu" should be "Zhou, Yang."
10. `pjrt` / `eim` – Unused bib entries; should be removed or the key name for `pjrt` is misleading (it's JAX).
11. Add citations for: WebAssembly, Intel MPK, Out-of-process IPC latency, Declarative DSLs – in the "Alternative extension mechanisms" paragraph.

---

## 6. Uncited Claims Summary Table

| Claim | Location | Suggested Citation |
|-------|----------|-------------------|
| WebAssembly sandboxed execution | §6 | Haas et al. PLDI 2017 (WebAssembly) |
| Wasm overhead higher due to bounds checks | §6 | Needs measurement paper or Wasm spec |
| Out-of-process IPC = microseconds latency | §6 | IPC benchmark paper (e.g., L4, seL4, or microkernel study) |
| Declarative DSLs can't express stateful policies | §6 | P4 spec or an example DSL paper |
| Intel MPK memory separation without verification | §6 | ERIM (Vahldiek-Oberwagner et al., USENIX Security 2019) |
| NCCL "de facto standard" (abstract + §1) | Abstract, §1 | Already has `\cite{nccl}`; acceptable |
| 466 interruptions / 54 days / 16,384 GPUs | §2 | `\cite{llama3}` – verify exact numbers in PDF |
| ~30% jobs fail, config errors dominant | §2 | `\cite{philly}` – verify ~30% figure in paper |
