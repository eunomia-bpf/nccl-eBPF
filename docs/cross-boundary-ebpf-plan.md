# Cross-Boundary eBPF for GPU Collective Communication：计划与进度

> 本文档是 cross-boundary eBPF 项目的单一 hub。
> **编辑规则**：
> - 任何 TODO/实验/文档引用条目被取代时，必须至少保留一行并标注状态，不得直接删除。
> - 每个任务做完 → 立即更新本文档（任务条目状态 + 关键数据 + 文档路径）。
> - 每次 context 压缩后 → 完整读取本文档恢复全局状态。
> - 用 sonnet agent 跑任务，不阻塞主对话。
> 上次更新：2026-03-11（方案 A/B 验证完成，统一方案确立）

---

## 1. 论文定位与策略

### 1.1 核心 Thesis

> NCCL 的最优集合通信算法取决于系统级运行时状态的**类型**。
> CPU 饱和时硬件辅助算法（NVLS）最优，cpuset 限制时软件算法（Ring）最优，GPU 热降频时需降低通信激进度。
> **没有一个静态配置在所有运行时条件下都最优。**
>
> Cross-boundary eBPF 将内核级观测（CPU 调度器状态、GPU 热状态）与用户态 NCCL policy 决策统一在同一个 eBPF 框架中。
> 内核 eBPF 提供用户态不可获取的系统级信号，用户态 eBPF（bpftime）在 NCCL 热路径执行经过验证的、可热重载的 policy，两者通过 pinned BPF map 以 <10ns 延迟共享状态。

#### 核心实验数据支撑

| 干扰类型 | Ring | NVLS | 最优选择 |
|---------|------|------|---------|
| 无干扰 baseline | 319.9 GB/s | 212.9 GB/s | Ring |
| CPU 饱和 (stress-ng --cpu 240) | 213.3 GB/s (**-33.3%**) | 231.2 GB/s (免疫) | **NVLS** |
| 核心限制 (taskset 4 cores) | 370.5 GB/s (免疫) | 46.2 GB/s (**-78.3%**) | **Ring** |
| 内存带宽压力 (stress-ng --vm) | 无影响 (-0.2%) | 无影响 | 无需切换 |

GPU 热降频：SM 频率 2032→1650 MHz (SW_THERMAL_SLOWDOWN 0x20)，GPU 3 在 30s 内触发 14 次。

### 1.2 目标会议

Euro-Par 2025/2026, EuroSys, NSDI（12-14 页 full paper，LNCS 格式）

### 1.3 Novelty

1. **Cross-boundary eBPF 架构** — 内核 eBPF（sched_switch + NVML uprobe）+ 用户态 eBPF（bpftime NCCL policy）通过 pinned map 协同
2. **动态最优性差距（Dynamic Optimality Gap）** — 首次量化证明：CPU 干扰类型决定 Ring vs NVLS 的相对优劣，差距高达 78%
3. **GPU 热感知 policy** — 利用 NVML uprobe 检测 thermal throttle 作为 policy 输入
4. **无先例** — 检索确认无 kernel eBPF + userspace eBPF (bpftime) 协同 GPU 通信的已有工作

### 1.4 核心设计约束

1. **不修改 NCCL 源码** — 全部通过 plugin API 和 eBPF 实现
2. **不修改内核** — 使用标准 tracepoint/uprobe，无 kernel patch
3. **内核 observer 由 cluster admin 部署（root），NCCL policy 由 user 运行** — 权限分离
4. 遵循 CLAUDE.md：无 em-dash，"safety" not "correctness"，无 "in-kernel" 描述我们的系统

---

## 2. 已否决的方案

| 方案 | 否决原因 | 状态 |
|------|---------|------|
| tc/XDP 网络观测 | NVLink/RDMA 绕过内核网络栈，tc/XDP 看不到 GPU 通信 | ❌ 否决 |
| PCIe 带宽观测 | B300 NVLink 全互联，GPU 通信不走 PCIe | ❌ 否决 |
| sched_ext 双向调度控制 | 需 Linux 6.12+，当前内核 6.8 | ❌ future work |
| uprobe + sched_switch 单纯观测 | 用户态只需读 map + if/else，不需要用户态 eBPF | ❌ 否决 |
| 2+2 GPU 并发（无 CPU 限制） | NVLink 链路天然隔离，<2% 噪声范围内无干扰 | ❌ 验证失败 |

