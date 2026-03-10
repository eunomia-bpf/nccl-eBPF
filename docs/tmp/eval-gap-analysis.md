# NCCLPol 评估缺口分析

*分析日期：2026-03-09*

---

## 第一部分：当前评估的缺口（逐 RQ 分析）

### RQ1：CPU 开销与分解

**当前证明了什么：**
- eBPF dispatch 固定开销 +51ns，每次 map lookup +26ns，每次 map update +25ns
- 公式 `51 + 26·n_lookup + 25·n_update` ns 可预测开销
- 最复杂 policy（slo_enforcer）P50 = 113ns，P99 = 134ns
- Figure 4（GPU 延迟图）：4 条线完全重合，证明 100ns 的 CPU 开销在 GPU 侧不可见

**当前没有证明什么：**
- 开销数据来自 CPU 孤立核心上的 1M 次**直接调用循环**（没有 NCCL 上下文），不是真实 NCCL 调用路径中的开销
- GPU 图中 4 条线重合，**既没有证明 policy 有效，也没有证明 policy 无效** — 只证明了 policy 的 CPU 开销不会增加 GPU 集体时延
- 没有在真实 InfiniBand 或多节点下的开销数据
- 2 rank socket transport 场景中，集体时延约 4.3ms（见 phase4-baseline.log），这个时延几乎全是 socket 往返，与 policy 决策完全无关

**机制 vs. 效果的缺口：**
这是 RQ1 的核心问题。"4 条线重合"这个图有歧义：
- 正面解读：CPU 开销可忽略，eBPF 是零成本抽象
- 负面解读：policy 对 algo/proto/channels 的调整在当前硬件上根本没有效果，因此所有配置的 GPU 时延都一样

实际上，`size_aware_v2` 在不同消息大小下**确实改变了 algo/proto 选择**（从 phase4-size-aware-v2-tuning 日志可以看出：1KB→TREE/SIMPLE，4KB→TREE/LL，1MB→RING/SIMPLE），但这些不同的决策是否带来了可测量的 GPU 端差异，**论文完全没有证明**。

---

### RQ2：安全性与热重载

**当前证明了什么：**
- 14/14 验证器判定正确（7 safe + 7 unsafe）
- 7 类 bug（空指针、越界、非法 helper、栈溢出、无界循环、写 ctx 只读字段、除零）均被拦截
- `bad_lookup`（空指针解引用）：native plugin 产生 SIGSEGV，eBPF 在加载时 REJECT
- 热重载：原子交换 0.774μs，全流程 ~7.3ms，400k 次调用零丢失
- 不安全 policy 的热重载 fail-safe：被拒绝，旧 policy 继续运行

**当前没有证明什么：**
- 验证器测试套件是**作者自构造的**（论文的 Limitations 段承认这一点），没有来自真实世界 plugin bug 的语料库
- 热重载测试是单进程，没有测试跨 rank 场景或真实 GPU 负载下的行为
- 没有定量比较 eBPF 验证 vs. 其他安全机制（Wasm、进程隔离）的 overhead 数字
- 没有显示 MPK 内存隔离的有效性（论文提到 EIM 用 MPK，但没有相关测试）

**机制 vs. 效果的缺口：**
RQ2 是这篇论文最强的一个 RQ。安全演示逻辑清晰，证据链完整。主要缺口在于验证器覆盖率的主观性——但对 workshop 论文来说这是可接受的。

---

### RQ3：Policy 表达力

**当前证明了什么：**
- 闭环适应：profiler 数据通过 eBPF map 影响 tuner 决策（channels 从 8→10）
- 多租户分化：同进程两个 communicator 收到不同配置（1ms SLO 用 TREE/LL，10ms SLO 用 RING/SIMPLE）
- 轮廓适应：channels 在 3 个阶段（baseline 12→contention 2→recovery 12）单调变化
- 多 collective 覆盖：AllReduce/AllGather/ReduceScatter/Broadcast 均有不同决策

