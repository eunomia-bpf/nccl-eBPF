# Cross-Boundary eBPF for GPU Collective Communication

## 核心论点

eBPF 是唯一能跨越内核-用户空间边界、用统一语言和共享状态（pinned maps）实现端到端 policy 的机制。当前 NCCLbpf 只用了用户态一半；将内核态 eBPF 观测与用户态 NCCL policy 打通，实现**跨边界的集合通信治理**，是质的飞跃。

## 目标会议

Euro-Par 2025/2026, EuroSys, NSDI（12-14 页 full paper）

## 研究问题

**NCCL 的算法选择不考虑系统级运行时状态**：NCCL 在 communicator 创建时基于 GPU 拓扑选定 algorithm/protocol/channels，假设系统资源充足且独占。但在多租户 GPU 集群中，CPU 争用、NVLink 链路退化、多 job 干扰等系统级事件会影响不同算法的相对性能。NCCL tuner plugin 看不到这些跨进程/内核级信息。

**Cross-boundary eBPF** 解决这个问题：内核 eBPF（tracepoint/uprobe/perf_event）实时观测系统级状态，通过 pinned BPF map 将信息传递给 NCCL 用户态 eBPF policy，policy 据此动态调整集合通信参数。用户态 eBPF 也可写回 pinned map，实现进程间协调或向内核传递 NCCL 内部状态。

---

## 已否决的方案

### ❌ tc/XDP 网络观测（不适用于 NVLink 场景）
- **原因**：NVLink 和 RDMA 完全绕过内核网络栈，tc/XDP 看不到任何 GPU 通信流量
- 强制 socket transport（`P2P_DISABLE=1`）是人为构造，不代表生产环境
- **结论**：在 NVLink 8-GPU 单节点上没有真实意义，仅可作为跨边界 map 数据流通的最小化 PoC

### ❌ PCIe 带宽观测
- **原因**：B300 NVLink 5（NV18）全互联，GPU 通信不走 PCIe

### ❌ sched_ext 双向调度控制
- **原因**：需要 Linux 6.12+，当前内核 6.8 不支持
- **结论**：记为 future work

### ❌ uprobe + sched_switch 单纯观测 CPU 竞争
- **原因**：内核写 map，用户态只需读一个值做 if/else，不需要用户态 eBPF，普通 C 就能做
- **结论**：观测本身有价值，但不构成"cross-boundary eBPF"的 novelty

---

## 候选方案（按优先级排序）

### 方案 A: 多 NCCL 进程时序协调（优先级最高）

**场景**：两个 job 共享 8-GPU 节点（A 占 GPU 0-3，B 占 GPU 4-7），需要协调避免 CPU/内存带宽争用。

**内核 eBPF（sched_switch tracepoint）**：
- 跨进程全局视角：观测两个 job 的 NCCL proxy thread 是否同时处于 RUNNING 状态
- 这是用户态进程**看不到的信息**（进程 A 无法知道进程 B 的线程调度状态）
- 维护滑动窗口：最近 N ms 内的跨进程冲突事件计数
- 写入 pinned BPF_F_MMAPABLE array map

**用户态 eBPF（每个 NCCL 进程内，bpftime）**：
- 从 kernel-user shared map 读取全局竞争状态
- 从本进程 profiler telemetry map 读取 per-collective 延迟
- **综合两个来源做联合决策**：如果检测到跨进程冲突 + 自身延迟上升 → 降低 aggressiveness（减少 channel，或切换到硬件辅助算法 NVLS）
- **写回** pinned map：发布"我正在做 8GB AllReduce"，供其他进程读取（进程间协调）

**为什么双侧 eBPF 不可替代**：
- 内核侧：sched_switch tracepoint 只有内核 eBPF 能挂，提供跨进程调度视角
- 用户态侧：决策逻辑在 NCCL 热路径（每次 getCollInfo）执行，综合多 map 查找 + 状态机 + 写回协调 map；需要热重载（运行时更新协调策略）、验证（保证不崩溃 NCCL 进程）、沙箱隔离
- 双向数据流：内核→用户态（竞争状态），用户态→用户态（进程间协调通过 kernel pinned map 中转）

**关键风险**：B300 有 240 核 AMD EPYC，CPU 争用可能不显著。需 Phase 0 验证。