---

## 3. 统一方案架构

```
┌─────────────────────────────────────────────────────────────┐
│ Kernel Space                                                 │
│                                                               │
│  sched_switch TP ──────┐   uprobe: nvmlGetClockInfo ────┐   │
│  (CPU contention type)  │   (GPU throttle/thermal)       │   │
│                         ▼                                 ▼   │
│              ┌────────────────────────────────────────┐       │
│              │ Pinned BPF Maps (BPF_F_MMAPABLE)       │       │
│              │                                         │       │
│              │ map 1: cpu_state {type, runqueue_len}   │       │
│              │ map 2: gpu_state {throttle, margin_temp}│       │
│              └──────────────┬─────────────────────────┘       │
├─────────────────────────────┼─────────────────────────────────┤
│ User Space                  │                                  │
│              ┌──────────────▼──────────────────┐              │
│              │ bpftime kernel-user array maps   │              │
│              │ (mmap zero-copy, <10ns read)     │              │
│              └──────────────┬──────────────────┘              │
│                             │                                  │
│  ┌──────────────────────────▼────────────────────────────┐   │
│  │ NCCL Tuner eBPF Policy (bpftime JIT, ~100ns)          │   │
│  │                                                        │   │
│  │  signal 1: cpu_state (from kernel sched eBPF)         │   │
│  │  signal 2: gpu_state (from kernel uprobe eBPF)        │   │
│  │  signal 3: profiler telemetry (from userspace map)    │   │
│  │  signal 4: nccl_policy_ctx (msg size, ranks, etc.)    │   │
│  │  → output: algo/proto/channels                        │   │
│  └────────────────────────────────────────────────────────┘   │
│                                                                │
│  NCCL Process (unchanged)                                     │
└────────────────────────────────────────────────────────────────┘
```

Policy 逻辑：
```
if (cpu_contention_type == SATURATION && msg_size in [4M, 128M]):
    → NVLS (hardware multicast, CPU-independent)
elif (cpu_contention_type == CPUSET_LIMITED && msg_size in [4M, 128M]):
    → Ring (fewer thread dependencies)
elif (gpu_throttle_active):
    → reduce aggressiveness (fewer channels, lower bandwidth pressure)
else:
    → size-aware default (Ring/LL128 for 4-32M, Ring/Simple for 64-192M)
```

---

## 4. 环境与验证数据

### 4.1 硬件环境

- 8x NVIDIA B300 SXM6 (Blackwell), NVLink 5 (NV18, 1.8 TB/s/GPU)
- 240-core AMD EPYC 9575F, 2.1 TB RAM
- Linux 6.8, CUDA 13.0, NCCL 2.29, 驱动 580.95.05
- 无 InfiniBand/RoCE，单节点，virtio-net 虚拟机

### 4.2 内核 BPF 能力

- `CONFIG_BPF_LSM=y`, `CONFIG_CGROUP_BPF=y`, `CONFIG_BPF_STREAM_PARSER=y`
- BTF + bpftool 7.4，uprobe_register 在 kallsyms
- LSM BPF 未激活（需 boot cmdline），perf_event_paranoid=4（需 root）
- 无 AMD Uncore PMU（虚拟机限制）

### 4.3 bpftime kernel-user map

- `array_map_kernel_user`: BPF_F_MMAPABLE → mmap 零拷贝 <10ns
- `hash_map_kernel_user`: syscall 路径 ~1-5us
- `KERNEL_USER_MAP_OFFSET = 1000`，类型 1001/1002
- plugin.cpp 需修改以支持（设计方案已完成）

### 4.4 NVML 可观测指标

| 指标 | idle | NCCL 高负载 | NVML API | 可 uprobe |
|------|------|------------|---------|----------|
| SM Clock | 2032 MHz | 1650-1972 MHz | `nvmlDeviceGetClockInfo` @ 0x5c980 | ✅ |
| Throttle Reason | 0x0 | 0x20 (SW_THERMAL) | `nvmlDeviceGetCurrentClocksThrottleReasons` | ✅ |
| Margin Temp | 55°C | 31°C | `nvmlDeviceGetMarginTemperature` | ✅ |
| Power | 190W | 500-1054W | `nvmlDeviceGetPowerUsage` | ✅ |
| NVLink Error | 0 (healthy) | — | `nvmlDeviceGetNvLinkErrorCounter` @ 0x5c980 | ✅ |
| NVLink Utilization | — | — | `nvmlDeviceGetNvLinkUtilizationCounter` | ❌ Not Supported on B300 |
| NVLink TX/RX | 0 | ~76-81 GB/s | DCGM fields 1011/1012 | N/A (DCGM only) |

