# NCCLPol Policy Effectiveness Research

*分析日期：2026-03-09*

---

## 第一部分：NCCL 调优机制深度分析

### 1.1 NCCL 的 algo/proto/channel 选择机制

NCCL 的集合通信算法选择发生在 `ncclGetAlgoInfo()` 函数中，流程如下：

1. **初始化阶段（`init`）**：NCCL 根据拓扑信息（NVLink 域、NIC 带宽、节点数）计算 `tunerConstants`（基础延迟、每通道最大带宽等）。tuner plugin 的 `init` 回调在这一步之后、`ncclTopoTuneModel` 之前运行，因此 v5 plugin 可以扰动 NCCL 的内部默认常数。

2. **每次集合调用阶段（`getCollInfo`）**：NCCL 先填充一个 `[algo][proto]` 二维代价表，然后将其传给 tuner plugin，plugin 可以修改代价表条目，NCCL 再根据最低代价选出最终的 (algo, proto) 和 nChannels。

**关键机制细节（直接影响 policy 有效性）：**

- **代价表条目为 -1（`NCCL_ALGO_PROTO_IGNORE`）= 硬性拒绝**：如果 NCCL 检测到某个 (algo, proto) 组合在当前拓扑不可用（比如 CollNet 在 socket transport 下不支持，或 NVLS 在非 NVLink 系统上不支持），会把对应条目设为 -1。plugin 不能通过修改这些条目来启用不支持的功能。

- **Plugin 选择特定组合的方式**：将目标 (algo, proto) 的代价表条目设为 0.0，同时保持其他条目不变（或设为更高值），NCCL 就会选择代价最低的那个。

- **nChannels 的作用**：`getCollInfo` 的 `nChannels` 输出是"允许 NCCL 使用的最大通道数"，但 NCCL 内部还会根据消息大小和拓扑约束进行二次截断。从 `profiler-adapter-results.md` 可以看到：当 plugin 返回 `channels=9` 或 `channels=10` 时，NCCL 的实际运行日志仍显示 `channel{Lo..Hi}={0..3}`（即 4 个通道），说明 NCCL 在 socket transport + 2 rank 场景下将有效通道数钳制在 4。

### 1.2 tuner 返回的 algo/proto/nChannels 何时会被 NCCL 忽略？

基于代码分析和实测结果，总结以下情况：

| 场景 | 描述 | 后果 |
|------|------|------|
| 目标 (algo, proto) 代价为 -1 | 对应功能在当前拓扑不支持（CollNet、NVLS 等） | 强制忽略，NCCL 选 -1 以外代价最低的 |
| nChannels 超出拓扑上限 | 超过 NCCL 内部计算的最大通道数（受 NIC 数量、ring/tree 结构约束） | 静默截断到拓扑上限 |
| nChannels 返回 0 或负值 | 无效值 | NCCL 使用其默认值 |
| NCCL_ALGO 环境变量限制 | 用 `NCCL_ALGO=ring` 排除了 TREE | plugin 设置 TREE 的代价也无效，NCCL 仍用 RING |
| LL/LL128 在 socket transport 下带宽限制 | LL 协议仅能使用约 50% 峰值带宽（需 atomic 4B store，不支持 GPUDirect RDMA） | 对 socket transport，LL 和 Simple 的实际吞吐差异被 TCP 往返时延掩盖 |

**实测证据（来自 `phase4-size-aware-v2-tuning-20260309.log`）：**

```
call=1  bytes=1024  action=...  channels=2  aggr=2
→ NCCL INFO AllReduce: 1024 Bytes -> Algo TREE proto SIMPLE channel{Lo..Hi}={0..0}

call=3  bytes=4096  action=...  channels=4  aggr=2
→ NCCL INFO AllReduce: 4096 Bytes -> Algo TREE proto LL channel{Lo..Hi}={0..0}

call=7  bytes=32768  action=...  channels=4  aggr=2
→ NCCL INFO AllReduce: 32768 Bytes -> Algo TREE proto LL channel{Lo..Hi}={0..3}
```

结论：policy 的 algo/proto 确实被接受并执行（TREE/LL 被选中），但 channels=4 对应的有效通道范围是 `{0..0}` 到 `{0..3}`，说明实际使用的通道数受 ring/tree 结构约束（2 rank 场景下实际通道非常有限）。

### 1.3 NCCL 默认选择何时是次优的？

**NCCL 默认选择次优的已知场景（文献 + 实测）：**

