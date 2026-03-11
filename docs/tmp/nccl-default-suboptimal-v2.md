# NCCL Default Tuner Suboptimality: Evidence Survey

**Date**: 2026-03-10
**Purpose**: Catalog real cases where NCCL's default tuner makes genuinely wrong algo/proto/channel
selections — without artificial perturbation of the cost model.

---

## Summary of Findings

NCCL's default tuner is "near-optimal" for the single-node AllReduce on homogeneous testbeds
(confirmed by our sweep). However, the literature and GitHub issue tracker document numerous
production scenarios where defaults are measurably wrong. The cases fall into six categories:

1. **Fixed threshold mismatch on new hardware** — empirically-derived breakpoints from old GPUs
2. **NVLSTree over-selection** — chosen when Ring/LL is dramatically faster for small messages
3. **Ring over-selection vs Tree/PAT at scale** — AllGather/ReduceScatter, multi-node
4. **NVLS under-selection for ReduceScatter** — preference for Ring sends 12.7% more data
5. **Topology detection failures** — wrong algorithm flows from wrong topology graph
6. **Multi-tenant interference** — defaults assume single-tenant, no per-job QoS

Each section below gives the concrete evidence, sources, and reproducibility on our testbed.

---

## 1. Fixed LL/LL128/Tree Thresholds Are Wrong on New Hardware

### The structural problem

NCCL's protocol-selection thresholds are empirically hardcoded values derived from a specific
generation of hardware. The "Demystifying NCCL" paper (HotI 2025, arXiv:2507.04786) states:

> "The current thresholds for switching protocols (e.g., around 64 KiB for LL to Simple, 256 KiB
> for Tree to Ring) are fixed, empirically derived values from current-generation GPUs."

The paper benchmarks on Alps supercomputer (16 nodes, GH200 Grace Hopper Superchips) and finds:

> "As the AllReduce message size increases to the gigabyte range across 16 nodes, [LL and LL128]
> performance drops sharply compared to the Simple protocol."

Furthermore: "LL128 can even lag behind LL because the extra cost per 128-byte operation becomes
significant at scale."

### Specific mechanism

The LL protocol's overhead model was calibrated on Volta/Ampere-era bandwidth and latency. On
Hopper's higher compute throughput, the protocol pays more sync overhead relative to the available
memory bandwidth, making the LL→Simple crossover point shift downward in message size. NCCL's
static 64 KiB threshold does not adapt to this.

### Reproducibility on our testbed (RTX 5090 / socket)

Our own sweep already showed this for socket transport: at 16 MB, forcing `NCCL_PROTO=LL` causes
**39.5x slowdown** relative to the default (which correctly switches to Simple). This is not NCCL
making the wrong choice — but it shows the *exact mechanism* by which the fixed threshold is
critical. On a new-generation GPU where the bandwidth ratio shifts, NCCL might not switch *early
enough*, leaving LL active into the medium-message range where Simple is faster.

**Potential experiment**: On the RTX 5090 (Blackwell), compare NCCL default algo/proto at message
sizes from 4 KB to 256 KB against exhaustive sweep. Look for a bandwidth dip at the
LL→Simple transition — that dip is the "wrong threshold" signal described in the NVIDIA tuner blog.

---

## 2. NVLSTree Over-Selection for Small Messages

### GitHub Issue #1362 (NVIDIA/nccl)

> "When the total number of GPUs is large and message size is 1 [byte], NVLSTree AllReduce achieves
> ~300ms vs Ring's ~8ms."

Performance gap: NVLSTree is **37.5x slower** than Ring/LL for tiny messages. The maintainer
acknowledged: "For small message size, the latency of Ring LL algorithm is lower than that of
NVLSTree." Issue closed as "not planned."

### PyTorch Issue #117748 (pytorch/pytorch), NCCL 2.19.3

