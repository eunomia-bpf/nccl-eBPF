# GPU + eBPF Related Work Survey

*Compiled for the related work section of the NCCL-eBPF workshop paper.*
*Search date: 2026-03-10*

---

## Category 1: eBPF Applied Directly to GPU Workloads

### 1.1 gpu_ext: Extensible OS Policies for GPUs via eBPF
- **Authors:** Yusheng Zheng, Tong Yu, Yiwei Yang, Minghui Jiang, Xiangyu Gao, Jianchang Su, Yanpeng Hu, Wenan Mao, Wei Zhang, Dan Williams, Andi Quinn
- **Venue/Year:** arXiv:2512.12615, December 2025
- **Summary:** Proposes an eBPF-based runtime that extends GPU drivers by exposing safe programmable hooks, including a *device-side* eBPF runtime capable of executing verified policy logic within GPU kernels. Targets GPU memory placement, scheduling, and observability. Achieves up to 4.8× throughput improvement and 2× tail latency reduction across inference, training, and vector search workloads—without modifying or restarting applications.
- **Relation to our work:** The closest prior work. gpu_ext focuses on OS-level GPU resource management (driver/device layer), while our work targets the *communication library* layer (NCCL plugins). Both use eBPF for verifiable, isolated policy execution on GPU-adjacent paths. gpu_ext targets GPU kernel execution policy; we target collective communication algorithm and protocol selection policy.
- **Citation worthiness:** STRONG — most directly related. Must cite.

---

## Category 2: eBPF for HPC / Distributed ML Monitoring and Telemetry

### 2.1 Host-Side Telemetry for Performance Diagnosis in Cloud and HPC GPU Infrastructure
- **Authors:** Erfan Darzi, Aldo Pareja, Shreeanant Bharadwaj
- **Venue/Year:** arXiv:2510.16946, October 2025
- **Summary:** An eBPF-based monitoring system providing unified host-side monitoring of GPU workloads. Correlates eBPF-derived host metrics with GPU-internal events. Achieves 81–88% diagnostic accuracy with only 1.21% CPU overhead at 100 Hz sampling. Identifies root causes including NIC contention, PCIe pressure, and CPU interference in multi-tenant GPU infrastructure.
- **Relation to our work:** Demonstrates eBPF as a telemetry substrate for GPU infrastructure — complementary to our profiler-based feedback loop. Their work is pure monitoring; we use similar telemetry data to *drive policy decisions* that change NCCL behavior.
- **Citation worthiness:** GOOD — supports our argument that eBPF is a viable substrate for GPU infrastructure observability.

### 2.2 eACGM: Non-instrumented Performance Tracing and Anomaly Detection towards Machine Learning Systems
- **Authors:** Ruilin Xu, Zongxuan Xie, Pengfei Chen
- **Venue/Year:** IWQoS 2025 (arXiv:2506.02007)
- **Summary:** A full-stack AI/ML monitoring framework using eBPF to collect real-time performance data from GPUs, network communication layers, CUDA, Python, and PyTorch without any code instrumentation. Applies Gaussian Mixture Model statistical analysis to identify performance anomalies in distributed training.
- **Relation to our work:** Uses eBPF to observe the same GPU+network stack we target. They observe and report; we observe and act. Their GPU+network telemetry collection complements the "policy loop" motivation in our work.
- **Citation worthiness:** GOOD — IWQoS 2025 is a reputable venue; useful for the telemetry/observability angle.

### 2.3 Adaptyst: Enabling Heterogeneous Performance Analysis for Scientific Workloads
- **Authors:** Maksymilian Graczyk, Vincent Desbiolles, Stefan Roiser, Andrea Guerrieri (CERN)
- **Venue/Year:** IEEE HPEC 2025, Outstanding Short Paper Award (arXiv:2511.13928)
- **Summary:** Explores eBPF-based methods (uprobes, USDT) for performance analysis across CPUs, GPUs, and FPGAs in scientific/HPC workloads. Part of CERN's open-source Adaptyst initiative for architecture-agnostic performance analysis.
- **Relation to our work:** Demonstrates eBPF applicability in HPC heterogeneous computing. Our work similarly applies eBPF hooks inside a systems library (NCCL) rather than at the kernel level.
- **Citation worthiness:** MODERATE — supports eBPF in HPC context; the HPEC venue is directly relevant to our workshop setting.

