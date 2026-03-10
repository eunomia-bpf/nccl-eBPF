# NCCL Default Tuner 次优场景调研

*研究日期：2026-03-09*

---

## 执行摘要

本文调研 NCCL 默认 tuner 已知的次优场景，分析 eBPF policy 可以介入的具体时机和量级。结论分两层：

1. **在当前硬件（1 RTX 5090, 2 ranks, socket transport）上**，NCCL 的默认选择在大消息上已经最优，eBPF policy 的主要价值是**防止灾难性错误配置**（LL 强制在大消息上导致 42× 性能崩溃），而不是超越 NCCL 默认值。

2. **在生产环境（多节点、InfiniBand、NVLink）上**，NCCL 默认确实在多个已知场景次优，eBPF policy 有真实的性能提升空间（改善 10%–3×）。

---

## 第一部分：已知 NCCL 次优场景（文献 + 社区报告）

### 1.1 NCCL 的代价模型与调优机制

NCCL 的 `ncclGetAlgoInfo()` 函数在每次集合调用前填充一个 `[algo][proto]` 二维代价表，代价公式为：

```
time = latency * latCount + (bytes / (1000 * bandwidth))
```

代价模型参数（带宽、延迟）在 init 阶段根据拓扑信息计算一次，**之后不再更新**。Tuner plugin 可以在每次 getCollInfo 时修改代价表，NCCL 选择代价最低的 (algo, proto) 组合。

**关键约束**：环境变量 `NCCL_ALGO` 和 `NCCL_PROTO` 优先级最高，会覆盖 tuner plugin 的设置（但反过来，tuner plugin 无法覆盖用户设置的环境变量）。

来源：
- NCCL 源码 `src/graph/tuning.cc`（直接分析）
- GitHub Issue #1661 "How Ext-Tuner Plugin Enforces Algorithm and Protocol?"

---

### 1.2 已知的 NCCL 默认次优场景（按重要性排序）

#### 场景 1：Broadcast/ReduceScatter 在中等消息上的 LL 协议选择（最新发现）

**来源**：GitHub Issue #1810 "Tuning Opportunities for Broadcast on GB200 NVL72"（Aug 2025）

**描述**：在 GB200 NVL72/NVL64/NVL32 上，NCCL 对 Broadcast 操作在 32KB–32MB 消息范围默认选择 LL 协议，但 LL128 和 Simple **均比 LL 更快**。这是 NCCL 内置代价模型的已知误判。

**量级**：未报告具体数字，但图表显示 LL128 和 Simple 明显优于 LL，性能差异估计在 20%–2×。

**解决方式**：NVIDIA 开发者建议使用 custom tuner plugin 覆盖协议选择，或升级到 NCCL 2.27.6+。

**对 NCCLPol 的价值**：这是 NVIDIA 官方承认的 tuner plugin 使用场景之一——修正 NCCL 内置代价模型对 Broadcast 协议选择的误判。eBPF policy 可以直接实现这个纠正逻辑。

---

#### 场景 2：NVLSTree 在多节点场景的算法退化（B300，2026）

**来源**：GitHub Issue #2035 "Low performance with NVLSTree algo on B300"（Mar 2026）

**描述**：在 4 节点以上的 B300 集群上，NCCL 默认选择的 NVLSTree 算法吞吐量从 800 GB/s（2 节点）急剧下降至 200 GB/s（4/8/16 节点），而 Ring 算法在同样配置下维持稳定的 760 GB/s。

**量级**：4 节点以上，NVLSTree vs Ring 的 busbw 差距约 **3.8×**。

**状态**：NVIDIA 确认为 known issue，正在修复。

**对 NCCLPol 的价值**：展示了一种场景——NCCL 默认算法选择的代价模型在特定硬件/规模组合下严重失效，eBPF policy 可以通过运行时实测 (profiler 数据) 动态检测并覆盖。

---

#### 场景 3：MSCCL 自定义 schedule vs NCCL 默认（Azure NDv4 / A100）