**验证实验**：
```bash
# 同时跑两个 4-GPU NCCL job，观测是否有性能干扰
# Job A: GPU 0-3
CUDA_VISIBLE_DEVICES=0,1,2,3 mpirun -np 4 all_reduce_perf -b 4M -e 128M -f 2 -n 50 &
# Job B: GPU 4-7
CUDA_VISIBLE_DEVICES=4,5,6,7 mpirun -np 4 all_reduce_perf -b 4M -e 128M -f 2 -n 50 &
wait
# 对比单独运行时的性能
```

---

### 方案 B: NVLink 错误率感知弹性 policy

**场景**：NVLink 链路在长时间高负载下可能出现 CRC/replay 错误率上升，这是硬件退化的早期信号。

**内核 eBPF（uprobe on libnvidia-ml.so）**：
- 对 `nvmlDeviceGetNvLinkErrorCounter` 挂内核 uprobe
- 内核 uprobe 能跨进程 attach（bpftime uprobe 只能同进程），可以截获任何调用 NVML 的进程（包括 `nvidia-smi`、DCGM、监控守护进程）的 NVLink 错误读取
- 提取 NVLink CRC error count / replay error count，写入 pinned map

**用户态 eBPF（NCCL tuner policy）**：
- 从 kernel-user shared map 读取 NVLink 错误率
- 结合 NCCL profiler telemetry（当前 collective 延迟趋势）
- 当某条链路错误率上升时，切换算法以回避该链路（如 Ring → Tree），或降低 aggressiveness

**为什么双侧 eBPF 不可替代**：
- 内核侧：只有内核 uprobe 能跨进程 attach 到 NVML 库调用
- 用户态侧：NVLink 错误率是缓慢变化的异步信号，需结合 NCCL 内部的快速同步决策（per-collective）

**关键风险**：NVML 是闭源的，符号可能被 strip 或混淆。需先验证 `nm -D libnvidia-ml.so | grep nvmlDeviceGetNvLinkErrorCounter`。

**验证实验**：
```bash
# 检查 NVML 符号是否可被 uprobe
nm -D /usr/lib/x86_64-linux-gnu/libnvidia-ml.so.1 | grep -i nvlink
# 读取当前 NVLink 错误计数
nvidia-smi nvlink -e
```

---

### 方案 C: cgroup 多租户 CPU 感知

**场景**：Kubernetes 多租户环境，多个 NCCL job 在不同 cgroup 中运行，内核感知各 job 的 CPU 饥饿程度。

**内核 eBPF（perf_event + sched tracepoint）**：
- 按 cgroup 采集各 job 的 CPU 运行队列等待时间、上下文切换频率
- 写入 pinned map：`{job_id, cpu_stall_us, context_switches}`

**用户态 eBPF（NCCL tuner policy）**：
- 读取自己 job 的 CPU 压力状态（从 kernel-user shared map）
- 读取 NCCL profiler telemetry（本进程数据）
- CPU 饥饿严重时减少 nChannels（降低 proxy thread 负担），CPU 充裕时增加 channels

**为什么双侧 eBPF 不可替代**：
- 内核侧：跨 cgroup 的 CPU 统计只有内核能提供
- 用户态侧：per-collective 决策 + 热重载 + 验证

**额外能力（需 LSM BPF）**：
- 内核 LSM eBPF 可控制哪些 NCCL policy 允许加载（多租户安全）
- 但需要 boot cmdline 添加 `lsm=...,bpf`，当前未启用

**关键风险**：与方案 A 类似，240 核 CPU 可能争用不显著。cgroup 隔离需要额外的容器化设置。

---

## Phase 0: 环境调研 ✅

### 已完成

1. **硬件**：8x NVIDIA B300 SXM6 (Blackwell), NVLink 5 (NV18, 1.8 TB/s/GPU), 240-core AMD EPYC 9575F
2. **无 InfiniBand / RoCE** — 单网卡 virtio-net 虚拟机（DataCrunch），只有 TCP
3. **无多节点** — 单机 8 GPU，无集群
4. **内核 6.8 + BTF + bpftool 7.4** — eBPF 开发环境完备
5. **bpftime 支持 kernel map 共享**：
   - `array_map_kernel_user`: 若内核 map 设 `BPF_F_MMAPABLE`，mmap 零拷贝读取 <10ns
   - `hash_map_kernel_user`: syscall 路径，~1-5us