A regression on 8× AWS P5 nodes (H100) for **4 GB AllReduce**:
- Before NCCL fix: ~81,146 µs (52.93 GB/s)
- After NCCL fix (cherry-picked PR #1112): ~30,222 µs (142.11 GB/s)
- **2.7x degradation** caused by NCCL 2.19.3 selecting NVLSTree in a configuration where the
  previous version did not.

### GB200 Issue #1801 (NVIDIA/nccl), 3-group NVL partitions

On GB200 with NVLS disabled (IB-only), for **16 GB AllReduce**:
- Ring (NCCL default): 196.72 GB/s bus BW (NVL4) / 393.66 GB/s (NVL8)
- Tree (overridden): 234.43 GB/s bus BW (NVL4) / 485.68 GB/s (NVL8)
- **Tree is 19–23% faster**, yet NCCL defaults to Ring.

The maintainer confirmed: tuning optimization exists for 2-group configurations but not for
3-group (3 NVL partitions). This is an explicit tuning gap in the default model.

### Why this matters for our paper

NVLSTree/NVLS hardware is increasingly deployed. NCCL's cost model for these new algorithms has
documented gaps where the tuner selects them incorrectly (over- or under-selecting relative to
optimal). An eBPF policy that observes collective type + message size + topology can prevent these.

---

## 3. Ring Over-Selection for AllGather/ReduceScatter at Scale

### AllGather/ReduceScatter historically had only Ring

Until NCCL 2.23 (PAT algorithm, 2024), AllGather and ReduceScatter had **only Ring**. TACCL (NSDI
2023, Microsoft Research) demonstrated that NCCL's Ring AllGather is substantially suboptimal:

- On DGX-2 testbed: TACCL AllGather is **up to 6.7× faster** than NCCL for small-to-moderate sizes
- For large sizes: **up to 25% faster** (nearly saturates inter-node bandwidth)
- On Azure NDv2 testbed: TACCL achieves **12–35% speedup** for 1KB–1MB, **61%–3.4× speedup** for
  sizes > 1MB

Root cause: NCCL's Ring sends data along a linear ring topology that may poorly match the physical
cluster topology (fat-tree, DGX-H100 rack topology). TACCL exploits the actual bandwidth graph.

### PAT algorithm introduction (NCCL 2.23)

NVIDIA acknowledged Ring's latency problem for AllGather/ReduceScatter by introducing PAT
(Parallel Aggregated Trees) in 2.23. PAT provides logarithmic latency scaling vs Ring's linear
scaling. The announcement explicitly states:

> "You can expect small to medium message sizes to perform better with PAT, with this improvement
> increasing as your workload scales."

Implication: before NCCL 2.23, Ring was the only option and was suboptimal for small/medium
messages. After 2.23, the question is whether the tuner correctly selects PAT vs Ring at the
boundary.

### MCCS (SIGCOMM 2024)

MCCS demonstrates **up to 2.4× improvement** over NCCL for collective communication in multi-tenant
cloud by dynamically selecting algorithms based on network conditions. The paper documents NCCL's
"one-size-fits-all" static ring selection failing to adapt to network contention patterns.

> "Using libraries to implement collective communication algorithms is not a good fit for a
> multi-tenant cloud environment because the tenant is not aware of the underlying physical network
> configuration or how other tenants use the shared cloud network."

---

## 4. NVLS Under-Selection for H100 ReduceScatter

### GitHub Issue #1047 (NVIDIA/nccl)

On a single H100 node with NVSwitch, for **ReduceScatter at 128MB–8GB**:
- Ring: **344.5 GB/s** average bus bandwidth
- NVLS (what some configs select): **305.7 GB/s** average bus bandwidth
- Ring is **12.7% faster** than NVLS for this operation

Root cause (from NCCL contributor Kaiming Ouyang): NVLS reduce-scatter with load-reduce sends
`nRanks * count` bytes to the NVSwitch, while Ring only sends `(nRanks - 1) * count`. NVLS
sends more data. Issue closed as "not planned" — i.e., NCCL accepted that its NVLS selection
for ReduceScatter may be suboptimal in this bandwidth-sensitive range.

### Separate constraint: reduce-scatter with NVLS and avg reduction

Issue #1670 documents a related case: NCCL silently falls back from NVLS to Ring when an
unsupported reduction type (avg) is used with float32. The user gets unexpected algorithm
selection with no diagnostic output. This is an invisible suboptimality — NCCL selects Ring not
because Ring is optimal, but because NVLS is unavailable for this op/type combo.

---

## 5. Topology Detection Failures Causing Wrong Algorithm Selection

### Inconsistent Algo/Proto in Heterogeneous CPU Clusters (Issue #1136)

This is the most severe documented class of wrong selection: **each rank picks a different
algorithm/protocol**, causing deadlocks during collective operations.

**Configuration**: A800 GPU cluster with mixed Intel + AMD CPUs on different hosts.

**Root cause**: NCCL 2.18.1+ added CPU vendor-specific overhead calculation in `tuning.cc`. AMD
and Intel hosts computed different `llMaxBws` and latency estimates, causing:
- AMD rank: selects Ring/LL for message size X
- Intel rank: selects Ring/Simple for message size X
- Result: ranks disagree on protocol → deadlock

Affected versions: 2.18.1, 2.19.3. Fixed in 2.24+ by using aligned CPU type from peer info.

Workaround: `NCCL_NET_OVERHEAD=1000` (Intel value) forced consistent selection.

**Implication for our work**: This is a multi-host tuning consistency failure. An eBPF policy
that enforces a consistent algo/proto across all ranks prevents this class of bug.

### Incorrect Topology Graph Under PyTorch (Issue #933)

On H800 cluster (8-GPU/node, 9 RoCE NICs, NVLink):
- nccl-tests selects graph patterns 3 and 4 (correct, all NICs balanced)
- PyTorch+NCCL selects graph patterns 1 and 4 (wrong, 2 NICs idle)

Root cause: NVML API (`ncclNvmlDeviceGetNvLinkRemotePciInfo`) returns invalid PCI info (`fffffff:ffff:ff`), causing NCCL to fall back to non-NVLink topology. NIC load becomes unbalanced.

**Performance impact**: NICs 5 and 7 show zero traffic, meaning ~22% of available inter-node
bandwidth is unused.

### Alternating Rings Bug in ncclTopoPostset() (Issue #1494)

On heterogeneous 2-host Hopper clusters, a bug in `graph/connect.cc`:

```c
// Buggy:
if (graphs[NCCL_ALGO_RING]->crossNic && (nChannels % 2) == 0)
// Correct:
if (graphs[NCCL_ALGO_RING]->crossNic == 2 && (nChannels % 2) == 0)
```

The wrong condition triggers ring alternation in cases where it shouldn't, causing NIC-to-GPU
distance mismatches. NICs send PFC (pause flow control) frames, causing network congestion and
throughput collapse. Fixed in recent NCCL versions, but demonstrates how a single wrong comparison
in the topology logic cascades to wrong ring selection and severe performance loss.

---

## 6. AllToAll Performance Regression: Version-Induced Wrong Channel Count

### Issue #1316: NCCL 2.19/2.20 vs 2.18 on H800 + RoCE

On 8-node H800 cluster with NVSwitch and RoCE:
- NCCL 2.18: achieves **90% switch port utilization** (50 GB/s) at all node counts 1–8
- NCCL 2.19/2.20: drops to **~60% utilization** (37 GB/s) at 3–8 nodes

Root cause (from sjeaugey, NVIDIA): "In 2.19 we have reduced the memory footprint and SM usage to
something more reasonable." The change in chunk size / timing / algorithm caused the AllToAll
pattern to no longer fill the RoCE network efficiently.

Mitigation: Setting `NCCL_NCHANNELS_PER_NET_PEER=32` recovers ~85% utilization, but this is not
the default.

**Implication**: Default channel count selection for AllToAll is wrong after 2.19. A tuner plugin
that sets `nChannels` for AllToAll based on network type/rank count would fix this.

### Issue #1298: PCIe bandwidth detection → wrong SM count → performance regression

On AWS g5.48xlarge (8× A10G, PCIe topology):
- NCCL 2.20.5 uses 4 SMs (assumes PCIe Gen4 x16 = 24 GB/s assumed BW)
- NCCL 2.18.5 uses fewer SMs (assumed PCIe Gen3 x16 = 12 GB/s)
- More SMs hurt performance when actual bandwidth is very low (NPS=1 config)

Root cause: NCCL reads PCIe speed from `/sys`. When AWS /sys reports the wrong value, NCCL
miscalibrates its SM allocation, creating more channels/blocks than the link can saturate.

---

## 7. TCP/Socket Transport: Default Channel Count Wrong

### Issue #209 (NCCL 2.4 regression)

On 32 Gbps TCP network (2-rank test):
- NCCL 2.4 default: ~2 GB/s (**6% wire speed utilization**)
- Single socket limitation: one TCP connection maxes at ~10 Gbps on cloud kernels
- NCCL 2.3.7 with 8 rings: ~3.75 GB/s (still only 12%)

Root cause: NCCL defaulted to a single socket per connection. The tuner never adjusts
`NCCL_SOCKET_NTHREADS` or `NCCL_NSOCKS_PERTHREAD` based on actual network bandwidth.

Fix: Setting `NCCL_SOCKET_NTHREADS=2 NCCL_NSOCKS_PERTHREAD=4` achieves 8–10 GB/s on AWS P3dn.

**On our testbed**: This is directly relevant. Our socket transport is getting 4.3ms floor.
NCCL's default socket thread/connection count may not be optimal. However, our testbed uses
loopback (same machine), so the bottleneck is software/CPU, not wire bandwidth — different regime.

---

## 8. NCCL Tuner Blog Case Study: Bandwidth Dip at Protocol Transition

### NVIDIA Technical Blog (Aug 2025): "Understanding NCCL Tuning"

The NVIDIA tuner blog (directly linked from NCCL documentation) shows a concrete case study where
NCCL's default selection produces a **visible bandwidth dip** at a message size transition:

> "If you see performance dipping when increasing message size, that's a strong signal of a bad
> transitional point in tuning."

The case study shows:
- "Suboptimal algorithm selections from 4MB through 512MB message ranges degraded bandwidth
  utilization significantly"
- After tuner plugin deployment: "clean S-curve performance across all message sizes, maintaining
  near-peak bandwidth utilization throughout the range"

The blog explicitly acknowledges:
> "Due to a variety of factors like network switch vendor, virtualization, CPU, or PCI
> configuration, NCCL tunings need to be tweaked to reach optimal performance."

This is NVIDIA's own admission that the default tuner is hardware-specific and will make wrong
choices on configurations it was not calibrated for.

---

## 9. Reproducibility Assessment for Our Testbed

### What we can test (1× RTX 5090, 2 ranks, socket)

| Scenario | Reproducible? | Expected Signal |
|----------|--------------|-----------------|
| LL→Simple threshold on Blackwell | **Yes** | Bandwidth dip at transition point vs. forced-Simple sweep |
| Channel count for AllGather vs AllReduce | **Yes** | Run `all_gather_perf` with default vs. `NCCL_MIN_NCHANNELS` sweep |
| ReduceScatter default channel count | **Yes** | Same approach as AllGather |
| Broadcast default vs forced Ring/LL | **Yes** | `broadcast_perf` sweep |
| Socket thread count calibration | **Yes** | `NCCL_SOCKET_NTHREADS` sweep for socket transport |
| 3+ rank Ring vs Tree difference | **Possibly** | `NCCL_HOSTID` hack for 3–4 ranks; may hit prior instability |

### What we cannot test

| Scenario | Why Not |
|----------|---------|
| NVLSTree suboptimality | No NVSwitch; no multi-node NVLink |
| Mixed CPU vendor deadlock | Homogeneous single-machine |
| NVLS ReduceScatter vs Ring | No NVSwitch |
| Alternating rings NIC topology | No InfiniBand/RoCE; no multi-node |
| AllToAll channel regression | AllToAll not in standard nccl-tests scope |

### Recommended experiment to run

**Experiment A: AllGather/ReduceScatter channel suboptimality**

```bash
# AllGather sweep: default channels vs forced 1, 2, 4, 8 channels
for nc in 1 2 4 8; do
  NCCL_MIN_NCHANNELS=$nc NCCL_MAX_NCHANNELS=$nc \
    mpirun -n 2 all_gather_perf -b 1K -e 256M -f 2 -g 1
done
```

NCCL default behavior: for small messages, it reduces channel count below its default. If the
reduction is too aggressive (e.g., drops to 1 channel when 2 would be faster), we observe this
as a bandwidth shortfall vs the 2-channel forced run.

**Experiment B: Protocol transition dip on AllReduce**

```bash
# Exhaustive proto sweep at fine-grained sizes near the LL→Simple boundary
for proto in LL LL128 Simple; do
  NCCL_PROTO=$proto mpirun -n 2 all_reduce_perf -b 4K -e 512K -f 1.2 -g 1
done
```

Compare with default (no NCCL_PROTO set). If default shows lower bandwidth at any point compared
to the forced-optimal sweep, that is a "wrong threshold" signal.

**Experiment C: Broadcast vs AllReduce tuning disparity**

NCCL optimizes primarily for AllReduce. For Broadcast, the cost model may not be as well-calibrated.

```bash
# Broadcast sweep: NCCL default vs forced algo/proto
mpirun -n 2 broadcast_perf -b 1K -e 256M -f 2 -g 1
for algo in Ring Tree; do
  for proto in LL Simple; do
    NCCL_ALGO=$algo NCCL_PROTO=$proto \
      mpirun -n 2 broadcast_perf -b 1K -e 256M -f 2 -g 1
  done
done
```

---

## 10. Implications for the Paper's Motivation

### The right framing for our system

Our paper should NOT claim "eBPF policies beat NCCL defaults on our testbed" (they don't, for
AllReduce on socket). Instead, the correct framing is:

**NCCL's default tuner is provably wrong in documented production scenarios. Our system provides
the mechanism to fix these without modifying NCCL source or recompiling.**

Specific evidence to cite in the paper:

1. **Protocol collapse prevention** (our strongest result): 39.5× slowdown from LL at 16MB. NCCL
   itself says this is a real hazard — the tuner blog warns about "bad transitional points." Our
   eBPF policy prevents forced-LL collapse.

2. **NVLSTree over-selection** (Issue #1362, #117748): 37.5× and 2.7× degradation from wrong
   algorithm selection on production H100 clusters. A size-aware eBPF policy prevents NVLSTree
   selection below the crossover threshold.

3. **Heterogeneous-CPU consistency** (Issue #1136): Deadlocks from per-rank tuning divergence.
   An eBPF policy enforcing a fixed algo/proto removes the CPU-vendor dependency entirely.

4. **Multi-tenant interference** (MCCS, SIGCOMM 2024): NCCL has no SLO-awareness. MCCS achieves
   2.4× improvement by adding policy-driven algorithm selection. Our eBPF framework provides the
   same hook point at zero modification to NCCL source.

5. **New-hardware threshold staleness**: As GPU architectures evolve (Hopper → Blackwell → future),
   fixed thresholds will be wrong. Our eBPF policies can be hot-reloaded without NCCL recompilation.
   The 7.3ms hot-reload + 0.774µs swap time (our measured results) make this practical.

---

## Key References

- arXiv:2507.04786 — "Demystifying NCCL" (HotI 2025): LL/LL128 threshold staleness, GH200 benchmarks
- NSDI 2023 — TACCL paper: AllGather up to 6.7× suboptimal vs topology-aware synthesis
- SIGCOMM 2024 — MCCS paper: 2.4× improvement via policy-driven algorithm selection
- GitHub NVIDIA/nccl#1136: Mixed-CPU deadlock from inconsistent algo/proto
- GitHub NVIDIA/nccl#1362: NVLSTree 37.5× slower than Ring for small messages
- GitHub NVIDIA/nccl#1801: Ring default 19–23% worse than Tree on GB200 3-NVL-partition config
- GitHub NVIDIA/nccl#1047: NVLS 12.7% worse than Ring for ReduceScatter on H100
- GitHub NVIDIA/nccl#1494: Alternating rings bug causing PFC congestion collapse
- GitHub NVIDIA/nccl#1316: AllToAll 33% throughput regression in NCCL 2.19/2.20
- GitHub pytorch/pytorch#117748: 2.7× NVLSTree regression in NCCL 2.19.3
- NVIDIA Blog (Aug 2025): Tuner case study showing bandwidth dips from wrong default transitions