1. **多节点大规模 AllReduce 的 RING vs TREE**：
   - Ring 在带宽方面最优（满载 pipeline），但每 AllReduce 的通信轮次是 `O(n)`（n 是节点数），随规模增大延迟线性增长。
   - Tree 在延迟方面是 `O(log n)`，在 24,000 GPU 规模下，Tree 比 Ring 快约 180 倍（NVIDIA Blog 数据）。
   - **2 rank 场景**：RING 和 TREE 在 2 rank 下**拓扑结构几乎相同**，都是两点之间互传（TREE 退化为链状）。因此 2 rank 下强制切换 algo 几乎没有可测量的性能差异。

2. **小消息 LL vs Simple 协议**：
   - LL 使用 4B 数据 + 4B 标志的原子写，实际有效带宽为峰值的约 50%（不支持 GPUDirect RDMA）。
   - LL128 使用 120B 数据 + 8B 标志，可达 95% 峰值带宽，但仅在 NVLink 场景下有效（socket transport 不支持 128B 原子写）。
   - Simple 在大消息下可达接近 100% 带宽，但小消息延迟高。
   - **对 socket transport**：LL 的存在意义大幅减弱，因为 socket 的往返延迟（~4ms）远超 LL vs Simple 的差异（~2-6μs）。

3. **nChannels 在高带宽 InfiniBand 下**：
   - 更多通道 = 更多 CUDA block = 更高 GPU 并行度，但每通道的 chunk size 变小。
   - 当 chunk size < 512KB（NIC FIFO 缓冲大小）时，NIC 发送未填满的缓冲区，实际吞吐下降。
   - NCCL 的启发式规则在大部分场景下正确，但在特定负载组合（混合 small/large）下可能次优。
   - **socket transport + 2 rank 场景**：已观察到 NCCL 将有效通道钳制在 4，plugin 返回更大值无效。

4. **单 GPU 2 rank 特殊情况（当前硬件）**：
   - 两个 rank 强制绑定同一 GPU（CUDA device 0），通信经过 socket（TCP loopback）。
   - 所有大小的 AllReduce 延迟约 4300μs，瓶颈完全是 TCP loopback 往返时延。
   - 在这种硬件约束下，任何 algo/proto/channels 的变化都无法突破 TCP 瓶颈，4 条线重合是必然的。

### 1.4 2-rank 场景下 RING 和 TREE 是否等价？

**几乎完全等价**，原因如下：

- **拓扑层面**：2 rank 的 Ring 是 0→1→0 的环，2 rank 的 Tree 退化为 0↔1 的链。两者实际通信模式相同：每个 rank 发送一半数据，接收一半数据，共两步。
- **NCCL 文档确认**：在 2 rank 时，如果将两棵树叠加处理两半数据，每个 rank 最多接发两次半份数据，这和 Ring 的通信量完全相同。
- **代价模型层面**：NCCL 的内部代价表（见 `phase4-size-aware-v2-tuning-20260309.log` 中 AllReduce 的 Tree/LL 和 Ring/LL 代价数值）在 2 rank 时非常接近（Tree=16.8/0.5，Ring=16.6/1.2），差异不到 2%，远低于测量噪声。

**结论**：在 2-rank 场景下，强制 TREE vs RING 无法产生可测量的端到端性能差异。

---

## 第二部分：相关研究总结

### 2.1 NCCL 调优相关研究