6. **getCollInfo 每次 collective call 都调用** — 在 `enqueue.cc:2054`，热路径，policy 可实时生效
7. **NCCL 算法特性**（B300 实测）：Ring 在 4-128M 比 NVLS 快 5.5-26.5%
8. **内核 BPF 能力**：`CONFIG_BPF_LSM=y`, `CONFIG_CGROUP_BPF=y`, `CONFIG_BPF_STREAM_PARSER=y`
9. **LSM BPF 未激活**：`/sys/kernel/security/lsm` 不含 `bpf`，需修改 boot cmdline
10. **无 AMD Uncore PMU**：内存带宽计数器不可用（虚拟机限制）

### 方案 A 验证结果 ✅✅ （重大发现）

**第一轮（2+2 GPU 并发）**：无干扰（<2%），因为 2-GPU pair 用不同 NVLink 链路

**第二轮（CPU 压力实验）— 核心发现：算法选择取决于 CPU 竞争类型**

| 干扰类型 | Ring | NVLS | 最优选择 |
|---------|------|------|---------|
| 无干扰 baseline | 319.9 GB/s | 212.9 GB/s | Ring |
| CPU 饱和 (stress-ng --cpu 240) | 213.3 GB/s (**-33.3%**) | 231.2 GB/s (免疫) | **NVLS** |
| 核心限制 (taskset 4 cores) | 370.5 GB/s (免疫) | 46.2 GB/s (**-78.3%**) | **Ring** |
| 内存带宽压力 (stress-ng --vm) | 无影响 (-0.2%) | 无影响 | 无需切换 |
| 两个 2-GPU job 共享 4 核 | ~63 GB/s | — | 严重退化 + 6s 级 stall |

**关键洞察**：
- **CPU 饱和**（runqueue 争用）→ Ring 的 proxy thread 被频繁抢占 → 应选 NVLS（硬件 multicast）
- **cpuset 限制**（核心数不足）→ NVLS 的同步线程缺核 → 应选 Ring（更少线程依赖）
- **没有一个算法在所有干扰类型下都最优** — 必须根据干扰类型动态切换
- 这正是 cross-boundary eBPF 的完美场景：内核 sched_switch 能区分两种干扰，用户态 eBPF 做算法切换

详细日志：`docs/tmp/interference-experiment-summary.md`

### 方案 B 验证结果 ✅✅ （重大发现）

**NVML 符号可访问性**：
- [x] NVML 库：`/usr/lib/x86_64-linux-gnu/libnvidia-ml.so.580.95.05`
- [x] 所有 NVLink 符号均已导出（类型 `T`，可 uprobe），14+ 个函数
- [x] uprobe 内核支持就位（`uprobe_register` 在 kallsyms 中存在）
- [x] DCGM 已安装（`/usr/bin/dcgmi`），发现全部 8 GPU

**NCCL 运行时可观测的动态指标**：

| 指标 | idle | NCCL 高负载 | 可操作性 |
|------|------|------------|---------|
| SM Clock | 2032 MHz | **1650-1972 MHz** (throttle) | 高：频率下降 = 性能退化信号 |
| Throttle Reason | 0x0 | **0x20** (SW_THERMAL_SLOWDOWN) | 高：GPU 3 在 30s 内触发 14 次 |
| Margin Temperature | 55°C | **31°C** (距热限制) | 高：leading indicator |
| Power Draw | 190W | **500-1054W** (峰值 959W) | 中：变化快但不直接影响算法选择 |
| NVLink TX/RX (DCGM) | 0 | ~76-81 GB/s aggregate | 中：可检测带宽不均衡 |

**不可用的指标**：
- `nvmlDeviceGetNvLinkUtilizationCounter` → B300 上返回 "Not Supported"
- `hw_thermal_slowdown` (0x40) / `hw_power_brake` (0x80) → 未触发（B300 用 SW 路径）
- `pstate` → 始终 P0

**关键洞察**：
- GPU 3 在持续高负载下频繁触发 SW_THERMAL_SLOWDOWN，频率降 19%
- Margin Temperature 是**先行指标**（在 throttle 发生前下降）
- **policy 机会**：检测到 throttle → 降低 aggressiveness（减少 channel/切换算法），让 GPU 冷却

详细日志：`docs/tmp/plan-b-nvml-analysis.md`

### 方案 C 验证结果 ✅