---

## Category 3: Userspace eBPF Runtimes and Application Extension Models

### 3.1 bpftime: Userspace eBPF Runtime for Uprobe, Syscall and Kernel-User Interactions
- **Authors:** Yusheng Zheng, Tong Yu, Yiwei Yang, Yanpeng Hu, Xiaozheng Lai, Andrew Quinn
- **Venue/Year:** OSDI 2025 (arXiv:2311.07923)
- **Summary:** Introduces bpftime, a user-space eBPF runtime using binary rewriting for uprobe and syscall hooking. Achieves 10× speedup over kernel uprobes by eliminating context-switch overhead. Supports interprocess eBPF maps, seamless attachment to running processes, and compatibility with existing eBPF toolchains (clang, libbpf, CO-RE).
- **Relation to our work:** bpftime is the *execution substrate* for our eBPF policies inside NCCL. Our work is an application/case study of bpftime in the GPU collective communication domain. The EIM capability model (also from this group) underpins our policy isolation design.
- **Citation worthiness:** STRONG — must cite as infrastructure we build upon.

### 3.2 Extending Applications Safely and Efficiently (EIM/bpftime)
- **Authors:** Yusheng Zheng, Tong Yu, Yiwei Yang, Yanpeng Hu, Xiaozheng Lai, Dan Williams, Andi Quinn
- **Venue/Year:** OSDI 2025
- **Summary:** Introduces the Extension Interface Model (EIM), a resource-based specification approach where each extension feature is modeled as a resource (memory, function invocation capability, etc.). Implements EIM with eBPF-style verification, Intel MPK hardware isolation, and dynamic binary rewriting. Demonstrates six use cases in security, performance monitoring, and configuration analysis.
- **Relation to our work:** EIM is the formal capability model underlying our policy isolation semantics. Our work instantiates EIM hooks at NCCL decision points (tuner, net, profiler plugins). This paper should be cited as the formal foundation for our extension model.
- **Citation worthiness:** STRONG — must cite as direct architectural foundation.

---

## Category 4: eBPF for Network Extensibility and Transport Customization

### 4.1 eTran: Extensible Kernel Transport with eBPF
- **Authors:** Zhongjie Chen, Qingkai Meng, ChonLam Lao, et al.
- **Venue/Year:** NSDI 2025
- **Summary:** Uses eBPF to safely implement custom kernel transport protocols, incorporating user-space transport performance techniques into the kernel. Demonstrated TCP+DCTCP and Homa with 4.8×/1.8× higher throughput and 3.7×/7.5× lower latency compared to existing kernel implementations.
- **Relation to our work:** Demonstrates the "eBPF for programmable network stack" paradigm. Our work extends this idea from network transport to the collective communication library layer. Both show eBPF enabling safe runtime customization without kernel modification.
- **Citation worthiness:** GOOD — strong systems venue; shows eBPF network extensibility as a validated approach.

