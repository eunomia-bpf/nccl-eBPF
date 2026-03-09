# Related-Work Survey: eBPF + GPU Communication / Collectives / RDMA / GPU Policy

As of March 9, 2026.

## Scope and search method

I searched for combinations of `eBPF` with GPU communication, `NCCL`, collectives, RDMA, InfiniBand, HPC, and multi-tenant GPU management using official venue pages, DBLP, arXiv, project repositories, and official system documentation. I also checked OSDI, NSDI, EuroSys, ASPLOS, ATC, and SIGCOMM/eBPF workshop results from 2022-2025.

Important negative result: DBLP queries for `eBPF NCCL`, `eBPF RDMA`, `eBPF InfiniBand`, `eBPF MPI`, `eBPF GPU collective`, `eBPF Horovod`, and `eBPF PyTorch NCCL` returned zero hits on March 9, 2026. That does not prove absolute non-existence, but it is strong negative evidence that there is no established published line of work on an eBPF-native NCCL policy plane.

## Bottom line

The literature is sparse and splits into four buckets:

1. Papers that use eBPF/XDP to accelerate distributed training communication or aggregation, but do not integrate with NCCL directly.
2. Papers that use eBPF to observe NCCL/GPU behavior, but do not enforce policy.
3. Papers/projects that make GPU drivers or userspace applications extensible with eBPF, but not at the NCCL collective layer.
4. Adjacent non-eBPF systems that customize collectives or GPU transport, and therefore define the nearest comparison class for reviewers.

I did not find a paper or public project that does all of the following at once:

- targets existing NCCL collectives directly;
- exposes a safe, verified eBPF policy surface;
- supports multi-tenant or QoS-style policy decisions;
- operates as a dynamic policy plane rather than a static plugin or a replacement transport.

## 1. Directly relevant prior work

### 1.1 eBPF/XDP for distributed training communication and aggregation