**当前没有证明什么：**
- **这是最大的缺口**：所有 RQ3 实验都是**控制平面正确性验证**，而不是**端到端效果验证**
  - contention 注入是合成的（通过写 map 模拟延迟），不是真实的 GPU 计算负载竞争
  - channels 从 12 降到 2 这个行为的**实际效果是什么？真实场景下会更好还是更差？** 论文没有回答
  - SLO enforcer 产生了不同的 algo/proto，但没有测量这些决策是否真的满足了 1ms 和 10ms 的 SLO
  - 多 collective 覆盖只是展示了决策表格，没有吞吐量或延迟对比
- profiler→tuner 闭环：channels 从 8 变到 10，但没有测量这个变化是否带来了任何可见的性能改善

**机制 vs. 效果的缺口：**
RQ3 目前是"我们能表达这些 policy 逻辑"，完全没有达到"这些 policy 改善了结果"。论文自己在 Section 5 明确说了：

> "We frame this as an expressiveness demonstration: we show that the eBPF programming model can express these patterns, not that the resulting parameter changes improve end-to-end training outcomes."

这是诚实的，但也意味着 RQ3 当前只能支持"expressiveness"这个 claim，不能支持任何性能或 SLO 效果的 claim。

---

### Figure 4（GPU 延迟图）专项分析

**4 条线为什么重合：**

从实际日志数据来看，根本原因是：
1. **socket transport 瓶颈掩盖了一切**：2 rank socket 场景，每个消息大小的延迟约 4300μs（~4.3ms），这是 TCP socket 往返时延，与 algo/proto/channels 基本无关
2. **单 rank（in-place）快到极端**：baseline 中 in-place 8B AllReduce 只需 0.05μs，说明单 rank 没有实际通信，所有 policy 选什么 algo 都无所谓
3. **2 rank 规模太小**：TREE 和 RING 在 2 rank 时行为几乎相同（TREE 退化为链状）
4. **通信不是瓶颈**：当前测试中 GPU 计算和 socket 通信的比例失衡，policy 调整的参数（channels、algo、proto）不影响主要瓶颈

**重合意味着什么：**
- 它**不**意味着 policy 无效；它意味着在当前受限硬件上，效果无法测量
- size_aware_v2 确实在 NCCL 日志里改变了 algo（1KB=TREE，64KB=RING），但 socket 延迟掩盖了这个差异
- 这个图目前只能支持"overhead 可忽略"这一 claim，不能支持任何更强的论点

---

## 第二部分：在当前硬件上可行的改进实验

### 方案 A：人工构造 NCCL 默认选择次优的场景（可行性：中等）

**思路：**
在 2 rank + socket 场景中，NCCL 默认会基于 bandwidth/latency 模型选择 algo/proto。如果我们能证明在某个特定消息大小范围内，policy 选择的 algo 比 NCCL 默认选择更快，这就是最直接的性能改善证据。

**具体方案：**
1. 强制设置 `NCCL_ALGO=TREE` vs. `NCCL_ALGO=RING` 环境变量，在不同消息大小下跑 all_reduce_perf，找出 NCCL 默认选择次优的消息大小点
2. 写一个 policy：在该消息大小范围内覆盖 NCCL 默认选择，使用更优的 algo
3. 对比：baseline（无 plugin）vs. policy（覆盖选择） → 测量 GPU 端延迟差异

**障碍：**
- socket transport 下 algo 差异被通信延迟掩盖，可能找不到显著的差异点
- 1 个 GPU + 2 rank 的 socket 场景，NCCL 的默认选择已经比较优化
- 需要探索更大消息大小范围（128MB 以上）

**预期数据形式：**
```
消息大小   | NCCL默认(RING/LL) | Policy强制(TREE/LL) | 差异
32KB       | 4380μs            | 4310μs              | -1.6%
```

---

### 方案 B：协议（proto）切换效果 — LL vs. Simple（可行性：中等）

**思路：**
NCCL 的 LL（Low-Latency）协议使用 2x 内存换取更低的小消息延迟。在当前 socket transport 下，LL 协议的延迟优势被掩盖，但 Simple 协议在大消息时有更高吞吐量。