---

## 5. 文档索引

| 文档 | 路径 | 状态 |
|------|------|:---:|
| **本文档（唯一 hub）** | `docs/cross-boundary-ebpf-plan.md` | 活跃 |
| Euro-Par 论文规划 | `docs/europar-paper-plan.md` | ✅ |
| CPU 干扰实验总结 | `docs/tmp/interference-experiment-summary.md` | ✅ |
| 多 job 干扰总结 | `docs/tmp/multijob-interference-summary.md` | ✅ |
| NVML 指标分析 | `docs/tmp/plan-b-nvml-analysis.md` | ✅ |
| plugin.cpp 改造设计 | `docs/tmp/plugin-kernel-map-design.md` | ✅ |
| B300 评估计划 | `docs/eval-plan-b300.md` | ✅（Steps 0-6 完成）|
| 当前 workshop paper | `docs/paper/paper.tex` | ✅ |
| 内核 observer 源码 | `src/kernel_observer/` | ✅ 已编译 |
| 用户态 eBPF policies | `src/ebpf-policies/` | ✅ |
| NCCL tuner plugin | `src/nccl-policy-plugin/plugin.cpp` | 待修改 |

---

## 6. 任务追踪

> **规则**：
> - 所有重要数据和文档路径只在本列表维护，不在别处重复。
> - 每次执行 agent 都必须输出文档到 `docs/tmp/`，并在对应条目记录路径和关键数据。
> - 条目被取代时保留一行标注状态，不得删除。

### Phase 0: 环境调研与方案验证 ✅

| # | 任务 | 状态 | 关键数据 / 文档 |
|---|------|:---:|------|
| 1 | 硬件/网络/BPF 环境调研 | ✅ | 8x B300 NVLink, 240-core EPYC, Linux 6.8, 无 IB |
| 2 | bpftime kernel-user map 调研 | ✅ | mmap <10ns, KERNEL_USER_MAP_OFFSET=1000 |
| 3 | ~~tc/XDP 网络观测调研~~ | ❌ 否决 | NVLink 绕过内核网络栈 |
| 4 | ~~2+2 GPU 并发干扰测试~~ | ❌ 无干扰 | <2% 噪声范围，NVLink 链路天然隔离 |
| 5 | CPU 饱和 vs cpuset 干扰实验 | ✅ | Ring -33.3% (CPU sat), NVLS -78.3% (cpuset)。`docs/tmp/interference-experiment-summary.md` |
| 6 | 内存带宽压力实验 | ✅ | 无影响 (-0.2%) |
| 7 | NVML 符号可 uprobe 验证 | ✅ | 14+ 符号全部导出（类型 T），偏移量已知 |
| 8 | NVML 运行时指标采集 | ✅ | SM 2032→1650 MHz, throttle 0x20, margin 55→31°C。`docs/tmp/plan-b-nvml-analysis.md` |
| 9 | cgroup/LSM/perf_event 环境验证 | ✅ | cgroup v2, CGROUP_BPF=y, LSM BPF 未激活 |

### Phase 1: PoC 实现（当前 phase）

| # | 任务 | 状态 | 关键数据 / 文档 |
|---|------|:---:|------|
| 10 | 内核 sched_switch eBPF 程序 | ✅ | 编译通过，零 warning。`src/kernel_observer/nccl_cpu_observer.bpf.c` |
| 11 | 内核 observer 加载脚本 | ✅ | `src/kernel_observer/load.sh` |
| 12 | plugin.cpp kernel-user map 改造设计 | ✅ | 约 130 行新代码。`docs/tmp/plugin-kernel-map-design.md` |
| 13 | plugin.cpp kernel-user map 实现 | ❌ | 实现 `attach_kernel_maps()`，env var `NCCL_POLICY_KERNEL_MAPS` |
| 14 | 用户态 cpu_aware policy 编写 | ❌ | 读取 cpu_state map + size-aware 逻辑 |
| 15 | 端到端验证：sched_switch → map → policy → NCCL | ❌ | stress-ng 注入 → 内核检测 → policy 切换 → 性能改善 |