| Work | Venue / year | Key idea | Relevance to an eBPF policy plane for NCCL |
|---|---|---|---|
| [XAgg: Accelerating Heterogeneous Distributed Training Through XDP-Based Gradient Aggregation](https://doi.org/10.1109/TNET.2023.3339524) | IEEE/ACM ToN 2024 | Uses XDP-based in-kernel gradient aggregation to accelerate heterogeneous distributed training. | Very close on "eBPF/XDP for collective-like communication", but it accelerates gradient aggregation itself rather than inserting a safe policy layer into NCCL. |
| [ALEPH: Accelerating Distributed Training With eBPF-Based Hierarchical Gradient Aggregation](https://doi.org/10.1109/TNET.2024.3404999) | IEEE/ACM ToN 2024 | Uses eBPF-based hierarchical gradient aggregation for distributed training. | One of the closest papers overall. Still not NCCL-native, not multi-tenant policy/QoS, and not a general safe policy plane. |
| [BOAD: Optimizing Distributed Communication with In-Kernel Broadcast and Aggregation](https://doi.org/10.1145/3672197.3673438) | eBPF@SIGCOMM 2024 | Pushes broadcast and aggregation into the kernel for distributed communication. | Conceptually close because it moves distributed communication logic into eBPF, but it is still communication acceleration rather than policy enforcement for NCCL collectives. |

### 1.2 eBPF observability for NCCL / GPU / distributed inference

| Work | Venue / year | Key idea | Relevance to an eBPF policy plane for NCCL |
|---|---|---|---|
| [eACGM: Non-Instrumented Performance Tracing and Anomaly Detection Towards Machine Learning Systems](https://doi.org/10.1109/IWQOS65803.2025.11143277) and [repo](https://github.com/shady1543/eACGM) | IWQoS 2025 | Full-stack AI/ML observability with eBPF across CUDA, Python, PyTorch, and NCCL; anomaly detection on collected metrics. | Closest existing NCCL-specific eBPF observability system. Strongly relevant, but it is tracing/diagnosis only, not inline policy enforcement. |
| [eInfer: Unlocking Fine-Grained Tracing for Distributed LLM Inference with eBPF](https://doi.org/10.1145/3748355.3748372) | eBPF@SIGCOMM 2025 | Fine-grained tracing for distributed LLM inference with eBPF. | Relevant on distributed inference observability. Not collective policy, not NCCL control, not QoS. |
| [Host-Side Telemetry for Performance Diagnosis in Cloud and HPC GPU Infrastructure](https://doi.org/10.48550/arXiv.2510.16946) | CoRR 2025 | eBPF-based host telemetry for diagnosing GPU tail latency and contention in cloud/HPC environments. | Relevant to multi-tenant GPU cluster diagnosis and communication bottlenecks; again, diagnosis only, not policy. |

### 1.3 Safe extensibility foundations that could enable NCCL policy

| Work | Venue / year | Key idea | Relevance to an eBPF policy plane for NCCL |
|---|---|---|---|
| [Extending Applications Safely and Efficiently](https://www.usenix.org/conference/osdi25/presentation/zheng-yusheng) and [bpftime project](https://github.com/eunomia-bpf/bpftime) | OSDI 2025 | Introduces EIM and bpftime for safe, efficient userspace application extension with eBPF-style verification and isolation. | This is the strongest direct foundation for a safe NCCL policy plane. It gives the extensibility and safety model, but the OSDI paper itself is not about NCCL, collectives, or GPU communication. |
| [gpu_ext: Extensible OS Policies for GPUs via eBPF](https://arxiv.org/abs/2512.12615) and [repo](https://github.com/eunomia-bpf/gpu_ext) | CoRR 2025 | eBPF policies in GPU driver/device layers for memory placement, scheduling, and observability, including multi-tenant scenarios. | The closest paper to "GPU policy via eBPF". However it targets GPU driver/device policy, not NCCL collectives or RDMA/InfiniBand policy. |

## 2. Adjacent eBPF systems reviewers may cite

These are not GPU-collective systems, but they are likely comparison points because they show eBPF being used as a safe programmable substrate for distributed or kernel-resident logic.

| Work | Venue / year | Key idea | Why it matters |
|---|---|---|---|
| [Electrode: Accelerating Distributed Protocols with eBPF](https://www.usenix.org/conference/nsdi23/presentation/zhou) | NSDI 2023 | Executes distributed-protocol fast paths in eBPF before the normal networking stack. | Strong precedent for moving distributed-control logic into eBPF. Reviewers may argue "your work is Electrode for NCCL." |
| [DINT: Fast In-Kernel Distributed Transactions with eBPF](https://www.usenix.org/conference/nsdi24/presentation/zhou-yang) | NSDI 2024 | Uses eBPF to accelerate distributed transaction processing in-kernel. | Another precedent for safe in-kernel distributed systems logic. Not GPU-specific. |
| [eTran: Extensible Kernel Transport with eBPF](https://www.usenix.org/conference/nsdi25/presentation/chen-zhongjie) | NSDI 2025 | Makes kernel transports extensible with eBPF while keeping protection and performance. | Probably the closest generic eBPF transport paper. If your work touches transport policy, reviewers will compare against this. |
| [PageFlex: Flexible and Efficient User-space Delegation of Linux Paging Policies with eBPF](https://www.usenix.org/conference/atc25/presentation/yelam) | USENIX ATC 2025 | Uses eBPF as a safe policy interface for paging decisions. | Good precedent for "policy plane" as opposed to "new mechanism". Useful for framing novelty around policy rather than transport redesign. |
| [XRP: In-Kernel Storage Functions with eBPF](https://www.usenix.org/conference/osdi22/presentation/zhong) | OSDI 2022 | Safe in-kernel storage extensions via eBPF. | Broad precedent for safe extensibility in performance-critical systems, but unrelated to GPU communication. |

## 3. Adjacent non-eBPF collective / GPU transport systems

These do not use eBPF, but they are the nearest non-eBPF alternatives for reviewers asking whether NCCL already has enough programmability.

| System | Venue / year | Key idea | Relevance |
|---|---|---|---|
| [MSCCL](https://github.com/microsoft/msccl) | Project; papers in ASPLOS 2022/2023, NSDI 2023, PPoPP 2021 listed in repo | Custom collective algorithms on top of NCCL using a toolkit/compiler/runtime stack. | Important boundary system. It customizes collective algorithms, not safe inline policy enforcement. |
| [MSCCL++: Rethinking GPU Communication Abstractions for Cutting-edge AI Applications](https://arxiv.org/abs/2504.09014) and [repo](https://github.com/microsoft/mscclpp) | CoRR 2025 / project | GPU-driven communication abstractions for highly customized AI communication patterns. | Very relevant if your work changes the communication API or execution model. It is not eBPF and not a policy plane for unmodified NCCL. |
| [AWS OFI-NCCL](https://github.com/aws/aws-ofi-nccl) | Project | NCCL network plugin using libfabric and GPUDirect RDMA-capable providers. | Shows NCCL already supports transport plugins, but those are static C/C++ plugins, not verified dynamic policies. |
| [An Extensible Software Transport Layer for GPU Networking](https://arxiv.org/abs/2504.17307) | CoRR 2025 | UCCL decouples RDMA control/data paths to make GPU networking transport extensible in software. | Closest non-eBPF system if the paper is interpreted as a transport-policy paper rather than a library policy-plane paper. |
| [NCCL](https://github.com/NVIDIA/nccl) official plugin surfaces | Project | NCCL exposes network, tuner, profiler, and environment plugin APIs. | Important reviewer baseline. These APIs already allow some customization, but they are not a general verified eBPF policy substrate. |

## 4. What NCCL already exposes, and why that still leaves room

Official NCCL documentation and repository state show these extension points:

- [Net plugins](https://github.com/NVIDIA/nccl/tree/master/plugins/net): external network transports and optional collNet support for in-network collectives.
- [Tuner plugins](https://github.com/NVIDIA/nccl/tree/master/plugins/tuner): override algorithm/protocol/channel selection by modifying cost tables.
- [Profiler plugins](https://github.com/NVIDIA/nccl/tree/master/plugins/profiler): extract collective, kernel, proxy, and net-plugin events.
- [Environment plugins](https://github.com/NVIDIA/nccl/tree/master/plugins/env): customize environment-variable resolution and configuration handling.
- The official NCCL tree also contains Google-specific plugin directories such as `plugins/net/google-fastsocket` and `plugins/profiler/google-CoMMA`, which again reinforce that the current extensibility model is plugin-oriented, not eBPF-oriented.

This matters because a reviewer can reasonably ask: "why not just use NCCL plugins?" The short answer is that plugins are not the same thing as a safe eBPF policy plane:

- NCCL plugins are shared libraries written in C/C++ and compiled against NCCL interfaces.
- They are not statically verified in the eBPF sense.
- They are not designed as hot-swappable per-tenant policy programs with shared eBPF maps and controlled helper surfaces.
- The tuner/profiler/env/net split is narrower than a unified policy plane spanning API admission, per-communicator state, network decisions, fairness, and observability.

## 5. Main-venue scan summary (2022-2025)

### OSDI

- Directly relevant: [Extending Applications Safely and Efficiently](https://www.usenix.org/conference/osdi25/presentation/zheng-yusheng) (OSDI 2025).
- Generic eBPF systems only: [XRP](https://www.usenix.org/conference/osdi22/presentation/zhong), verifier work in OSDI 2024.
- No NCCL-specific or GPU-collective eBPF paper found.

### NSDI

- Relevant generic precedents: [Electrode](https://www.usenix.org/conference/nsdi23/presentation/zhou), [DINT](https://www.usenix.org/conference/nsdi24/presentation/zhou-yang), [eTran](https://www.usenix.org/conference/nsdi25/presentation/chen-zhongjie).
- No NCCL-specific or GPU-collective eBPF paper found.

### ATC

- Relevant generic precedent: [PageFlex](https://www.usenix.org/conference/atc25/presentation/yelam).
- No NCCL-specific or GPU-collective eBPF paper found.

### EuroSys

- I found only generic eBPF work, such as eBPF library/performance/verifier papers, not GPU/NCCL collective work.

### ASPLOS

- I found only generic eBPF papers, such as Merlin and eHDL, not GPU/NCCL collective work.
- DBLP query `NCCL venue:ASPLOS` returned zero hits on March 9, 2026.

### SIGCOMM

- Main SIGCOMM: I did not find NCCL/eBPF collective work in the main conference via DBLP title search.
- eBPF workshop: [BOAD](https://doi.org/10.1145/3672197.3673438) in 2024 and [eInfer](https://doi.org/10.1145/3748355.3748372) in 2025 are relevant.

## 6. Special check: does the bpftime / EIM OSDI paper already cover GPU collectives?

Short answer: no, not in the published OSDI paper.

What I verified:

- The OSDI 2025 paper page describes bpftime/EIM as a general userspace application-extension framework.
- Grepping the OSDI 2025 PDF for `NCCL`, `collective`, `allreduce`, `allgather`, `reduce-scatter`, `GPU`, `CUDA`, and `PTX` returned no matches.
- The public [bpftime repository](https://github.com/eunomia-bpf/bpftime) README does claim GPU kernel hooks and PTX injection support.

Interpretation:

- The paper gives a strong safe-extensibility foundation.
- The repository suggests technical feasibility for GPU hooks.
- But there is no published bpftime/EIM paper result, as of March 9, 2026, that already claims an NCCL collective policy plane.

## 7. Gaps: what has not been done

The following gaps appear to be genuinely open:

1. No published eBPF work on NCCL-native policy enforcement.
   - I found tracing, aggregation acceleration, and GPU-driver policy work.
   - I did not find an eBPF system that attaches to NCCL collectives and enforces allow/deny/rate/fairness/QoS policy.

2. No published eBPF work on RDMA/InfiniBand policy enforcement for ML collectives.
   - I found transport extensibility papers and observability papers.
   - I did not find a paper specifically on eBPF-enforced RDMA/InfiniBand policy for NCCL/MPI-style collectives.

3. No safe extensibility interface inside existing HPC/ML communication runtimes.
   - bpftime/EIM gives a general application-extension substrate.
   - NCCL gives plugin APIs.
   - But there is no published design that combines verifier-backed safe policies with collective-runtime semantics.

4. No multi-tenant GPU cluster work tying collective policy to tenant-level fairness or QoS through eBPF.
   - gpu_ext is close at the GPU driver/device layer.
   - Host-side telemetry and eACGM are close on diagnosis.
   - But I found no collective-aware policy plane that reasons about tenants, communicators, budgets, and network contention together.

5. No cross-layer NCCL policy plane spanning collective API, transport state, and cluster context.
   - Existing work usually lives in one layer only:
   - aggregation path (`XAgg`, `ALEPH`, `BOAD`);
   - observability (`eACGM`, `eInfer`, host-side telemetry);
   - transport substrate (`eTran`, `UCCL`, AWS OFI-NCCL);
   - GPU driver/device policy (`gpu_ext`).

## 8. Novelty assessment for "eBPF policy plane for NCCL"

### 8.1 Where the idea is genuinely new

An `eBPF policy plane for NCCL` looks genuinely novel if the contribution is framed as:

- safe, verifier-backed, dynamically updatable policy logic for existing NCCL collectives;
- policy on unmodified applications and existing NCCL deployments;
- policy decisions based on collective semantics, not just low-level packets;
- support for multi-tenant fairness, admission control, rate limiting, or QoS;
- shared state across jobs/communicators/ranks via maps;
- optional coupling to NCCL transport or tuner decisions, but without becoming "just another static plugin".

This combination does not appear in prior work.

### 8.2 Where the idea is only moderately new

Novelty is weaker if the system only does:

- NCCL tracing via eBPF;
- collective profiling or anomaly detection;
- pure algorithm/protocol selection with no new safe policy substrate;
- a thin wrapper around NCCL's existing tuner/profiler plugin APIs.

In those cases, reviewers can reasonably point to `eACGM`, `eInfer`, and official NCCL plugins.

### 8.3 Where the idea would not be new enough

Novelty is weak if the implementation is only:

- "use eBPF uprobes to log NCCL calls";
- "provide a tuner plugin with some heuristics";
- "collect NCCL telemetry and visualize it";
- "restate bpftime as the contribution without a collective-specific design".

That would look derivative of existing observability work or of bpftime itself.

## 9. Likely reviewer objections and counters

### Objection 1

`NCCL already has tuner/net/profiler plugins. Why do you need eBPF?`

Counter:

- Plugins are static shared libraries, not verifier-backed dynamic policies.
- Tuner plugins only modify cost tables for algorithm/protocol selection.
- Profiler plugins observe; they do not enforce.
- Net plugins replace transport implementations; they do not provide a unified per-collective policy substrate.
- The key novelty is safe, dynamic policy composition with controlled helper access and shared state.

### Objection 2

`This is just MSCCL/MSCCL++ or UCCL with a different implementation language.`

Counter:

- MSCCL/MSCCL++ focus on custom collective algorithms and communication abstractions.
- UCCL focuses on GPU transport extensibility.
- An NCCL eBPF policy plane is orthogonal: it governs existing collective execution rather than replacing the collective library or transport with a new stack.
- The deployment story is also different: operator-controlled dynamic policy insertion into existing NCCL workloads.

### Objection 3

`eACGM and eInfer already use eBPF around NCCL / distributed inference.`

Counter:

- Those systems are observability/diagnosis systems.
- They do not define a safe inline policy language for collective admission, fairness, rate limiting, or QoS.
- Your contribution has to be about enforcement and control, not just tracing.

### Objection 4

`gpu_ext already does GPU policies with eBPF.`

Counter:

- gpu_ext acts at the GPU driver/device layer for memory and scheduling.
- NCCL policy lives at the communication-runtime layer, where the objects are communicators, collectives, message sizes, peers, transport choices, and tenant budgets.
- The two are complementary rather than identical.

### Objection 5

`ALEPH/XAgg/BOAD already put communication logic in eBPF.`

Counter:

- Those papers accelerate or restructure aggregation/broadcast paths.
- They are not safe policy planes for existing NCCL semantics.
- They do not target multi-tenant collective policy or runtime governance.

### Objection 6

`Safety claims around eBPF are overstated because eBPF verification is imperfect.`

Counter:

- Do not oversell safety as "impossible to fail".
- Phrase it as bounded, verifier-backed safety plus controlled helper interfaces and fail-safe rollback.
- If using bpftime/EIM, emphasize defense in depth and the narrowness of the exposed policy interface.

### Objection 7

`Why not modify NCCL directly?`

Counter:

- Direct NCCL modifications tie policy rollout to NCCL release engineering and per-version maintenance.
- A policy plane separates mechanism from operator policy.
- This is especially attractive for cluster operators who need hot updates, per-tenant policies, and rapid experimentation.

### Objection 8

`Where is the systems contribution beyond hooking?`

Counter:

The paper should make at least one of these crisp:

- a new NCCL policy abstraction and API surface;
- a safety model for collective-runtime extensibility;
- a cross-layer policy-state model spanning NCCL and transport state;
- a multi-tenant scheduler/QoS design with measurable fairness or tail-latency gains;
- a compatibility story with existing NCCL plugin mechanisms.

## 10. Recommended paper positioning

The strongest positioning is:

- "safe, dynamic, verifier-backed collective policy for existing NCCL deployments"

not:

- "another collective implementation"
- "another NCCL profiler"
- "another RDMA transport"

The best experimental angles are likely:

- multi-tenant fairness/QoS between jobs sharing NIC/GPU/network resources;
- admission/rate control for pathological collectives;
- policy-guided algorithm or transport selection under contention;
- low-overhead enforcement relative to NCCL plugins or user-space policy daemons;
- safe hot updates and failure isolation.

## 11. Source index

### Direct prior work

- ALEPH: <https://doi.org/10.1109/TNET.2024.3404999>
- XAgg: <https://doi.org/10.1109/TNET.2023.3339524>
- BOAD: <https://doi.org/10.1145/3672197.3673438>
- eACGM paper: <https://doi.org/10.1109/IWQOS65803.2025.11143277>
- eACGM arXiv: <https://arxiv.org/abs/2506.02007>
- eACGM repo: <https://github.com/shady1543/eACGM>
- eInfer: <https://doi.org/10.1145/3748355.3748372>
- Host-Side Telemetry: <https://doi.org/10.48550/arXiv.2510.16946>
- bpftime / EIM paper: <https://www.usenix.org/conference/osdi25/presentation/zheng-yusheng>
- bpftime repo: <https://github.com/eunomia-bpf/bpftime>
- gpu_ext paper: <https://arxiv.org/abs/2512.12615>
- gpu_ext repo: <https://github.com/eunomia-bpf/gpu_ext>

### Adjacent eBPF systems

- Electrode: <https://www.usenix.org/conference/nsdi23/presentation/zhou>
- DINT: <https://www.usenix.org/conference/nsdi24/presentation/zhou-yang>
- eTran: <https://www.usenix.org/conference/nsdi25/presentation/chen-zhongjie>
- PageFlex: <https://www.usenix.org/conference/atc25/presentation/yelam>
- XRP: <https://www.usenix.org/conference/osdi22/presentation/zhong>

### Adjacent non-eBPF systems and official docs

- UCCL: <https://arxiv.org/abs/2504.17307>
- MSCCL: <https://github.com/microsoft/msccl>
- MSCCL++: <https://github.com/microsoft/mscclpp>
- MSCCL++ paper: <https://arxiv.org/abs/2504.09014>
- AWS OFI-NCCL: <https://github.com/aws/aws-ofi-nccl>
- NCCL repo: <https://github.com/NVIDIA/nccl>
- NCCL net plugin docs: <https://github.com/NVIDIA/nccl/tree/master/plugins/net>
- NCCL tuner plugin docs: <https://github.com/NVIDIA/nccl/tree/master/plugins/tuner>
- NCCL profiler plugin docs: <https://github.com/NVIDIA/nccl/tree/master/plugins/profiler>
- NCCL env plugin docs: <https://github.com/NVIDIA/nccl/tree/master/plugins/env>

## 12. Final assessment

Inference from the collected evidence:

- There is prior work on eBPF for distributed training communication acceleration.
- There is prior work on eBPF for NCCL/GPU observability.
- There is prior work on eBPF for GPU driver/device policy.
- There is no clear prior work on a safe eBPF policy plane inside NCCL itself.

That means the paper can credibly claim novelty, but only if it emphasizes policy, safety, dynamic deployment, and multi-tenant control. If it collapses into tracing or simple plugin heuristics, the novelty story becomes much weaker.