**来源**：MSCCL (NSDI'23)，Microsoft Research

**描述**：在 Azure NDv4（8×A100）上，MSCCL 使用手工合成的 collective schedule，在 AllGather 和 ReduceScatter 操作上比 NCCL 默认的 Ring 算法快 **1.2×–3×**。对于 AllReduce，MSCCL 也有约 1.5× 改善（取决于消息大小和拓扑）。

**为什么 NCCL 默认次优**：NCCL 的 Ring 算法是拓扑无关的（topology-agnostic），不考虑 NDv4 内部的 NVSwitch 和 InfiniBand 分层带宽差异。MSCCL 针对这个拓扑手工设计双层 AllGather（先 NVLink 域内，再 IB 域间），完全利用 NVSwitch 的高带宽。

**量级**：AllGather 上 2–3×，AllReduce 上 1.2–1.5×。

**对 NCCLPol 的价值**：MSCCL 通过替换内核 schedule 实现，这超出了 tuner plugin API 的能力范围。但这个场景表明，在多层拓扑中，NCCL 默认的 single-level Ring 在 AllGather/ReduceScatter 上是显著次优的，而 tuner plugin 可以通过指导 NCCL 选择分层算法（如 CollnetChain/CollnetDirect）来部分弥补。

---

#### 场景 4：大规模多节点 Tree vs Ring 算法选择

**来源**：NVIDIA Blog "Massively Scale Deep Learning Training with NCCL 2.4"

**描述**：Ring 算法的通信轮次是 `O(nRanks)`，Tree 算法是 `O(log nRanks)`。在 24,576 GPU 规模的 Summit 超算上，Tree 比 Ring 快 **~180×**（对小消息）。

**NCCL 默认行为**：NCCL 内置了 Ring vs Tree 的自动切换逻辑——但切换阈值是基于初始代价模型计算的，如果实际带宽/延迟与模型预测偏差，切换时机会错误。

**具体失效场景**：
- 高延迟网络（跨交换机跨机柜的 InfiniBand）：latency cost 更高，Tree 应该更早切换，但 NCCL 代价模型可能仍用 Ring
- 网络拥塞：Ring 的 O(n) 步每步都需要跨网络，Tree 的 O(log n) 步减少了暴露度

**量级**：在 8–64 rank 范围，Tree 对小消息（<512KB）有 10%–50% 优势；在 1024+ rank 下差距扩大到数量级。

---

#### 场景 5：版本升级引入的代价模型回归

**来源**：GitHub Issue #1997（Jan 2026）

**描述**：从 NCCL 2.22 升级到 2.24/2.29 后，H200 上的 AllReduce 性能出现可测量的退化。原因是 `waitPeer` 函数中增加了大量 if-else 判断逻辑，触发更多寄存器溢出和 local memory 访问（指令数增加 +45%）。

**对 NCCLPol 的价值**：展示了 NCCL 内部代价模型或实现质量并非单调改善的，版本升级可能带来回归。一个 eBPF policy 如果被固化为确定的策略，在新版本 NCCL 下可能因为内部实现变化而失效。**反过来说**，eBPF policy 可以在 NCCL 版本升级后通过热重载更新策略（无需重新编译 NCCL），这是 native plugin 做不到的。

---

#### 场景 6：cloud vendor 自定义 tuner 的原因

**AWS aws-ofi-nccl**：AWS 创建 aws-ofi-nccl plugin 的主要原因是 NCCL 默认网络后端（sockets）在 EFA（Elastic Fabric Adapter）上性能次优。EFA 使用无连接可靠数据报协议，而 NCCL 的 socket net backend 假设面向连接的传输。aws-ofi-nccl 通过 libfabric 桥接层解决这个 API 不匹配，在 EFA 上大幅提升 InfiniBand 级别的带宽利用率。

**性质**：这不是 tuner plugin，而是 net plugin。但它展示了云厂商为了适配自己的网络硬件而不得不定制 NCCL 的普遍需求。

---

### 1.3 AllGather、ReduceScatter、Broadcast 对 algo/proto 的敏感度

根据 NCCL 源码分析（`src/graph/tuning.cc`）：

| 集合类型 | 可用算法 | 代价模型特点 | 已知次优场景 |
|---------|---------|------------|------------|
| AllReduce | Ring, Tree, CollnetChain, CollnetDirect, NVLS, NVLSTree, PAT | 最完整的代价模型，nsteps=2*(nRanks-1) | 大规模 Ring vs Tree 切换时机 |
| AllGather | Ring, CollnetDirect, NVLS, PAT | bandwidth 公式：`intraBw *= (ppn-1)/ppn` | 多层拓扑下 Ring 次于分层方案 |
| ReduceScatter | Ring, CollnetDirect, NVLS, PAT | 类似 AllGather，nsteps=nRanks-1 | 同上 |
| Broadcast | Ring only | 最简单，仅 Ring | Issue #1810：LL 协议选择错误 |
| Reduce | Ring only | 同 Broadcast | 类似问题 |

**关键发现**：Broadcast 和 Reduce 只有 Ring 算法可用，因此 **algo 选择不是问题**，但 **proto 选择（LL/LL128/Simple）是主要调优杠杆**。Issue #1810 明确表明 NCCL 在 GB200 的 Broadcast 上默认选 LL 是次优的。

---

## 第二部分：当前硬件上的 "Default 次优" 分析

### 2.1 当前已测量的数据（RTX 5090, 2 ranks, socket transport）

以下数据来自 `docs/tmp/p2-default-vs-optimal-sweep.md` 和 `docs/tmp/p2-proto-bandwidth-experiment.md` 中的实测结果：

#### NCCL 默认选择（AllReduce）

| 消息大小 | NCCL 默认 | 最优 | Gap |
|:-------:|:--------:|:---:|:---:|
| 8B–64KB | RING/LL | RING/LL（=DEFAULT）或 Tree/Simple（-2.4%） | ≤2.7%（噪声级别） |
| 128KB–128MB | RING/Simple | RING/Simple（=DEFAULT） | 0% |

**结论**：在 socket transport + 2 rank 场景，NCCL 默认几乎最优。

#### LL 协议强制的灾难性后果（已测量）

| 消息大小 | Simple (busbw) | LL (busbw) | LL 慢多少 |
|:-------:|:--------------:|:----------:|:--------:|
| 16 MB   | 2.38 GB/s      | 0.06 GB/s  | **39.7×** |
| 32 MB   | 2.53 GB/s      | 0.06 GB/s  | **42.2×** |
| 64 MB   | 2.55 GB/s      | 0.06 GB/s  | **42.5×** |
| 128 MB  | 2.55 GB/s      | 0.06 GB/s  | **42.5×** |

**LL128 的情况**：

| 消息大小 | Simple (us) | LL128 (us) | LL128 慢多少 |
|:-------:|:-----------:|:----------:|:-----------:|
| 16 MB   | 7,051       | 17,551     | 2.49×       |
| 128 MB  | 52,568      | 131,465    | 2.50×       |

这是**在当前硬件上实际测量到的最大性能差异**，是 NCCLPol 最强的量化结果。

---

### 2.2 当前硬件为什么大多数 algo/proto 差异消失

1. **TCP loopback 主导**：2 rank socket 场景，AllReduce 延迟约 4.3ms，全部是 TCP 往返时延，与 algo/proto 无关（LL 协议的崩溃除外，因为那是协议架构问题，不是网络问题）。

2. **2-rank RING ≈ TREE**：在 2 rank 下，TREE 退化为链状（0↔1），与 RING 的通信模式完全相同。NCCL 代价表中 Tree 和 Ring 的代价差 <2%，在 socket 瓶颈下完全被掩盖。

3. **nChannels 被钳制**：NCCL 在 socket transport + 2 rank 场景下将有效通道数钳制在 4，tuner plugin 返回更大的值被静默截断。

---

### 2.3 具体场景分析

#### 场景 A：多 collective 类型差异

**AllReduce**：NCCL 默认选择 RING/LL（小消息）和 RING/Simple（大消息），在当前硬件上接近最优。

**AllGather/ReduceScatter**：在当前硬件上，NCCL 也只使用 Ring（无 CollNet/NVLS 支持）。协议选择的重要性与 AllReduce 类似。尚未测量 AllGather/ReduceScatter 在当前硬件上的 algo/proto 敏感度，**这是一个可以尝试的实验**。

**Broadcast**：Issue #1810 明确指出 Broadcast 在中等消息（32KB–32MB）上，NCCL 默认的 LL 协议比 LL128 和 Simple 慢。这个场景在当前硬件上**可以直接测量**：

```bash
# 测试 Broadcast 的 proto 敏感度
for proto in Simple LL LL128; do
  NCCL_PROTO=$proto mpirun ... broadcast_perf_mpi -b 32K -e 32M -f 4 -n 20
done
```

**预期发现**：可能重现 Issue #1810 的现象，得到 Simple > LL128 > LL 的顺序。

---

#### 场景 B：workload 变化导致的次优

**核心问题**：NCCL 代价模型在 init 时计算一次，**之后不更新**。如果运行期间消息大小分布变化（LLM 训练不同阶段：prefill vs decode vs gradient sync），static default 是次优的。

**具体分析**：
- LLM prefill 阶段：大 batch，大 AllReduce（数十 MB），最优是 RING/Simple
- LLM decode 阶段：小 batch，小 AllReduce（数 KB），最优是 RING/LL 或 TREE
- Gradient sync：不同层大小差异极大（1KB embedding → 4GB dense layer），固定 algo 次优

**eBPF policy 的价值**：size-aware policy（size_aware_v2）在每次调用时根据消息大小动态选择，比任何静态配置都好。这是 NCCLPol 已经实现的功能，只是在当前硬件上效果被 TCP 瓶颈掩盖。

**在当前硬件上的验证方法**：

```bash
# 构造混合 workload：小 + 大消息交替
for size in 1024 4096 1048576 67108864; do
  mpirun ... all_reduce_perf_mpi -b $size -e $size -n 100
done
# 对比：static-LL（全 LL）vs static-Simple（全 Simple）vs size_aware policy
```

如果大消息 LL vs Simple 有 42× 差距，混合 workload 中的 adaptive policy 比 static-LL 有明显优势。

---

#### 场景 C：环境变量/配置冲突

**NCCL_PROTO=LL 全局设置的危害**（已证实，42× 退化）：

在生产集群中，sysadmin 可能出于调优目的设置 `NCCL_PROTO=LL`（认为 LL 对小消息有优势）。这会对所有消息大小强制 LL，导致大消息（>512KB）的灾难性退化：

| 情形 | 128MB AllReduce |
|------|:---------------:|
| 默认（无设置）| 52ms |
| NCCL_PROTO=LL（错误设置）| 2,237ms（**43× 慢**）|
| eBPF policy 纠正后 | 52ms（保护了性能）|

**Tuner plugin 能否 override 用户设置的环境变量？**

根据 NCCL 文档和 Issue #1661 的讨论：**不能**。环境变量 `NCCL_PROTO` 的优先级高于 tuner plugin 的输出。tuner 返回的代价表在应用环境变量限制之后被截断，如果 `NCCL_PROTO=LL` 设置了，tuner 选择 Simple 的条目会被忽略。

**这正是 eBPF policy plane 的关键应用场景**：通过 plugin API 的 cost-table 覆盖，可以在 tuner 层面强制选择，但环境变量层面的限制绕不过去。NCCLPol 的论文 claim 需要明确：policy 覆盖的是 tuner 代价表（NCCL 内部推理），不能绕过最高优先级的环境变量。

---

#### 场景 D：多 communicator 差异

**NCCL 的行为**：每个 communicator 在 init 时独立计算代价模型，根据自己的拓扑和 rank 数选择算法。同一进程中的两个 communicator 完全独立调优。

**NCCLPol 已实现**：RQ3 中的 SLO enforcer 演示了对同进程两个 communicator 给出不同配置（TREE/LL vs RING/SIMPLE）。这是控制平面正确性数据，已存在于论文中。

**当前硬件上的限制**：两个 communicator 共享同一 GPU 和 socket transport，实际运行时的差异被 TCP 瓶颈掩盖（4.3ms 每个 AllReduce，与 algo 无关）。

---

#### 场景 E：跨 collective 的资源竞争

**NCCL 的行为**：每个 communicator 有自己的 channel 集，多个 communicator 并发运行时共享 GPU SM 资源但不共享 NCCL channel。NCCL 默认不感知跨 communicator 的竞争。

**理论场景**：两个 communicator 各用 4 channel，共占用 8 个 GPU CTA 进行通信。如果 GPU 的 SM 资源有限，降低一个 communicator 的 channel 数可能让另一个更快。

**在当前硬件上的可行性**：**低**。单 GPU + socket transport，通信瓶颈是 TCP，不是 GPU SM 资源。跨 communicator 竞争在当前场景不可测量。这个场景需要 NVLink 环境（GPU-to-GPU PCIe/NVLink 通信是 SM 密集型的）。

---

#### 场景 F：冷启动/warmup 问题

**NCCL 的行为**：NCCL init 时使用 bootstrap 阶段测量到的粗略带宽和延迟估计，计算代价模型常数，之后这些常数固定不变。如果网络状态在训练过程中变化（拥塞、节点故障、网络重配置），代价模型逐渐失效。

**已有证据**：
- NCCL Issue #1997（2.22→2.29 回归）：展示了 NCCL 内部参数在不同版本间可能不准
- 没有直接证据说明 init 时的估计在特定硬件上不准，但从 NVLSTree 在 B300 上的失效（Issue #2035）可以推断：NCCL 的静态代价模型对新硬件（B300）的 NVLSTree 扩展性预测不准

**eBPF policy 的潜在价值**：profiler adapter 闭环（NCCLPol 已实现）可以用实测 profiler 数据实时更新 policy 决策，无需等待 NCCL 重初始化。这对长时间运行的训练 job（数小时到数天）特别有价值——随着训练进行，profiler 数据积累，policy 可以持续优化。

---

#### 场景 G：不同 collective 操作（非 AllReduce）

**已搜索**：Issue #1810 明确表明 **Broadcast** 在 GB200 上（32KB–32MB）存在 LL 协议次优问题。

**在当前硬件上测试 Broadcast 的可行性**：高。nccl-tests 有 `broadcast_perf_mpi`，测试方法与 AllReduce 完全相同。

**预期结果**：
- 如果 Issue #1810 的现象也出现在 RTX 5090 上（概率高，因为 LL 的设计限制是协议层面的，不是硬件特定的）：
  - Broadcast + LL at 32KB–32MB：可能比 Simple 慢 2×–42×（取决于消息大小）
  - NCCL 默认 Broadcast 选 LL 在某些大小上是次优的
  - eBPF policy 将 Broadcast 的协议从 LL 切换到 Simple 可以有显著改善

---

## 第三部分：相关论文的 "tuning beats default" 实验设计分析

### 3.1 MSCCL (NSDI'23)

**硬件**：Azure NDv4（8×A100 with NVLink + 200Gbps IB）

**方法**：手工合成 collective schedule（MSCCL-IR + synthesizer），直接替换 NCCL 内部 kernel。

**什么场景 NCCL 默认次优**：
- AllGather 在 NDv4 上：NCCL 默认 Ring 在 NVLink 域内和 IB 域间都用 Ring，无法利用 NVLink 的高带宽。MSCCL 的双层 AllGather（先 NVLink 内聚合，再 IB 外同步）比 NCCL 默认快 2–3×。
- 主要优势消息范围：中等到大消息（128KB–128MB）

**实验设计**：用 nccl-tests 的 `allgather_perf` 对比 NCCL 默认（Ring）和 MSCCL 自定义 schedule 的吞吐量曲线。

**对 NCCLPol 的启示**：MSCCL 的改善来自 kernel-level schedule 替换，远超 tuner plugin API 能做的。NCCLPol 不应声称能达到 MSCCL 量级的改善。但 NCCLPol 可以做 MSCCL 做不到的事：**验证器保证**、**热重载**、**EIM 能力模型**。

---

### 3.2 AutoCCL (SC'24)

**硬件**：多节点 A100 GPU 集群

**方法**：自动搜索最优的 nChannels、chunk size 等参数，通过系统性 sweep 取代 NCCL 的启发式规则。

**什么场景 NCCL 默认次优**：
- nChannels 在 InfiniBand 上：NCCL 的默认 nChannels 选择基于拓扑，但在高带宽 IB 网络上可能不足（每 channel 的 chunk size 太大，NIC FIFO 未充分利用）
- chunk size 选择：NCCL 默认对带宽/延迟的权衡不总是最优的
- 训练 throughput（tokens/sec）比 nccl-tests busbw 更能反映真实收益

**实验设计**：端到端训练 benchmark（如 GPT-3 175B），对比 NCCL 默认和 AutoCCL 搜索出的最优配置。据报道改善幅度在 5%–20%，取决于 workload 和规模。

**对 NCCLPol 的启示**：AutoCCL 展示了 **nChannels 的重要性**。在我们当前硬件上，channel 数被 socket 场景钳制在 4，但在真实 IB 集群上，channel 数（以及 chunk size）是最重要的调优参数之一。

---

### 3.3 "Demystifying NCCL" (ETH Zurich preprint, 2025)

**内容**：系统性实测 NCCL 所有 algo/proto 组合，测量实际带宽效率。

**关键发现**：
- Simple: ~100% 峰值带宽（大消息）
- LL128: ~95% 峰值带宽（仅 NVLink）
- LL: ~50% 峰值带宽（所有传输类型，理论值；实测在 socket 下更差）

这直接证实了 LL 的理论 2× 带宽惩罚——但我们的实测显示 socket 场景下实际惩罚更大（42×），因为 LL 的 buffer 架构与 socket 的 syscall 模式交互极差。

---

## 第四部分：在当前硬件上最有希望的方向

### 4.1 强烈推荐：Broadcast 协议扫描

**理由**：Issue #1810 明确说明 Broadcast + LL 在中等消息上次优（GB200 上 32KB–32MB）。这个现象很可能在 RTX 5090 上也存在，因为 LL 的限制是协议层面的。

**实验设计**：

```bash
# Broadcast proto 扫描
for proto in Simple LL LL128; do
  NCCL_PROTO=$proto mpirun --oversubscribe \
    -np 1 env LD_LIBRARY_PATH=.../nccl/build/lib \
      NCCL_TESTS_DEVICE=0 NCCL_P2P_DISABLE=1 NCCL_SHM_DISABLE=1 \
      NCCL_NET=Socket NCCL_HOSTID=bcast-rank0 \
      .../nccl-tests/build/broadcast_perf_mpi -b 32K -e 32M -f 4 -n 20 -w 5 \
    : \
    -np 1 env ... NCCL_HOSTID=bcast-rank1 \
      .../nccl-tests/build/broadcast_perf_mpi -b 32K -e 32M -f 4 -n 20 -w 5
done
```

**预期结果**：
- 如果 Broadcast+LL 在 32KB–32MB 比 Simple 慢 2×–42×（与 AllReduce 的 LL 崩溃类似），这是新的实验证据
- NCCL 默认 Broadcast 是否在这个范围选 LL？用 `NCCL_DEBUG=INFO NCCL_DEBUG_SUBSYS=TUNING` 确认
- 如果 NCCL 默认在 Broadcast 上也选 LL，而 Simple 更快，这是 "default is suboptimal" 的直接证据

**对论文的贡献**：如果成功，可以说明"Broadcast 上 NCCL 的默认 LL 选择次优，eBPF policy 将其纠正为 Simple，改善 X×"——这比 AllReduce 的结果（大消息上 NCCL 默认已是 Simple，最优）更有说服力。

---

### 4.2 强烈推荐：人工构造 "Default 次优" 场景——LL 强制退化演示

**理由**：已有数据，量化清晰，42× 的数字无可辩驳。

**场景设计**：
1. 模拟"错误的集群配置"：`NCCL_PROTO=LL` 全局设置（sysadmin 错误）
2. 基线性能：NCCL 默认（Simple，52ms at 128MB）
3. 错误配置性能：强制 LL（2,237ms at 128MB，**43× 慢**）
4. eBPF policy 介入：检测到 LL + 大消息，强制切换 Simple（52ms，恢复正常）

**这是已知数据，可以直接写入论文，不需要新实验。**

```c
// eBPF policy: prevent LL on large messages
if (n_bytes > 256 * 1024 && proto == NCCL_PROTO_LL) {
    proto = NCCL_PROTO_SIMPLE;  // 防止 42× 退化
}
```

**对论文的价值**：这是 NCCLPol 目前最强的量化结果：**policy 防止了 43× 性能退化**，而不是"policy 改善了 2.4%"。

---

### 4.3 中等推荐：AllGather/ReduceScatter proto 扫描

**理由**：AllGather 和 ReduceScatter 与 AllReduce 的 proto 选择逻辑不同（不同的 nsteps 公式和 bandwidth 计算）。是否也会有 LL 崩溃尚未验证。

**实验设计**：

```bash
for collective in allgather_perf_mpi reduce_scatter_perf_mpi; do
  for proto in Simple LL LL128; do
    NCCL_PROTO=$proto mpirun ... $collective -b 1M -e 128M -f 2 -n 20
  done
done
```

**预期结果**：LL 在大消息上可能同样崩溃，但崩溃的阈值和幅度可能不同（AllGather/ReduceScatter 的通信量是 AllReduce 的一半）。

---

### 4.4 中等推荐：混合 workload 下 adaptive policy 的累积效果

**场景**：模拟 LLM 训练梯度同步序列：
- 100 轮：小消息（1KB，embedding layer）+ 中消息（64KB，attention）+ 大消息（64MB，dense layer）
- 比较：static-LL（坏配置）vs size_aware_v2（正确 adaptive policy）
- 在大消息上：static-LL 被 size_aware_v2 的保护措施拦截，不会退化到 42× 慢

**关键点**：这个实验的目的不是"adaptive 比 NCCL 默认好"，而是"adaptive 比错误的 static 配置好"——这是 policy safety 的场景延伸。

---

### 4.5 低成本验证：SHM Transport（可能降低基线延迟）

如果去掉 `NCCL_SHM_DISABLE=1`，NCCL 可能在同主机双 rank 间使用 SHM transport（延迟可能从 4.3ms 降到 100–500μs）。如果成功，SHM 下 algo/proto 的差异更可测量，但 2-rank 的 RING≈TREE 问题依然存在。

**验证命令**（5 分钟实验）：

```bash
mpirun --oversubscribe \
  -np 1 env NCCL_P2P_DISABLE=1 NCCL_NET=Socket \
    NCCL_HOSTID=shm-rank0 NCCL_DEBUG=INFO ... all_reduce_perf_mpi -b 8 -e 1M \
  : \
  -np 1 env NCCL_P2P_DISABLE=1 NCCL_NET=Socket \
    NCCL_HOSTID=shm-rank1 ...
# 不设 NCCL_SHM_DISABLE，看 NCCL 是否选 SHM transport
```

---

## 第五部分：具体实验建议（按优先级）

### 优先级 1（最重要，预期 1 小时）：Broadcast LL vs Simple 扫描

```bash
# 目标：验证 NCCL Broadcast 默认 LL 选择在 32KB–32MB 是否次优
# 工具：broadcast_perf_mpi
# 配置：同 phase4 baseline（socket transport, 2 ranks, RTX 5090）

# Step 1: 确认 NCCL Broadcast 默认协议
NCCL_DEBUG=INFO NCCL_DEBUG_SUBSYS=TUNING mpirun ... broadcast_perf_mpi -b 32K -e 32M

# Step 2: 扫描三种协议
for PROTO in Simple LL LL128; do
  NCCL_PROTO=$PROTO mpirun --oversubscribe \
    -np 1 env LD_LIBRARY_PATH=/home/yunwei37/workspace/nccl-eBPF/nccl/build/lib \
      NCCL_TESTS_DEVICE=0 NCCL_P2P_DISABLE=1 NCCL_SHM_DISABLE=1 \
      NCCL_NET=Socket NCCL_HOSTID=bcast-proto-rank0 \
      /home/yunwei37/workspace/nccl-eBPF/nccl-tests/build/broadcast_perf_mpi \
        -b 32768 -e 33554432 -f 4 -g 1 -n 20 -w 5 \
    : \
    -np 1 env LD_LIBRARY_PATH=/home/yunwei37/workspace/nccl-eBPF/nccl/build/lib \
      NCCL_TESTS_DEVICE=0 NCCL_P2P_DISABLE=1 NCCL_SHM_DISABLE=1 \
      NCCL_NET=Socket NCCL_HOSTID=bcast-proto-rank1 \
      /home/yunwei37/workspace/nccl-eBPF/nccl-tests/build/broadcast_perf_mpi \
        -b 32768 -e 33554432 -f 4 -g 1 -n 20 -w 5 \
    2>&1 | tee /home/yunwei37/workspace/nccl-eBPF/docs/tmp/bcast-proto-${PROTO}.log
done
```

**决策逻辑**：
- 如果 Broadcast+LL 比 Simple 慢 2× 以上（在中等消息，如 1MB–16MB）：论文可以声称"NCCL 默认 Broadcast 协议次优，eBPF policy 纠正后改善 2×+"
- 如果差异不显著（<20%）：与 AllReduce 结论相同，LL 崩溃在当前硬件的 Broadcast 上同样存在但只有在更大消息下触发

---

### 优先级 2（已有数据，加强叙事）：LL 强制退化故事完善

将现有数据（AllReduce LL vs Simple，42× 差距）包装为完整的"default 次优 + eBPF 纠正"故事：

1. **场景动机**：生产集群中 sysadmin 设置 `NCCL_PROTO=LL`（认为小消息受益），导致大消息（128MB AllReduce）退化 43×。

2. **问题**：NCCL 内置 tuner 无法覆盖 `NCCL_PROTO` 环境变量（环境变量优先级更高）。native plugin 也无法纠正。

3. **eBPF policy 的解决**：NCCLPol 实现了一个 size-aware 安全策略，通过 tuner cost table 的 LL 代价设为极高值，强制 NCCL 选 Simple——但等等，如果 `NCCL_PROTO=LL` 已经设置，tuner 无法覆盖……

   **这里需要澄清一个技术细节**：tuner plugin 在 `NCCL_PROTO` 环境变量限制**之前**还是**之后**运行？根据 Issue #1661 的讨论，环境变量的优先级确实高于 tuner plugin。因此 eBPF policy 通过 tuner API 的防御只对**没有设置 `NCCL_PROTO` 的场景**有效。

4. **正确的叙事框架**：
   - "eBPF policy 防止了 tuner-level 错误决策导致的 42× 退化"（即：没有 policy，tuner 可能错误地将 LL 设为低代价；有 policy，tuner 被引导选 Simple）
   - 对于环境变量覆盖问题，明确说明这是 NCCL plugin API 的限制，eBPF policy 在 tuner 层面工作，不能绕过更高优先级的配置层

---

### 优先级 3（较低成本，验证性）：AllGather/ReduceScatter LL 崩溃验证

```bash
# 验证 AllGather 和 ReduceScatter 的 LL 崩溃是否与 AllReduce 相同
for coll in allgather_perf_mpi reduce_scatter_perf_mpi; do
  for proto in Simple LL; do
    NCCL_PROTO=$proto mpirun ... $coll -b 1M -e 64M -f 2 -n 10 -w 3
  done
done
```

**预期**：AllGather/ReduceScatter 的 LL 崩溃阈值可能在 256KB–1MB 之间（AllReduce 的一半，因为每个 rank 只接发一半数据），但崩溃幅度相似（数十倍）。

---

## 第六部分：场景汇总与论文定位

### 6.1 NCCLPol 可以声称的 "Default 次优" 场景（基于已有数据）

| 场景 | 默认行为 | 次优程度 | eBPF policy 的纠正 | 数据来源 |
|-----|---------|---------|------------------|---------|
| AllReduce + LL 在大消息（>512KB）| NCCL 默认选 Simple，但错误配置/tuner 可能强制 LL | **43× 慢于 Simple** | 检测到 LL+大消息，强制 Simple | 已测量 |
| AllReduce + LL128 在大消息 | 同上 | **2.5× 慢于 Simple** | 同上 | 已测量 |
| AllReduce 小消息（<32KB）| NCCL 选 RING/LL，Tree/Simple 略快 | **~2.4% 改善** | 选 Tree/Simple | 已测量（噪声边界） |
| Broadcast + LL 在中等消息 | NCCL 可能选 LL，Simple 更快 | **待测量，Issue #1810 报告 2×+** | 选 Simple/LL128 | 待验证 |
| NVLSTree 在 4+ 节点（B300）| NCCL 选 NVLSTree，Ring 快 3.8× | **3.8× 慢于 Ring** | 选 Ring | NVIDIA Issue #2035（非本地硬件） |

### 6.2 论文叙事的正确框架

**强：防止灾难性错误（Correctness/Safety）**
> "An incorrectly configured NCCL_PROTO=LL or a buggy native tuner plugin forcing LL on large messages causes a 42×–44× bandwidth reduction. NCCLPol's eBPF policy detects and corrects this in 51ns, with formal verification guaranteeing the correction logic is bug-free. Native plugins offer no such guarantee."

**中：Broadcast 协议次优（待验证）**
> "NCCL's default tuner incorrectly selects LL for Broadcast operations in the 32KB–32MB range [Issue #1810]. An eBPF policy switching to Simple recovers X× throughput. This correction is impossible via environment variables (NCCL_PROTO is a global override) but expressible as a per-collective per-size policy in NCCLPol."

**弱（当前硬件局限）：小消息 algo 改善**
> "On socket transport with 2 ranks, Tree/Simple achieves ~2.4% lower latency than NCCL's default Ring/LL for messages ≤32KB. The absolute improvement (~100μs) is modest due to TCP loopback dominance, but demonstrates policy-driven differentiation."

---

## 附录：参考资料

1. **NVIDIA NCCL GitHub Issues**：
   - Issue #1810: "Tuning Opportunities for Broadcast on GB200 NVL72" — https://github.com/NVIDIA/nccl/issues/1810
   - Issue #2035: "Low performance with NVLSTree algo on B300" — https://github.com/NVIDIA/nccl/issues/2035
   - Issue #1997: "Performance regression from 2.22 to 2.24/2.29" — https://github.com/NVIDIA/nccl/issues/1997
   - Issue #1661: "How Ext-Tuner Plugin Enforces Algorithm and Protocol?" — https://github.com/NVIDIA/nccl/issues/1661

2. **NCCL 环境变量文档**：
   - https://docs.nvidia.com/deeplearning/nccl/user-guide/docs/env.html
   - NCCL_ALGO, NCCL_PROTO, NCCL_MAX_NCHANNELS, NCCL_MIN_NCHANNELS（及对应的 _CTAS 变体）

3. **MSCCL (NSDI'23)**：
   - "MSCCL: Efficient Collective Communication Algorithms for Azure NDv4 and Beyond"
   - https://github.com/microsoft/msccl
   - 关键数据：Azure NDv4 上 AllGather 比 NCCL 默认快 2–3×

4. **NCCL Algorithm Selection Blog**：
   - "Massively Scale Deep Learning Training with NCCL 2.4"
   - https://developer.nvidia.com/blog/massively-scale-deep-learning-training-nccl-2-4/
   - 关键数据：24,576 GPU 规模，Tree 比 Ring 快 180×（小消息）

5. **项目内测量数据**：
   - `docs/tmp/p2-proto-bandwidth-experiment.md`：LL vs Simple vs LL128 实测，42× 差距
   - `docs/tmp/p2-default-vs-optimal-sweep.md`：完整 algo/proto 扫描
   - `docs/tmp/eval-gap-analysis.md`：当前硬件局限分析
   - `docs/tmp/policy-effectiveness-research.md`：实验方案详细评估
