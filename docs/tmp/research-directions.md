# NCCL Policy Plane: Research Directions & Novelty Analysis

## Core Thesis

Transform NCCL from a black-box collective library into a **governable datapath** using verifiable, isolated, composable eBPF policies. The system — "NCCL-Policy" — abstracts NCCL's decision points into policy hooks, executes policies via bpftime/EIM (eBPF verification + LLVM JIT + MPK isolation), and exposes them through NCCL's native plugin system.

**Why this is NOT "just another tuner":**
- Tuners optimize for throughput. We enforce **governance objectives** (SLO, fairness, fault resilience, security).
- Tuners output static decisions. We provide **composable, verifiable, live-reloadable policy programs**.
- Tuners are trusted code. We provide **untrusted extension safety** (verification + hardware isolation).

## NCCL's "Programmability Surface" (Why Now)

### Plugin System (Decision + Execution + Observation)
| Plugin Type | Hook Point | What You Control |
|---|---|---|
| Tuner | Per-collective call | algo, protocol, nChannels (SM budget) |
| Net | Connection setup/teardown | NIC selection, vNIC, device offload |
| Env | Parameter initialization | All NCCL env var equivalents |
| Profiler | Event callbacks | Telemetry collection |

Key enabler: **one .so can implement all plugin types** → single deployment artifact.

### Runtime Governance APIs (Action Space)
| API | Purpose | Policy Use |
|---|---|---|
| `ncclCommRevoke` | Quiesce communicator | Fault isolation / stop-the-bleeding |
| `ncclCommShrink` | Remove failed ranks | Graceful degradation |
| `ncclCommGrow` | Add new ranks | Elastic recovery / scale-out |
| One-sided Host APIs | Zero-SM communication | Reduce compute interference |
| Device APIs (LLVM IR) | JIT/DSL integration | Future: device-side policy execution |

## Priority Research Directions

### Direction 1: Multi-Tenant QoS/Fairness (STRONGEST for paper)

**Problem:** Multiple training jobs on shared GPU nodes fight over NIC/NVLink/PCIe. NCCL has no concept of "fairness" or "SLO" — jobs interfere destructively, causing P99 step time spikes.

**What the policy does:**
- Per-collective: adjust nChannels based on job priority + current contention
- Per-connection: rate-shape via net plugin scheduling
- Closed-loop: telemetry → eBPF maps → policy decision → tuner/net action

**Novelty claim:** First system providing **communication SLO enforcement** at the collective runtime level with verifiable policies. Distinct from network-level QoS (which can't see collective semantics) and framework-level scheduling (which can't control per-collective decisions).

**Evaluation plan:**
- Workloads: data-parallel AllReduce, MoE AllToAll, mixed
- Metrics: P50/P99 step time, Jain's fairness, throughput
- Baselines: default NCCL, static tuner, NCCL env vars

### Direction 2: Safe Extensibility via bpftime/EIM

**Problem:** Platform operators want custom NCCL behavior but can't let users load arbitrary .so plugins into training processes — security/stability risk.

**What the policy does:**
- Policy programs are eBPF: verified at load time (bounded execution, memory safety)
- MPK isolation: hardware-enforced memory separation between policy and NCCL
- EIM capabilities: fine-grained access control (read-only monitoring vs. read-write firewall)
- Hot reload: update policies without restarting training

**Novelty claim:** First application of the EIM/bpftime safe extensibility model to GPU collective communication. Demonstrates that **verifiable extensions are feasible even in the latency-sensitive collective hot path** (~100ns overhead on ~10-1000μs operations = 0.01-1%).

### Direction 3: Policy-Driven Elastic Recovery

**Problem:** Node failures during large-scale training cause full-job restarts. NCCL now has revoke/shrink/grow APIs, but no automated policy to drive them.

**What the policy does:**
- Monitor: detect failure precursors (timeout patterns, retry bursts, latency anomalies)
- Act: trigger revoke → shrink → notify framework → grow when replacement ready
- Safety: hysteresis and confirmation thresholds to prevent false-positive disruption

**Novelty claim:** Policy-driven recovery at the **collective runtime layer** (below framework, above network), with verifiable policy ensuring the recovery logic itself can't cause worse failures.

### Direction 4: Compute-Communication Interference Control

**Problem:** NCCL communication kernels consume SM resources, starving compute kernels. The optimal SM budget for communication varies dynamically.

**What the policy does:**
- Monitor GPU SM occupancy via telemetry
- Dynamically adjust nChannels (fewer channels = fewer SM blocks for communication)
- Switch between SM-based and zero-SM (CopyEngine) communication paths
- Optimize for end-to-end iteration time, not communication bandwidth

### Direction 5: Programmable Network Device Policy

**Problem:** Multi-NIC, NIC fusion, heterogeneous topology — static device selection is fragile.

**What the policy does:**
- Dynamic NIC subset selection based on congestion, error rate, topology
- vNIC creation/destruction via net plugin's `makeVDevice`
- Device offload decisions based on workload characteristics

### Direction 6: Cross-Vendor Portable Policy (NCCL + RCCL)

**Problem:** Heterogeneous GPU clusters (NVIDIA + AMD) need unified governance.

**What the policy does:**
- Define vendor-neutral policy IR (eBPF)
- Thin adapters for NCCL tuner plugin and RCCL tuner plugin (APIs are similar)
- Same policy program works on both platforms

## Architecture: "One .so, Three Layers"

```
┌─────────────────────────────────────────────────────┐
│                Training Framework                    │
│            (PyTorch / DeepSpeed / etc.)              │
├─────────────────────────────────────────────────────┤
│                     NCCL                             │
│  ┌──────────┐ ┌──────────┐ ┌──────────┐ ┌────────┐ │
│  │  Tuner   │ │   Net    │ │   Env    │ │Profiler│ │
│  │  Plugin  │ │  Plugin  │ │  Plugin  │ │ Plugin │ │
│  └────┬─────┘ └────┬─────┘ └────┬─────┘ └───┬────┘ │
├───────┼─────────────┼───────────┼────────────┼──────┤
│       └─────────────┼───────────┘            │      │
│              ┌──────┴──────┐                 │      │
│              │  Policy     │◄────────────────┘      │
│              │  Runtime    │   (telemetry feed)      │
│              │  (bpftime)  │                         │
│              ├─────────────┤                         │
│              │ eBPF Policy │  ← verified, JIT'd,    │
│              │  Programs   │    MPK-isolated         │
│              ├─────────────┤                         │
│              │ eBPF Maps   │  ← shared-memory        │
│              │ (state/cfg) │    control plane         │
│              └─────────────┘                         │
│                nccl_policy.so                        │
└─────────────────────────────────────────────────────┘
```

## Anti-Patterns to Avoid (Reviewer Objections)

| Objection | Defense |
|---|---|
| "This is just a tuner" | Tuners optimize throughput; we enforce governance (SLO/fairness/fault). Different objective functions. |
| "Why eBPF, not just a plugin?" | Verification + MPK = safe untrusted extensions. Native plugins require trust. |
| "Overhead too high for hot path" | ~100ns on 10-1000μs operations = <1%. Show with microbenchmarks. |
| "Only microbenchmarks" | Full end-to-end training + multi-tenant + fault injection evaluation. |
| "NCCL-specific, not general" | EIM model is general; NCCL is the demanding case study. Also show RCCL portability. |