### Phase 2: GPU 热感知扩展

| # | 任务 | 状态 | 关键数据 / 文档 |
|---|------|:---:|------|
| 20 | 内核 uprobe on NVML（throttle + clock） | ❌ | attach `nvmlDeviceGetCurrentClocksThrottleReasons` |
| 21 | 扩展 policy：综合 CPU + GPU 信号 | ❌ | |
| 22 | 持续负载下 GPU 热感知端到端验证 | ❌ | |

### Phase 3: 全量实验

| # | 任务 | 状态 | 关键数据 / 文档 |
|---|------|:---:|------|
| 30 | 默认 NCCL vs 静态最优 vs cross-boundary policy 对比 | ❌ | |
| 31 | 动态切换实验：运行中注入/撤除 CPU 压力 | ❌ | |
| 32 | kernel-user map 读取延迟开销测量 | ❌ | |
| 33 | 多信号组合实验（CPU + GPU 同时干扰） | ❌ | |

### Phase 4: 论文

| # | 任务 | 状态 | 关键数据 / 文档 |
|---|------|:---:|------|
| 40 | Euro-Par 论文结构规划 | ✅ | `docs/europar-paper-plan.md` |
| 41 | LNCS 格式模板搭建 | ❌ | |
| 42 | 论文 draft（从 workshop paper 扩展） | ❌ | |
| 43 | Figures（架构图、实验图） | ❌ | |

---

## 7. 风险评估

| 风险 | 影响 | 缓解 | 状态 |
|------|------|------|:---:|
| stress-ng 不代表生产 | 审稿质疑 | stress-ng = 多租户争用; taskset = K8s cpuset cgroup | 已准备回应 |
| GPU throttle 信号稀疏 | 方案 B 弱化 | GPU 3 在 30s 触发 14 次已足够 | 已验证 |
| plugin.cpp 改造阻塞 | Phase 1 延迟 | 设计已完成，约 130 行代码 | 待实现 |
| 内核 eBPF 需 root | 部署受限 | 权限分离设计：admin 部署 observer，user 运行 policy | 设计已定 |
| "只是读 map if/else" | novelty 质疑 | 综合 3+ 信号 + per-collective + 热重载 + 验证 | 已准备回应 |
| 240 核 CPU 争用难产生 | 方案 A 弱化 | stress-ng/taskset 已证明可制造可测量差异 | ✅ 已验证 |
| NVML 符号变化 | uprobe 跨版本 break | 当前 580.95.05 已验证，论文讨论版本依赖 | 已验证 |

---

## 8. 关键决策记录

| 决策 | 结论 | 原因 | 日期 |
|------|------|------|------|
| 目标：跨边界 eBPF | ✅ | 提升 novelty，区别于纯用户态 NCCLbpf | 2026-03-11 |
| ~~tc/XDP 网络观测~~ | ❌ 否决 | NVLink 绕过内核网络栈 | 2026-03-11 |
| ~~uprobe + sched 单纯观测~~ | ❌ 否决 | 用户态不需要 eBPF，普通 C 就能做 | 2026-03-11 |
| ~~PCIe 带宽观测~~ | ❌ 否决 | B300 NVLink 全互联不走 PCIe | 2026-03-11 |
| 方案 A: CPU 干扰类型感知 | ✅ 采纳 | 实验证明 Ring/NVLS 对不同干扰类型敏感度相反 | 2026-03-11 |
| 方案 B: GPU 热感知 | ✅ 采纳 | SM 频率降 19%，margin temp 从 55→31°C | 2026-03-11 |
| 方案 C: cgroup 多租户 | 暂缓 | 核心可行但 LSM 需重启，优先做 A+B | 2026-03-11 |
| A+B 合并为统一方案 | ✅ | 两个内核信号源 + 一个用户态 policy = 更强叙事 | 2026-03-11 |
| sched_ext 双向控制 | future work | 需 Linux 6.12+ | 2026-03-11 |
| 论文格式 LNCS | ✅ | Euro-Par 要求 Springer LNCS | 2026-03-11 |