**具体方案：**
1. 在大消息（>256KB）强制 LL vs. SIMPLE，测量吞吐量差异
2. policy：根据消息大小动态切换 proto（LL for small, Simple for large）
3. 对比：static LL（始终 LL）vs. static Simple（始终 Simple）vs. policy（自适应）

**已有数据参考：**
size_aware_v2 在 1MB 时选 RING/SIMPLE，在 32KB 时选 TREE/LL。如果能证明 SIMPLE 在 1MB 时比 LL 有更高吞吐量，这就是 policy 有效的证据。

---

### 方案 C：channel 数量对吞吐量的影响（可行性：较高）

**思路：**
channel 数量直接影响并行度。在 socket transport 下，更多 channels 可以并行发送更多分片，从而提高大消息吞吐量。这是最可能在当前硬件上展示效果的参数。

**具体方案：**
1. 强制 `NCCL_MIN_NCHANNELS=1` vs. `NCCL_MAX_NCHANNELS=4`，在大消息下测量吞吐量差异
2. 如果 1 channel vs. 4 channels 有可测量差异，则：
   - 写一个 size_threshold policy：小消息 1 channel，大消息 4 channels
   - 对比 static-1ch vs. static-4ch vs. adaptive-policy
3. 如果效果不显著，退而求其次：展示 channel 变化带来吞吐量曲线的可测量变化

**预期数据形式（如果成功）：**
```
消息大小   | 1 channel | 4 channels | policy(自适应)
1MB        | 0.24 GB/s | 0.47 GB/s  | 0.47 GB/s
32KB       | 0.01 GB/s | 0.01 GB/s  | 0.01 GB/s
```

---

### 方案 D：顺序 workload trace 中的自适应效果（可行性：较高）

**思路：**
与其在单个 collective 上证明改善，不如在一个**混合消息大小序列**上展示 adaptive policy vs. static policy 的累积效果差异。

**具体方案：**
1. 构造一个模拟 LLM 训练的 collective 序列：
   - gradient AllReduce：多个不同层大小（1KB → 128MB）
   - 序列按层顺序发出，共 100 轮
2. Static policy（始终 RING/SIMPLE/4ch）vs. Size-aware policy（per-size 选择）
3. 测量总完成时间，如果 static 在小消息上有 LL 优势则会有差异

**实现复杂度：低**，只需修改 nccl-tests 的调用顺序。

---

### 方案 E："坏" policy vs. "好" policy — 退化演示（可行性：高，且是最重要的）

**思路：**
换一个角度：不证明 eBPF policy 比 NCCL 默认更好，而是证明**一个错误的 native plugin 可以让性能退化，而 eBPF policy 的验证器能阻止这种退化**。

这实际上是 RQ2 的延伸，但加入了性能维度：

**具体方案：**
1. 写一个 native bad tuner plugin：在所有消息大小都强制 1 channel，或者强制使用错误 algo
2. 测量这个 bad native plugin 带来的性能退化（vs. baseline）
3. 展示等效的 bad eBPF policy 被验证器拒绝（不是崩溃，而是安全拒绝）
4. 对比：bad native plugin（可运行但性能差）vs. bad eBPF policy（被拒绝）

**数据形式：**
```
场景                      | 1MB 延迟
baseline（无 plugin）     | 4.38ms
bad native plugin         | 8.76ms（人工退化 2x）
bad eBPF policy           | REJECT（性能不受影响）
good eBPF policy          | 4.38ms（正常运行）
```

这个实验**不需要展示性能改善**，只需要展示安全性防止退化，这是 NCCLPol 核心安全 claim 的最强支撑证据之一。

---

### 方案 F：微基准中的 policy decision quality（可行性：很高）

**思路：**
在 CPU 微基准层面（不需要 GPU），比较不同 policy 决策的质量。即：给定一个消息大小，policy 的决策是否与最优静态配置一致？

**具体方案：**
1. 用 nccl-tests 手动扫描所有 algo/proto/channel 组合，记录每个消息大小下的最优配置
2. 对比 size_aware_v2 policy 的决策 vs. 最优静态配置
3. 对于 policy 决策偏离最优的情况，解释原因（或修正 policy）

这个实验不需要新的硬件，只需要系统性的扫描测试。