- [x] cgroup v2 纯模式（`cgroup2 on /sys/fs/cgroup`）
- [x] `CONFIG_CGROUP_BPF=y`，所有 cgroup BPF 程序类型可用
- [x] perf_event 可用：7 个硬件计数器 + 12 个软件计数器
- [x] BPF 程序类型全部可用：perf_event, cgroup_skb, cgroup_sock, lsm, kprobe, tracepoint 等
- [ ] LSM BPF **未在运行时激活**（需 boot cmdline `lsm=...,bpf`，需重启）
- [ ] `perf_event_paranoid=4`（需 root 或调低至 <=1）
- **结论**：核心路径可行，LSM 部分需重启激活

---

## 综合发现与统一方案

方案 A 和 B 的发现可以**合并为一个统一的 cross-boundary eBPF 系统**：

### 内核 eBPF 侧（提供用户态不可获取的信息）

1. **sched_switch tracepoint**：区分 CPU 饱和（runqueue 长）vs cpuset 限制（可用核少）
   - CPU 饱和 → NVLS 更优（免疫）
   - cpuset 限制 → Ring 更优（免疫）

2. **uprobe on NVML**（`nvmlDeviceGetClockInfo` / `nvmlDeviceGetCurrentClocksThrottleReasons`）：
   - 检测 SW_THERMAL_SLOWDOWN（0x20）
   - 检测 SM 频率下降（1650 vs 2032 MHz）
   - 提前感知 margin temperature 下降

### 用户态 eBPF 侧（NCCL tuner policy，bpftime）

综合多个信号做 per-collective 决策：
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

用户态 eBPF 不可替代的原因：
- 综合 3+ 个 map 的信号（CPU 竞争 + GPU 热状态 + NCCL profiler telemetry）
- per-collective 热路径执行（每次 getCollInfo，~100ns）
- 热重载（运行时更新 policy 逻辑）
- eBPF 验证器保证不崩溃 NCCL 进程

### 共享 Map 架构

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

---

## 执行计划（更新后）

### Phase 1: PoC 实现（优先）

1. **内核 sched_switch eBPF**：写 pinned map 区分 CPU 竞争类型
2. **修改 plugin.cpp**：支持 kernel-user map 注册
3. **用户态 cpu_aware policy**：读取 cpu_state map + size-aware 逻辑
4. **端到端验证**：stress-ng → 内核检测 → map 更新 → policy 切换 → NCCL 性能改善

### Phase 2: GPU 热感知扩展

5. **内核 uprobe on NVML**：捕获 throttle reason + SM clock
6. **扩展 policy**：综合 CPU + GPU 信号

### Phase 3: 全量实验

7. 对比：默认 NCCL vs 静态最优 vs cross-boundary eBPF policy
8. 动态切换实验：运行中注入/撤除 CPU 压力
9. 开销测量：kernel-user map 读取延迟

---

## 风险评估（更新后）

| 风险 | 影响 | 缓解 |
|------|------|------|
| stress-ng 制造的竞争不代表生产 | 审稿质疑实验真实性 | 论文说明：stress-ng 模拟多租户 CPU 争用；taskset 模拟 K8s cpuset cgroup |
| GPU throttle 不够频繁 | 方案 B 信号稀疏 | 长时间持续负载可稳定触发；GPU 3 在 30s 内触发 14 次已足够 |
| plugin.cpp 不支持 kernel-user map | 所有方案阻塞 | 需修改 plugin.cpp（工作量有限，已评估）|
| 内核 eBPF 需要 root | 部署受限 | 论文叙事：kernel observer 由 cluster admin 部署，policy 由 user 运行 |
| 用户态 eBPF "只是读 map 做 if/else" | 审稿质疑 novelty | 综合 3+ 信号 + per-collective 决策 + 热重载 + 验证保证 ≠ 简单 if/else |

---

## 论文叙事

> **核心发现**：GPU 集合通信的最优算法取决于系统级运行时状态的类型——CPU 饱和时硬件辅助算法（NVLS）最优，cpuset 限制时软件算法（Ring）最优，GPU 热降频时需降低通信激进度。没有一个静态配置在所有运行时条件下都最优。
>
> **解决方案**：Cross-boundary eBPF 将内核级观测（CPU 调度器状态、GPU 热状态）与用户态 NCCL policy 决策统一在同一个 eBPF 框架中。内核 eBPF 提供用户态不可获取的系统级信号，用户态 eBPF 在 NCCL 热路径执行经过验证的、可热重载的 policy，两者通过 pinned BPF map 以 <10ns 延迟共享状态。

**无先例**：检索确认无 kernel eBPF + userspace eBPF (bpftime) 协同 GPU 通信的已有工作。