### 4.2 VEP: A Two-stage Verification Toolchain for Full eBPF Programmability
- **Authors:** Xiwei Wu, Yueyang Feng, Tianyi Huang, et al. (Shanghai Jiao Tong University)
- **Venue/Year:** NSDI 2025
- **Summary:** Addresses constraints in existing eBPF verifiers with a three-component annotation-guided toolchain (VEP-C, VEP-compiler, VEP-eBPF). Reduces code modification burden while enabling broader eBPF applications in networking, tracing, and security.
- **Relation to our work:** Supports the argument that eBPF verification is an active research area and that verifier constraints (which we address via bpftime's approach) are a recognized problem. Our use of policy verification for NCCL fits this broader context.
- **Citation worthiness:** MODERATE — relevant to the verification/safety story.

### 4.3 The eBPF Runtime in the Linux Kernel
- **Authors:** Bolaji Gbadamosi, Luigi Leonardi, Tobias Pulls, Toke Høiland-Jørgensen, Simone Ferlin-Reiter, Simo Sorce, Anna Brunström
- **Venue/Year:** arXiv:2410.00026, October 2024
- **Summary:** First comprehensive description of eBPF runtime design in the Linux kernel. Argues eBPF provides a mature, safe programming environment used to extend and program entire kernel components while preserving runtime integrity.
- **Relation to our work:** Provides authoritative background on eBPF's safety model, which our work extends to the userspace/library domain. Good general citation for eBPF background.
- **Citation worthiness:** MODERATE — good for background/introduction.

### 4.4 Rex: Safe and Usable Kernel Extensions in Rust
- **Authors:** Jinghao Jia, Ruowen Qin, Milo Craun, Egor Lukiyanov, Ayush Bansal, Michael V. Le, Hubertus Franke, Hani Jamjoom, Tianyin Xu, Dan Williams
- **Venue/Year:** arXiv:2502.18832, February 2025
- **Summary:** Addresses the "language-verifier gap" in eBPF by building kernel extensions in Rust with language-based safety instead of separate static verification. Uses a lightweight extralingual runtime for exception handling, stack safety, and termination guarantees.
- **Relation to our work:** Identifies the same fundamental problem we face: existing kernel eBPF verifiers have usability and expressivity limitations. Our solution (bpftime + EIM at userspace) takes a different approach than Rex (kernel+Rust), but both target the same gap.
- **Citation worthiness:** MODERATE — relevant to the "safe extension" motivation.

---

## Category 5: Collective Communication Optimization and NCCL Analysis

### 5.1 Demystifying NCCL: An In-depth Analysis of GPU Communication Protocols and Algorithms
- **Authors:** Zhiyi Hu, Siyuan Shen, Tommaso Bonato, Sylvain Jeaugey, Cedell Alexander, Eric Spada, James Dinan, Jeff Hammond, Torsten Hoefler
- **Venue/Year:** arXiv:2507.04786, July 2025
- **Summary:** Comprehensive analysis of NCCL's internal design: protocol variants (Simple, LL, LL128), intra/inter-node data movement, ring and tree collective algorithms. Introduces ATLAHS, an application-trace-driven simulation toolchain for reproducing NCCL communication patterns at scale.
- **Relation to our work:** Provides deep understanding of the NCCL internals our policy plane targets. Their analysis of protocol selection (Simple vs LL vs LL128) directly informs our size-aware policy experiments showing 39.5× slowdown from static protocol selection. The protocol/algorithm distinction is central to our paper.
- **Citation worthiness:** STRONG — essential NCCL background citation.

### 5.2 AutoCCL: Automated Collective Communication Tuning
- **Authors:** Guanbin Xu, Zhihao Le, Yinhe Chen, et al.
- **Venue/Year:** NSDI 2025
- **Summary:** Automated tuning of collective communication configurations using a divide-and-conquer strategy to handle configuration search space explosion. Achieves 1.24–1.29× speedups over NCCL and hides tuning overhead within initial training iterations. Accounts for communication-computation interference.
- **Relation to our work:** AutoCCL is a "configuration tuner" approach: it searches for a *static* optimal configuration per workload. Our work is a *dynamic policy plane*: we change algorithm/protocol decisions per-collective based on runtime telemetry, enabling SLO enforcement and anomaly prevention (e.g., protocol collapse) that static tuning cannot address. This is a key differentiator to articulate.
- **Citation worthiness:** STRONG — important comparison point to distinguish static tuning from dynamic governance.

### 5.3 OptiReduce: Resilient and Tail-Optimal AllReduce
- **Authors:** Ertza Warraich, Omer Shabtai, Khalid Manaa, et al.
- **Venue/Year:** NSDI 2025
- **Summary:** Collective communication system for cloud DDL with predictable completion times. Uses unreliable bounded transport with adaptive timeout, exploiting gradient loss tolerance. Achieves 70% faster time-to-accuracy vs Gloo and 30% vs NCCL in shared cloud environments.
- **Relation to our work:** Addresses fault resilience and tail latency in collective communication — problem space adjacent to our SLO enforcement and fault resilience use cases. Their approach modifies the transport; ours enforces policy from above via the tuner/net plugin API.
- **Citation worthiness:** MODERATE — relevant to fault resilience and SLO motivation.

---

## Category 6: GPU Distributed Training Communication Bottlenecks

### 6.1 Lagom: Unleashing the Power of Communication and Computation Overlapping for Distributed LLM Training
- **Authors:** Guanbin Xu, ZhenGuo Xu, Yuzhe Li, Youhui Bai, Ping Gong, Chaoyi Ruan, Cheng Li
- **Venue/Year:** arXiv:2602.20656, 2025
- **Summary:** Introduces a unified cost model and priority-based search to co-tune communication parameters for overlapping computation and communication in distributed LLM training. Achieves 1.07–1.33× speedup across diverse GPU clusters.
- **Relation to our work:** Demonstrates importance of communication parameter tuning (which our policy plane automates). Their cost model parallels NCCL's collCostTable that our policies modify.
- **Citation worthiness:** MODERATE — supporting context for communication tuning.

### 6.2 OptiNIC: A Resilient and Tail-Optimal RDMA NIC for Distributed ML Workloads
- **Authors:** Ertza Warraich, Ali Imran, Annus Mustafa, Shay Vargaftik, Sonia Fahmy, Muhammad Shahbaz
- **Venue/Year:** arXiv:2512.22743, December 2025
- **Summary:** Redesigns RDMA transport for ML workloads by eliminating retransmissions and in-order delivery, enabling best-effort, out-of-order transport. Shows 2× time-to-accuracy improvement and 3.5× 99th-percentile latency reduction.
- **Relation to our work:** Works at the NIC/transport layer; our work at the collective library layer. Together they suggest a multi-layer approach where policies act at different levels of the GPU communication stack.
- **Citation worthiness:** WEAK-to-MODERATE — peripheral; useful only if discussing multi-layer policy stacks.

---

## Key Gaps / What Our Work Adds

The survey reveals that:

1. **eBPF + GPU** exists only at the OS/driver layer (gpu_ext) or as telemetry (eACGM, Darzi et al.). No prior work applies eBPF at the *collective communication library* layer.

2. **Collective communication tuning** (AutoCCL, Lagom) focuses on static configuration search. No prior work uses a *dynamic runtime policy plane* with per-invocation decision making, telemetry feedback, and SLO enforcement for NCCL.

3. **Userspace eBPF** (bpftime/EIM at OSDI 2025) is the enabling infrastructure; our work is the first to apply it inside a GPU communication runtime.

4. **Protocol collapse prevention** (our 39.5× finding) is not addressed by any existing tuner or monitoring work.

---

## Recommended Citation Priority for Paper

| Priority | Paper | Reason |
|---|---|---|
| Must cite | gpu_ext (arXiv:2512.12615) | Closest related; eBPF+GPU policies |
| Must cite | bpftime OSDI 2025 (arXiv:2311.07923) | Infrastructure we build on |
| Must cite | EIM/bpftime OSDI 2025 (extension model) | Formal foundation for our design |
| Must cite | Demystifying NCCL (arXiv:2507.04786) | Core NCCL background |
| Must cite | AutoCCL NSDI 2025 | Key differentiator (static vs dynamic) |
| Strong | eACGM IWQoS 2025 (arXiv:2506.02007) | eBPF for GPU+network telemetry |
| Strong | eTran NSDI 2025 | eBPF network extensibility paradigm |
| Good | Darzi et al. (arXiv:2510.16946) | eBPF GPU infrastructure monitoring |
| Good | Adaptyst HPEC 2025 (arXiv:2511.13928) | eBPF in HPC context |
| Good | OptiReduce NSDI 2025 | SLO/resilience adjacent |
| Moderate | VEP NSDI 2025 | eBPF verifier context |
| Moderate | Rex (arXiv:2502.18832) | Safe extension motivation |
| Moderate | eBPF Runtime paper (arXiv:2410.00026) | eBPF background |

---

## Notes on Search Methodology

Searches conducted via arXiv.org full-text search and USENIX/ACM conference proceedings pages. The query space covered:
- "eBPF GPU" (direct hit: gpu_ext, eACGM, Darzi et al., Adaptyst)
- "bpftime userspace eBPF" (direct hit: bpftime OSDI 2025)
- "eBPF kernel extensibility policy" (hit: gpu_ext, beacon, eBPF verifier papers)
- "NCCL collective communication" (hit: Demystifying NCCL)
- "GPU distributed training communication bottleneck" (hit: Lagom, OptiNIC, etc.)
- NSDI 2025 and OSDI 2025 proceedings browsed directly

Google Scholar and ACM DL returned 403/rendering errors and were not usable.