---

## 第三部分：其他论文如何处理有限硬件

### XRP（OSDI'22）—— eBPF for NVMe

**硬件**：单节点，标准 NVMe SSD（不是专有硬件）

**评估策略**：
- 核心 claim 是"eBPF 在 NVMe 命令处理路径中节省 IPC 往返"
- 用 FIO 这样的合成工作负载构造了受控测试
- 没有用真实存储工作负载证明端到端提升，只证明了延迟分解（每次 IPC 往返 ~5μs，eBPF 版本节省这个开销）
- **对我们的启示**：XRP 也是只证明了"机制有效"（节省 IPC），没有在真实数据库工作负载上证明"应用提升"

### Electrode（NSDI'23）—— eBPF for Consensus

**硬件**：3-5 个 commodity servers，100GbE

**评估策略**：
- 重点是延迟下降：用 eBPF bypass kernel scheduler，减少唤醒延迟
- 对比 Raft leader commit latency：kernel 版本 vs. eBPF 版本
- 用 YCSB 合成工作负载，不是真实应用
- 关键 claim："eBPF 在 fast path 处理，减少 context switch" — 可以直接测量

**对我们的启示**：Electrode 的优势在于 consensus latency 是**直接可测量的**，而 NCCL 的 policy 影响是间接的（通过 algo/proto 影响 GPU kernel 选择）。

### eTran（OSDI'24）—— eBPF for RDMA

**硬件**：2 节点，100GbE，Mellanox RDMA NIC

**评估策略**：
- 重点是 RDMA transport 的可编程性，不是纯性能
- 用 microbenchmark 展示 eBPF handler 的延迟 overhead
- 用 iPerf 和 redis 展示实际改善
- **对我们的启示**：eTran 有真实的网络 transport 改变（可以插入 eBPF 改变 RDMA 包处理），效果直接可测。NCCLPol 的改变是"建议"（NCCL 可以接受或忽略），这是根本区别。

### AutoCCL（SC'24）—— 集体通信调优

**硬件**：多节点 GPU 集群（A100）

**评估策略**：
- 核心 claim 是"自动搜索找到比默认更好的配置"
- 有完整的 end-to-end 训练 throughput 对比（tokens/second）
- 能这么做是因为他们**实际改变了 NCCL 的内部行为**（通过 custom collective schedule），而不只是调整了 plugin 参数

**对我们的启示**：AutoCCL 能展示端到端改善，是因为他们改变的是 NCCL 的 kernel schedule，效果直接。NCCLPol 改变的是 tuner plugin 返回的 algo/proto/channels 建议，NCCL 会在此基础上进一步调整，效果间接且难以隔离。

### MSCCL（NSDI'23）—— Custom Collective Schedules

**硬件**：Azure NDv4（8x A100），多节点

**评估策略**：
- 用 hand-written 或 synthesized collective schedule，跑 nccl-tests，对比 default NCCL
- 用 Megatron-LM 训练 GPT-2，测量 tokens/sec
- 有清晰的 speedup 数字（1.2x-2x on specific collective types）

**对我们的启示**：MSCCL 能有明确的性能数字，是因为他们**替换了 NCCL 的 collective schedule**（这是实质性的 kernel 级别改变）。NCCLPol 的 tuner plugin 只是"建议"，NCCL 可以忽略。

---

### Workshop Paper vs. Full Paper 的评估期望差异

**Full paper（OSDI/NSDI/EuroSys）的期望：**
- 端到端 workload（真实或 representative benchmark）
- 至少一个量化的性能改善，有统计显著性
- 大规模（8+ GPU，多节点）
- 对比多个系统

**Workshop paper 的期望（HotNets, MLSys Workshop, etc.）：**
- 机制证明（mechanism works）
- 可信的 motivation（为什么这个问题重要）
- 初步数据（preliminary results，可以是小规模）
- 清晰的 future work 方向
- **不要求端到端性能改善**