| 系统 | 发表场所 | 方法 | 性能提升来源 |
|------|---------|------|-------------|
| **MSCCL** (NSDI'23) | NSDI 2023 | 自定义 collective schedule（替换内部算法） | 1.2-2x，在 AllGather/ReduceScatter |
| **AutoCCL** (SC'24) | SC 2024 | 自动搜索 nChannels/chunk size | 最优配置 vs 默认，端到端 training throughput |
| **Demystifying NCCL** (arxiv'25) | ETH Zurich preprint | 实测所有 algo/proto 组合 | 文档化了各协议的实际带宽开销：LL 50%, LL128 95%, Simple ~100% |
| **AWS OFI NCCL Tuner** | 生产系统 | 基于区域和规则的 tuner plugin | 主要针对 EFA 的 InfiniBand 优化 |

### 2.2 关键文献数据点

**协议带宽效率（"Demystifying NCCL"）**：
- Simple: ~100% peak bandwidth（大消息）
- LL128: ~95% peak（NVLink 场景，依赖 128B 原子写）
- LL: ~50% peak（与传输无关，因为 4B flag overhead 固定）

**Ring vs Tree 性能差异（NVIDIA Blog）**：
- 2-4 ranks：差异极小，噪声级别
- 8-32 ranks：Tree 在小消息（<512KB）开始有优势
- 1024+ ranks：Tree 显著优于 Ring
- 24,000 GPUs：Tree 最多快 180x（由于 O(log n) vs O(n) 通信轮次）

**nChannels 的影响（NCCL 文档 + 社区 issue）**：
- 更多通道 = 更多并发 CUDA block，有利于大消息的 GPU 端并行
- 但 per-channel chunk size 减小，如果小于 NIC FIFO（512KB），吞吐下降
- Socket transport 下，nChannels 影响的主要是 CPU proxy thread 的并发度
- 在 2-rank socket 场景下，NCCL 将实际通道钳制在 4，超出无效

### 2.3 现有工作如何避免"4 条线重合"问题

| 系统 | 能展示性能差异的原因 |
|------|-------------------|
| MSCCL | 直接替换 NCCL 内核代码，效果不经过 "建议" 层 |
| AutoCCL | 在 A100 集群（8+节点，IB 网络）测试，瓶颈是真实网络 |
| XRP (OSDI'22) | eBPF 直接改变 NVMe 命令处理路径（测量 IPC 往返节省），机制直接可测 |
| Electrode (NSDI'23) | 减少 kernel wake-up 延迟，在共识协议中直接可测 |

**NCCLPol 的根本困难**：tuner plugin 的输出是"建议"而非"命令"。在 socket transport + 2 rank + 单 GPU 的约束环境下，NCCL 可以（且会）忽略或截断不合理的建议，导致端到端无可测量差异。

---

## 第三部分：所有可行方案详细评估

### 方向 A：找到 NCCL 默认次优的场景

#### A1. 强制次优 NCCL 配置（env var 方法）

**描述**：用 `NCCL_ALGO=^tree`（排除 TREE）或 `NCCL_ALGO=ring` 强制 NCCL 只用 RING，然后对比 policy 启用 TREE 时的性能。

**技术可行性**：**低**
- 在 2-rank socket 场景，RING 和 TREE 性能几乎完全相同（见 1.4 节）。
- 即使强制次优，差异在噪声范围内（<2%），无法与 4.3ms 的 socket 往返时延区分。

**预期效果**：几乎为零差异。

**实现难度**：低（只需改环境变量）

**对论文的贡献**：负面——如果做了这个实验但没有找到差异，反而削弱 claim。

**推荐度**：不推荐。

#### A2. 大消息范围下 proto 切换（LL vs Simple）

**描述**：在超大消息（>4MB）下测试 `NCCL_PROTO=LL` vs `NCCL_PROTO=Simple` 的带宽差异，因为 LL 只有 50% 带宽效率。

**技术可行性**：**中等**
- 在 socket transport 下，大消息延迟主要被 TCP 传输时间决定，LL 的 2x overhead 理论上应该在大消息上可见（如 128MB 时：Simple ~175ms，LL 理论应 ~350ms）。
- 但 NCCL 对 socket transport 的 LL 协议有特殊处理：LL 在 socket 下本来就不推荐，NCCL 默认会选 Simple 做大消息。强制 LL 可以是人工制造次优场景。

**预期效果**：在大消息（>1MB）下，`NCCL_PROTO=LL` 对比 `NCCL_PROTO=Simple` 可能有 1.5-2x 的时延差异，policy 可以"纠正"这个次优选择。

**实现难度**：中（需要验证 NCCL 是否真正切换到 LL 在大消息上）

**对论文的贡献**：可以构造"bad static config（强制 LL）→ policy 纠正（选 Simple）→ 性能恢复"的叙事。

**推荐度**：**优先级 2**（需要先验证 proto 切换是否有效果）。

---

### 方向 B：Adaptive 比 Static 好（协议自适应）

#### B1. 混合 workload 下的 proto 自适应

**描述**：构造一个序列：交替运行小消息（1KB，应选 LL）和大消息（4MB，应选 Simple）。
- Static-LL policy：始终用 LL → 小消息快，大消息带宽只有 50%
- Static-Simple policy：始终用 Simple → 大消息好，小消息延迟高
- Adaptive policy（size_aware_v2）：自动切换 → 两者均达最优

**技术可行性**：**中等**
- 前提是 proto 切换在 socket transport 下有可测量效果（见 A2 的分析）。
- 如果大消息 LL vs Simple 有差异，这个方案就成立。
- 小消息（<1KB）在 socket transport 下全是 socket 往返时延，无论 LL 还是 Simple 都一样快（都是 ~4ms），proto 切换在小消息上无效。

**预期效果**：只在大消息端有差异（如果 proto 切换有效）。在小消息端无差异。

**实现难度**：中（需要修改测试脚本循环不同大小）

**对论文的贡献**：展示 adaptive policy 在混合 workload 下的正确性（不一定是性能优于 baseline，而是优于次优静态配置）。

**推荐度**：**优先级 3**（依赖 B1 的验证结果）。

---

### 方向 C：展示 Channel Count 对吞吐量的影响

#### C1. Channel 数量对大消息吞吐量的影响

**描述**：
1. 用 `NCCL_MIN_NCHANNELS=1 NCCL_MAX_NCHANNELS=1` 强制单通道，与默认 4 通道对比。
2. 如果有可测量差异，展示 size-aware channel policy 的效果（小消息 1 通道，大消息 4 通道）。

**技术可行性**：**中低**
- 已知 NCCL 在 socket transport + 2 rank 场景下将有效通道钳制在 4（`profiler-adapter-results.md` 明确说明）。
- 但 1 通道 vs 4 通道的差异可能仍然可测：更多 channel = 更多 CUDA block = 更高 GPU pipeline 利用率（对大消息有用）。
- 实测数据（`benchmark-results.md`）：不同 channel 配置的延迟在单 GPU 路径上几乎没有差异，因为单 GPU 2-rank 的瓶颈是 TCP socket，不是 CUDA 计算。

**预期效果**：在当前硬件（socket transport）下，可能几乎没有差异。在多 GPU + InfiniBand 环境下差异可能达到 2-3x（如 AutoCCL 所示）。

**实现难度**：低（只需设置环境变量）

**对论文的贡献**：如果没有差异，这本身是个有趣的发现（"socket transport 下 channel count 效果被 TCP 瓶颈完全掩盖"）；如果有差异，可以展示 policy 的效果。

**推荐度**：**优先级 3**（先验证是否有差异，5 分钟实验）。

---

### 方向 D：顺序 Workload Trace 的累积效果

#### D1. LLM 梯度序列模拟

**描述**：构造模拟 LLM 训练梯度同步的 collective 序列：
- 不同层大小的 AllReduce：1KB（embedding layer）、64KB（attention）、4MB（FFN weights）
- 100 轮重复，测量总完成时间
- Static-all（固定 RING/Simple/4ch）vs Adaptive（size_aware_v2，动态切换）

**技术可行性**：**高**（实现简单）

**预期效果**：
- 如果 proto 切换有效（方向 A2/B1 验证后），这个序列会显示 adaptive policy 在大消息端节省时间。
- 如果 proto 切换在 socket transport 下无效，这个实验也不会有差异。

**实现难度**：低（修改 nccl-tests 脚本，循环多个大小）

**对论文的贡献**：贴近真实 training workload 的场景叙事，即使没有量化改善，也能展示 policy 的意图。

**推荐度**：**优先级 3**（实现成本低，可以和其他实验一起做）。

---

### 方向 E："坏" Policy vs "好" Policy — 退化演示（最重要）

#### E1. Native 坏 Plugin → 性能退化 + eBPF policy → 安全拒绝

**描述**：
1. 写一个 native C tuner plugin（不使用 eBPF），实现故意次优的决策（如强制所有大消息用 LL 协议，或强制 `nChannels=1`）。
2. 测量这个 bad native plugin 带来的性能退化（vs baseline）。
3. 写等效的 bad eBPF policy，展示验证器在加载时拒绝它（如果它违反安全属性），或者展示 eBPF 验证器的热重载 fail-safe 阻止了加载。
4. 展示"good" eBPF policy（size_aware_v2）在相同场景下正常运行。

**技术可行性**：**高**
- Native bad plugin 很容易写：强制所有大消息使用 LL 协议（`nChannels=1`）。
- 如果方向 A2 验证了 LL vs Simple 在大消息上有 1.5-2x 的差异，这个实验就有了量化的退化数据。
- 等效 eBPF policy 的"安全属性"可以定义为：例如禁止将大消息（>1MB）的 channels 设为 1（单通道策略在高负载下是已知次优的），验证器通过静态分析拒绝。

**更直接的版本（不依赖性能差异）**：
- Native bad plugin：故意返回无效指针，导致 SIGSEGV（已有 `bad_lookup.bpf.c` 的 eBPF 版本）。
- 数据：bad native plugin SIGSEGV（进程崩溃），等效 bad eBPF → REJECT（进程继续运行，用 fallback noop）。
- 这不需要任何 GPU 性能测量，纯 CPU 侧安全演示。

**预期数据**：
```
场景                        | 结果
bad native plugin（崩溃型）  | SIGSEGV，NCCL 进程崩溃
bad eBPF policy（崩溃型）    | 验证器 REJECT，进程继续运行
good eBPF policy             | 正常运行，channels=2/4 按大小
```

或加入性能维度（如果 A2 验证成功）：
```
场景                         | 1MB AllReduce 延迟 | 结论
baseline                     | 4.38ms             | 正常
bad native（强制 LL）        | ~8.76ms（理论 2x） | 性能退化
bad eBPF（强制 LL + REJECT） | 4.38ms（不运行）   | 安全拒绝，无退化
good eBPF（size_aware_v2）   | 4.38ms             | 正常
```

**实现难度**：**低**（最重要的安全演示版本已经在 phase3 实现了，只需加入量化退化对比）

**对论文的贡献**：**极高**。这是"safety prevents not just crashes but performance regressions"的最强论点，是 NCCLPol 核心安全 claim 的扩展。直接回应 "为什么不用普通 native plugin" 的反驳。

**推荐度**：**优先级 1（必须做）**。

---

### 方向 F：微基准层面的 Policy Decision Quality

#### F1. Policy 决策与最优静态配置对比

**描述**：
1. 系统性扫描所有消息大小下的 (algo, proto) 组合（用环境变量 `NCCL_ALGO` 和 `NCCL_PROTO` 逐个强制），记录每个组合的延迟/带宽。
2. 构建"最优静态配置表"（每个消息大小的最优 (algo, proto)）。
3. 对比 `size_aware_v2` policy 的决策是否与最优配置一致。

**技术可行性**：**高**（只需运行多组 nccl-tests）

**预期效果**：
- 在 socket transport 下，大部分消息大小的最优配置都是 RING/Simple（LL 在 socket 下带宽只有 50%，Socket transport 在 2 rank 下 RING ≈ TREE）。
- `size_aware_v2` 在 <4KB 时选 TREE/Simple，在 4KB-1MB 时选 TREE/LL，这可能与最优配置**不一致**（LL 在 socket transport 下可能并非最优）。
- 这个实验能客观评价当前 policy 的决策质量，并找出改进空间。

**实现难度**：中（需要运行约 20 组实验，自动化脚本）

**对论文的贡献**：中等。提供了"policy 决策正确性"的量化评估，但如果 policy 决策与最优不一致，反而可能损害论文。

**推荐度**：**优先级 4**（先做 E1，再考虑这个）。

---

### 方向 G：使用不同 Transport（共享内存 vs Socket）

#### G1. 启用 Shared Memory Transport

**描述**：去掉 `NCCL_SHM_DISABLE=1`，允许 NCCL 在同一主机上用 SHM transport（两个 rank 都在同一台机器上，SHM 是更自然的传输方式）。

**技术可行性**：**高**（只需去掉一个环境变量）

**预期效果**：
- SHM transport 的延迟可能比 TCP socket 低几个数量级（不需要经过 TCP loopback，直接内存共享）。
- 如果 SHM 下的 AllReduce 延迟从 4ms 降低到 100-500μs，那么 100ns 的 eBPF policy 开销从 0.002% 变成 0.01-0.1%，仍然可忽略。
- 更重要的是：SHM transport 下，algo/proto/channels 的选择对延迟的影响比 socket transport 更大（瓶颈不是 TCP 而是 CUDA kernel 本身）。

**已知障碍**：
- 目前用 `NCCL_SHM_DISABLE=1` 的原因是：2 个 rank 都在同一 GPU 上，NCCL 的 SHM transport 是为不同 GPU 在同一主机上设计的，不是为同一 GPU 多 rank 设计的。去掉 SHM_DISABLE 后是否有效需要实测。
- 如果两个 rank 在同一 CUDA device 上，NCCL 可能会拒绝（"duplicate GPU"），这也是之前加 NCCL_HOSTID 的原因。

**实现难度**：低（试验是否可以去掉 NCCL_SHM_DISABLE）

**对论文的贡献**：如果 SHM transport 能工作，可以降低绝对延迟，让 eBPF 开销的相对影响更明显，并让 algo/proto 差异更可测量。

**推荐度**：**优先级 2**（低成本验证实验）。

#### G2. 使用真实双机（如果可用）

**描述**：如果有另一台机器（哪怕另一个 CPU 进程 + 虚拟 GPU），用真实 socket transport 的双机场景。

**技术可行性**：未知（取决于硬件可用性）

**推荐度**：**长期目标**，不在当前迭代范围内。

---

### 方向 H：测量非延迟指标

#### H1. 吞吐量（Bandwidth）而非延迟

**描述**：将 nccl-tests 的评估指标从延迟（`time_us`）切换到带宽（`algbw` / `busbw`），并在大消息范围（128MB）进行测试。

**技术可行性**：**高**（nccl-tests 直接输出 busbw）

**预期效果**：
- 大消息（128MB）下，不同 proto 的带宽差异可能比延迟差异更明显。
- `NCCL_PROTO=LL`（50% 带宽）vs `NCCL_PROTO=Simple`（~100% 带宽）的差异在大消息带宽测量中应该是 2x。

**实现难度**：低

**对论文的贡献**：如果 proto 切换有效，大消息带宽图能展示 policy 在"正确选择 Simple"时的 2x 带宽提升 vs "被强制 LL"的 baseline。

**推荐度**：**优先级 2**（与 A2 方向一起验证）。

---

### 方向 I：增加 Rank 数

#### I1. 模拟更多 Rank（单 GPU 上）

**描述**：是否可以在单 GPU 上模拟 4 rank（每 rank 一个 MPI 进程，共享同一 GPU），通过 4 个 NCCL_HOSTID 区分？

**技术可行性**：**低**
- 标准 NCCL 不支持同一 GPU 上的多个 rank（会报 "duplicate GPU detected"）。
- 目前 2 rank 的运行依赖 `NCCL_HOSTID` hack，但这只能处理 2 rank（rank 0 和 rank 1 各有唯一 HOSTID）。
- 4 rank 共享同一 GPU 在理论上需要 NCCL 内部对 GPU 设备检测的更大修改，超出 HOSTID hack 的能力。

**推荐度**：不推荐（技术风险高，在当前硬件上可能无法工作）。

---

### 方向 J：多 Communicator 差异化

#### J1. 同进程双 Communicator 不同 SLO

**描述**：在同一进程中创建两个 communicator，一个代表高优先级（1ms SLO），一个代表低优先级（10ms SLO），policy 给不同配置（TREE/LL vs RING/Simple）。

**技术可行性**：**已实现**（这是 RQ3 表达力演示的内容，来自 `revise2-results.md`）

**预期效果**：已有控制平面正确性数据（decisions differ per communicator），但没有端到端 SLO 满足率数据。

**实现难度**：已实现

**对论文的贡献**：已有数据，可以直接用于论文。

**推荐度**：不需要额外工作，数据已存在。

---

## 第四部分：方案评估矩阵

| 方案 | 技术可行性 | 预期效果 | 实现难度 | 对论文贡献 | 推荐优先级 |
|------|-----------|---------|---------|-----------|-----------|
| E1. bad native plugin 退化演示 | 高 | 高（清晰故事） | 低 | 极高（支持 safety claim） | **P1（必做）** |
| A2. LL vs Simple 大消息带宽 | 中 | 中（2x 差异） | 低 | 高（如果验证成功） | **P2（验证实验）** |
| H1. 带宽指标测量 | 高 | 中 | 低 | 高（与 A2 一起） | **P2（低成本）** |
| G1. SHM transport 尝试 | 中 | 中-高 | 低 | 中（降低绝对延迟） | **P2（5分钟实验）** |
| B1. 混合 workload proto 自适应 | 中 | 中 | 中 | 中 | **P3（依赖 A2）** |
| D1. LLM 序列模拟 | 高 | 低-中 | 低 | 中（叙事价值） | **P3（易实现）** |
| C1. Channel 数量吞吐量影响 | 中低 | 低（socket 下 capped） | 低 | 低 | P4（5分钟验证） |
| F1. Policy decision quality 扫描 | 高 | 中 | 中 | 中（可能揭示问题） | P4（风险） |
| A1. 强制次优 RING/TREE | 低 | 极低 | 低 | 负面 | 不推荐 |
| I1. 4+ rank 单 GPU 模拟 | 低 | 高（如果成功） | 高 | 高 | 不推荐（技术风险） |

---

## 第五部分：推荐优先级排序和具体实施计划

### 优先级 1（必须做，1-2天）：E1 — 安全退化演示

**目标**：展示 bad native plugin 导致性能退化（或崩溃），bad eBPF policy 被安全拒绝，好 policy 正常运行。

**实施步骤**：

1. **写 bad native tuner plugin（1小时）**：
   - 实现两个变体：
     - `bad_crash_native.c`：故意返回坏指针（会 SIGSEGV）——用于安全演示，已有对等的 eBPF 版本 `bad_lookup.bpf.c`
     - `bad_perf_native.c`：对所有消息强制 `NCCL_PROTO_LL`（50% 带宽），用环境变量方式等效

2. **测量退化幅度（30分钟）**：
   - 用 `NCCL_PROTO=LL` 环境变量强制 LL，测大消息（16MB, 64MB, 128MB）的 busbw
   - 对比 baseline（默认选择 Simple）
   - 预期：大消息带宽约为 baseline 的 50%（LL 的理论带宽效率）

3. **构建故事（文档）**：
   ```
   bad native plugin → 崩溃（或 2x 延迟退化）
   bad eBPF policy   → 验证器拒绝（REJECT），fallback noop，性能正常
   good eBPF policy  → 正常运行，做出正确决策
   ```

**预期产出**：一张清晰的对比表，展示 eBPF 验证的安全价值（防止崩溃 + 防止性能退化）。

---

### 优先级 2（推荐做，0.5-1天）：A2/H1 — Proto 切换带宽实验

**目标**：验证在 socket transport 大消息场景下，`NCCL_PROTO=LL` vs `NCCL_PROTO=Simple` 是否有可测量的带宽差异。

**实施步骤**：

1. **5 分钟验证实验**：
   ```bash
   # 对比：默认（Simple）vs 强制 LL
   NCCL_PROTO=Simple mpirun ... all_reduce_perf_mpi -b 16M -e 128M -f 2 -n 20
   NCCL_PROTO=LL     mpirun ... all_reduce_perf_mpi -b 16M -e 128M -f 2 -n 20
   ```
   记录 busbw（总线带宽，GB/s）

2. **如果有差异（>20%）**：
   - 构造 B1 方案的实验：混合小/大消息的 workload，adaptive policy vs static LL vs static Simple
   - policy（size_aware_v2）的决策正确性变得可测量

3. **如果没有差异**：
   - socket transport 下 LL 的 2x overhead 被 TCP 瓶颈完全掩盖
   - 记录这一发现（"socket transport masks all protocol-level differences"）
   - 这本身支持"需要 InfiniBand 或 NVLink 才能看到 policy effectiveness"的论点

**预期产出**：验证数据，决定是否进行更复杂的 B1 实验。

---

### 优先级 2（低成本，30分钟）：G1 — SHM Transport 尝试

**目标**：测试去掉 `NCCL_SHM_DISABLE=1` 是否可以工作，如果可以，SHM transport 的延迟是否更低。

**实施步骤**：

1. **实验命令**（去掉 `NCCL_SHM_DISABLE=1`）：
   ```bash
   mpirun --oversubscribe \
     -np 1 env NCCL_P2P_DISABLE=1 NCCL_NET=Socket NCCL_HOSTID=rank0 ... \
     : \
     -np 1 env NCCL_P2P_DISABLE=1 NCCL_NET=Socket NCCL_HOSTID=rank1 ...
   ```
   去掉 `NCCL_SHM_DISABLE=1`，看 NCCL 是否选择 SHM。

2. **如果 SHM 工作**：
   - 测量 SHM transport 下的延迟，与 socket 对比
   - 如果延迟从 4ms 降到 100-500μs，algo/proto 差异更可测量
   - 在 SHM 场景下重新运行 A2 和 E1 实验

3. **如果 SHM 不工作（预期结果）**：
   - 说明在单 GPU 双 rank 场景，NCCL 无法使用 SHM，socket 是唯一 transport
   - 记录这一约束

**预期产出**：验证 SHM 是否可用，决定是否切换 transport。

---

### 优先级 3（可选，2-4小时）：D1 — LLM Workload 序列模拟

**目标**：展示 adaptive policy 在混合 workload 下的正确性和潜在收益。

**实施步骤**：

1. **构造测试脚本**：
   ```bash
   # 模拟 LLM training: 多层梯度同步，不同大小
   sizes=(1024 4096 65536 1048576 16777216 67108864)
   for round in $(seq 1 100); do
     for size in "${sizes[@]}"; do
       mpirun ... all_reduce_perf_mpi -b $size -e $size -n 5
     done
   done
   ```

2. **对比三种配置**：
   - baseline（无 plugin）
   - static-ring-simple（强制 RING/SIMPLE 的 native plugin）
   - adaptive（size_aware_v2 eBPF policy）

3. **测量总完成时间**

**注意**：这个实验的意义主要是叙事价值，在当前硬件上实际数值差异可能很小。

---

## 第六部分：论文修订建议

### 当前状态的诚实评估

**已证明的（确实可以作为 claim）**：
1. eBPF policy 可集成进 NCCL plugin ABI，不修改 NCCL 源码（Phase 4 验证）
2. eBPF dispatch 开销 +33-77ns P50，是 GPU 集体时延的 <0.1%（CPU 微基准）
3. 7 类安全 bug 在加载时被验证器拦截（Phase 3）
4. 热重载正确：7.3ms 全流程，400k 次调用零丢失（Phase 3）
5. 控制平面表达力：能表达 SLO、多租户、adaptive channel 等 policy（RQ3）
6. Profiler-tuner 闭环：真实延迟数据驱动 channel 决策变化（profiler adapter 实验）
7. Policy 改变了 NCCL 的 algo/proto 决策（日志证据：TREE/LL 被正确选中）

**没有证明的（需要在 Limitations 中明确说明）**：
1. Policy 改善了任何可测量的 GPU 端延迟或带宽（4 条线重合）
2. eBPF policy 比 NCCL 默认选择更优（无法在 2-rank socket 场景下验证）
3. SLO enforcer 实际满足了 SLO（控制平面正确性 ≠ SLO 满足率）

### Figure 4（GPU 延迟图）的重新定位

当前图的正确叙述（不是"4 条线重合说明 policy 有效"）：
- 横轴：消息大小（8B-128MB）
- 纵轴：AllReduce 延迟（us）
- 4 条线：baseline、noop、size_aware_v2、slo_enforcer
- **正确 claim**："4 条线重合证明 eBPF policy 的运行时开销（100ns 级别）在 GPU 侧完全不可见，overhead 可忽略"
- **不正确的额外 claim**："4 条线重合证明 policy 有效"——这个图不支持也不反驳 policy effectiveness

### 推荐的论文叙述修订

**关于 Figure 4**，建议在正文中明确说明：
```
Figure 4 demonstrates that the eBPF policy dispatch overhead (51-134 ns) is
invisible at GPU scale: all four configurations produce identical end-to-end
latency, confirming that eBPF is a zero-cost abstraction in this dimension.
Note that the 2-rank socket topology used here constrains measurable
differentiation: the ~4.3ms TCP loopback latency dominates all collective sizes,
and 2-rank RING and TREE are topologically equivalent.
Evaluating policy effectiveness on throughput-differentiating workloads
(e.g., multi-node InfiniBand AllReduce) is deferred to future work.
```

**关于 E1 实验**，如果成功，可以加一句：
```
To demonstrate that the safety properties matter for performance, Figure X shows
that a deliberately suboptimal native plugin (forcing LL protocol for all
messages) achieves ~50% of baseline bandwidth for large messages, while an
equivalent bad eBPF policy is rejected at load time, leaving performance
unaffected.
```

---

## 总结

**当前硬件的根本约束**：1 RTX 5090 + 2 rank + socket transport + TCP loopback，全部大小的 AllReduce 延迟被 4.3ms TCP 往返时间主导，任何 algo/proto/channel 的变化都无法突破这个瓶颈。

**最值得做的实验（优先级排序）**：

1. **E1（必做）**：bad native plugin 退化演示。不需要突破硬件限制，直接展示"safety prevents performance regression"，是 workshop 论文最有力的新证据。

2. **A2/H1（快速验证）**：NCCL_PROTO=LL vs Simple 的大消息带宽对比，5 分钟验证实验，决定其余方向的可行性。

3. **G1（30分钟）**：去掉 SHM_DISABLE，测试是否能用更低延迟的 transport，如果成功可以降低基线延迟，让 policy 差异更可见。

4. **D1（可选）**：LLM 序列模拟，主要是叙事价值。

**对 workshop 论文的最终建议**：当前数据对 workshop 来说是足够的，核心 claim 是 safety + overhead + expressiveness，不是 performance improvement。诚实限定 claim 范围，并将 E1 实验作为"safety also prevents performance regressions"的新数据点，论文质量将显著提升。