**NCCLPol 当前状态与 workshop 期望的对齐度：**
- RQ1（overhead）：完全对齐，数据清晰
- RQ2（safety）：强对齐，这是最核心的 claim
- RQ3（expressiveness）：基本对齐，展示了 mechanism 的表达力
- 缺口：没有任何 policy effectiveness 数据，但 workshop 可以接受，前提是诚实说明这是 future work

---

## 第四部分：优先级排序

### 优先级 1（最重要，必须做）：方案 E — "坏" policy 性能退化演示

**理由：**
- 不需要在当前硬件上展示"policy 改善性能"
- 直接支持安全性核心 claim：bad native plugin 导致性能退化 + crash，eBPF 版本安全拒绝
- 实现简单：写一个故意次优的 native plugin（如强制 1 channel 或强制 wrong algo），测量退化幅度，然后展示等效 eBPF 被拒绝
- 这个实验把 RQ2 和 RQ3 连接起来：验证器不只是防止崩溃，还防止性能退化

**预期价值：** 强化论文最核心的安全+正确性 claim

---

### 优先级 2（较重要）：方案 C — Channel 数量对吞吐量影响

**理由：**
- 在当前硬件上最可能有可测量效果的参数
- 先用 `NCCL_MIN_NCHANNELS` 环境变量验证 1ch vs. 4ch 是否有可测量差异
- 如果有差异（哪怕 10-20%），就可以展示 size-aware channel policy 的实际效果
- 如果没有差异，说明 socket transport 掩盖了效果，这本身也是有意义的结论

**预期价值：** 可能是唯一能展示 policy 实际效果的实验

---

### 优先级 3（有价值）：方案 D — Workload Trace 累积效果

**理由：**
- 不需要单点改善，只需要在混合工作负载下展示累积效果
- 实现简单，只需修改测试脚本
- 即使效果不显著，也能展示 adaptive policy 在 workload-aware 场景下的意义

**预期价值：** 增加 RQ3 的说服力

---

### 优先级 4（可选）：方案 B — Proto 切换效果

**理由：**
- 在大消息下 LL vs. SIMPLE 可能有吞吐量差异
- 先手动测试 `NCCL_PROTO=LL` vs. `NCCL_PROTO=Simple`，如果有差异再做 policy 版本

**预期价值：** 补充性数据，不是核心 claim

---

### 不推荐：方案 A（在当前硬件上找 NCCL 次优选择）

**理由：**
- socket transport 掩盖了 algo 差异，很难找到有统计显著性的次优点
- NCCL 的默认选择对当前硬件已经很好，很难通过 policy 超越它
- 即使找到，也可能只是噪声级别的差异

---

## 总结：论文当前状态的诚实评估

**优势（当前已证明）：**
1. 机制可行：eBPF policy 能集成进 NCCL plugin ABI，不需要修改 NCCL
2. 开销可接受：100ns 级别，是 GPU 集体时延的 0.06%
3. 安全性有效：7 类 bug 在加载时被拦截，不崩溃
4. 热重载正确：7.3ms 全流程，零调用丢失
5. 表达力充足：能表达 SLO、多租户、自适应 channel 等 policy

**缺口（当前没有证明）：**
1. Policy 改善了任何可测量的性能指标
2. Policy 正确选择了比 NCCL 默认更优的 algo/proto
3. SLO enforcer 实际上满足了 SLO
4. Adaptive policy 在真实竞争下比 static policy 更好

**对 workshop 论文的结论：**
现有数据对 workshop 来说是够的，前提是：
1. 论文的 claim 精确限定在"safety"和"expressiveness"，不超出
2. Section 5 开头的那段 disclaimer 要保留（"expressiveness demonstration, not performance improvement"）
3. Conclusion 段的那句话要保留（"mechanism works but does not yet show adaptive policies improve outcomes"）
4. 优先做方案 E（坏 policy 退化演示），它能在当前硬件上提供最有意义的新证据

**对 full paper 的展望：**
要升级为 full paper（OSDI/NSDI），必须有：
- 多节点（至少 4-8 GPU），InfiniBand transport
- 真实训练工作负载（GPT 训练 tokens/sec）
- 在明确次优 NCCL 默认选择的场景下展示 policy 改善
- 多租户场景下的 SLO 满足率对比
