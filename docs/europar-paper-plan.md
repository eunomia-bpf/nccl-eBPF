# Euro-Par Full Paper 扩展规划

**从当前 6 页 workshop paper → Euro-Par 12-14 页 full paper**
日期：2026-03-11

---

## 一、现有论文现状梳理

### 当前 workshop paper 核心内容（6页，ACM sigconf）

| 章节 | 内容摘要 | 当前页数（估算） |
|------|---------|----------------|
| Abstract + Intro | 动机、贡献列表（3 条）、数字摘要 | ~1.5 页 |
| Background & Motivation | NCCL plugin 系统、safety gap、eBPF 三特性 | ~0.7 页 |
| Design | 4 个设计张力(T1-T4)、威胁模型、架构图、policy 编程模型 + 代码示例 | ~1.2 页 |
| Implementation | bpftime 集成、plugin 注册、NCCL integration challenges、热重载、native baseline | ~0.7 页 |
| Evaluation | Overhead 表、Safety/热重载、案例研究（NVLink policy + stability + composability + net plugin） | ~1.5 页 |
| Related Work | Collective comm、alternative extension mechanisms | ~0.3 页 |
| Discussion + Conclusion | 局限性、泛化性、总结 | ~0.3 页 |

### 当前论文三大贡献

1. NCCLbpf：在 NCCL plugin 接口内嵌入 userspace eBPF runtime（不修改 NCCL）
2. 跨 plugin 类型化 map（profiler→tuner 闭环）
3. 在 8x B300 NVLink 上的评估（80-130ns 开销、14/14 verifier、27% 吞吐提升）

---

## 二、新增实验结果

### 实验 A：CPU 竞争类型影响算法选择（cross-boundary eBPF 核心动机）

**关键发现：没有一个算法在所有运行时条件下都最优**

| 干扰类型 | Ring | NVLS | 最优 |
|---------|------|------|------|
| 无干扰 baseline | 319.9 GB/s | 212.9 GB/s | Ring |
| CPU 饱和（stress-ng 240核）| 213.3 GB/s (-33.3%) | 231.2 GB/s (+8.6%) | NVLS |
| cpuset 限制（taskset 4核）| 370.5 GB/s (0%, 免疫) | 46.2 GB/s (-78.3%) | Ring |
| 内存带宽压力 | 无影响 | 无影响 | 无需切换 |
| 两 job 共享 4 核 | ~63 GB/s (严重退化) | — | 需外部干预 |

**解释**：
- CPU 饱和（高 runqueue 竞争）→ Ring 的 proxy thread 被频繁抢占 → NVLS（硬件 multicast）更优
- cpuset 限制（可用核少）→ NVLS 的同步线程缺核 → Ring（线程依赖少）更优
- 内存带宽不是瓶颈（NVLink 直接 GPU-GPU HBM 传输，不经过 CPU DRAM）

### 实验 B：GPU 热降频可观测性

| 指标 | Idle | NCCL 高负载 | 可操作性 |
|------|------|------------|---------|
| SM 时钟 | 2032 MHz | 1650-1972 MHz (SW_THERMAL_SLOWDOWN) | 高 |
| Margin Temperature | ~55°C | ~31°C (距热限制) | 高（先行指标）|
| Power Draw | ~190W | 500-1054W | 中 |
| NVLink TX/RX (DCGM) | 0 | ~47-49 GB/s/GPU | 中 |

**关键**：GPU 3 在 30s 内触发 14 次热降频；SM 频率从 2032→1650 MHz。Margin Temperature 是热降频的先行指标。

### 已否决/排除的方案（诚实声明实验局限）

- NVLink 流量 eBPF：NVLink 绕过内核网络栈，tc/XDP 看不到
- 多 job NVLink 干扰：2x2 GPU disjoint pair 配置无干扰（B300 NVLink 5 足够宽）；需要 4+4 split 才能看到竞争
- AMD PMU 内存带宽：虚拟机限制，不可用

---

## 三、Euro-Par 会议特点分析

### Euro-Par 审稿标准

Euro-Par（Euro-Par: International European Conference on Parallel and Distributed Computing）是欧洲 HPC/并行计算领域的旗舰会议，12-14 页 LNCS 格式。审稿关注：

1. **系统完整性**：full paper 必须有足够的设计深度和评估广度
2. **实验充分性**：需要多个维度的评估，不能只有一个 case study
3. **HPC 相关性**：Euro-Par 偏 HPC 和并行计算，GPU 集合通信非常切题
4. **novelty 明确性**：需要清晰区分与已有 NCCL tuner 工作的差异

### LNCS 格式注意事项

- 不用 ACM sigconf，改用 Springer LNCS 模板（`\documentclass{llncs}`）
- 无 em-dash（`---`），用括号/逗号/分号/分句代替
- 单栏布局，通常 10pt 字体，页面更宽
- 12 页 LNCS ≈ 7-8 页 ACM sigconf 的内容密度（LNCS 内容密度较低）

---

## 四、Full Paper 结构规划（12-14 页）

### 整体叙事框架

**新 thesis**：NCCL 的最优算法选择不仅依赖于消息大小和 GPU 拓扑，还取决于运行时系统状态的类型（CPU 竞争类型、GPU 热状态）。没有一个静态配置在所有运行时条件下都最优。NCCLbpf 通过 cross-boundary eBPF 将内核级系统观测（CPU 调度器状态、GPU 热状态）与 NCCL userspace policy 决策统一在同一个 eBPF 框架中，实现动态、可验证、可热重载的运行时治理。

**贡献升级**（workshop 3 条 → full paper 4 条）：
1. NCCLbpf 框架（不变，但更详细）
2. 跨 plugin 类型化 map 和 composability 机制（不变，但有新实验）
3. Cross-boundary eBPF：内核-userspace pinned map 数据流，统一两侧 eBPF 信号
4. 全面评估：overhead、safety、NVLink 性能、CPU 竞争响应、GPU 热感知，plus 对比 static config

---

### Section 详细规划

#### 1. Introduction（~1.5 页）

**保留**：整体框架、NCCL plugin 问题、eBPF structural analogy
**扩展**：
- 新增具体的动机场景：**算法选择的多条件依赖性**。用一个具体场景开场：同样的 8M AllReduce，在空闲时 Ring 比 NVLS 快 27%，在 CPU 饱和时 Ring 慢 33%，在核心受限时 NVLS 慢 78%。"一个 NCCL 环境变量无法应对所有条件"是关键 hook。
- 扩展贡献列表到 4 条（加入 cross-boundary eBPF 贡献）
- 更新数字以反映新实验
- 段落结构：Background pain → Structural analogy → Our approach → Contributions

**新增内容量**：约 +0.3 页

---

#### 2. Background and Motivation（~2 页，从 ~0.7 页扩展）

**保留**：NCCL plugin system 描述、safety gap、eBPF 三特性
**扩展**（关键扩展节）：

2.1 NCCL Plugin Architecture（保留，细化）
- 详细描述 4 种 plugin（tuner v2-v5 演化、profiler v1/v6、net、env）
- 说明 plugin 独立性问题（无跨 plugin 通信）

2.2 The Safety Gap（保留）
- 生产故障案例（inspector use-after-free、profiler segfault、Llama3 466 中断）

2.3 eBPF as Policy Mechanism（保留，细化）
- eBPF 三特性
- 新增：kernel eBPF vs userspace eBPF (bpftime) 的区别
- 新增：pinned BPF map 作为跨边界共享状态的机制

2.4 The Dynamic Optimality Gap（**全新小节，约 0.8 页**）
- **核心动机**：展示 CPU 竞争实验的关键数据（小型表格），证明没有一个算法在所有条件下最优
- 展示：无干扰时 Ring 最优，CPU 饱和时 NVLS 最优，cpuset 限制时 Ring 最优
- 说明：`NCCL_ALGO=Ring` 这类静态环境变量无法应对动态条件
- 说明：NCCL tuner plugin 在 getCollInfo 时只看到消息大小和 rank 拓扑，看不到内核调度状态
- 说明：GPU 热降频实测数据（SW_THERMAL_SLOWDOWN，频率 2032→1650 MHz）
- 结论：需要一个能感知跨边界系统状态的动态 policy 机制

**页数估算**：~2 页（从 0.7 页扩展到 2 页）

---

#### 3. Design（~2.5 页，从 ~1.2 页扩展）

**保留**：4 个设计张力（T1-T4）、威胁模型框架、架构图
**扩展**：

3.1 Design Tensions（保留 T1-T4，可适当压缩）

3.2 Threat Model（保留，略扩展：multi-tenant 场景下 cluster admin 部署 kernel observer，user 运行 NCCL policy，两者角色分离）

3.3 Architecture（扩展为核心 section，约 1 页）
- 保留：userspace 架构图（profiler-tuner closed loop）
- **新增**：cross-boundary 架构图（内核 sched_switch eBPF → pinned map → bpftime kernel-user map → NCCL tuner policy）
- 说明三层：(1) 内核 eBPF 观测层（不可替代：跨进程视角，sched tracepoint/uprobe），(2) pinned BPF map 共享层（kernel-user array map，BPF_F_MMAPABLE，mmap 零拷贝 <10ns），(3) userspace bpftime policy 执行层（综合多信号，per-collective 热路径）
- 说明为何双侧 eBPF 不可替代（用户态进程看不到跨进程调度状态；内核只观测，不能在 NCCL 热路径执行 per-collective 决策）

3.4 Policy Programming Model（保留，略扩展）
- 保留代码示例（profiler-tuner closed loop）
- **新增**：跨边界 policy 示例（读取 cpu_state map + gpu_state map + size-aware 逻辑的综合 policy）
- 说明 helper whitelist（eBPF 验证器控制 helper 访问，kernel-user map 读取为 allowed helper）

**页数估算**：~2.5 页

---

#### 4. Implementation（~1.5 页，从 ~0.7 页扩展）

**保留**：bpftime 集成、plugin 注册、NCCL integration challenges（cost array 机制、communicator ID）、热重载机制、native baseline

**新增**：

4.5 Kernel eBPF Components（约 0.5 页）
- 内核 sched_switch tracepoint 程序：区分 CPU 饱和（全局 runqueue 长，即多进程竞争同一 CPU）vs cpuset 限制（单进程可用核少）的实现
- uprobe on NVML（`nvmlDeviceGetClockInfo` / `nvmlDeviceGetCurrentClocksThrottleReasons`）：捕获 SM 频率和热降频标志
- pinned BPF map 创建（`BPF_F_MMAPABLE`）和 plugin 端注册流程

4.6 Kernel-User Map Integration（约 0.3 页）
- bpftime 的 kernel-user array map（零拷贝 mmap 路径 vs syscall 路径的选择）
- plugin.cpp 中的 map 生命周期管理

**页数估算**：~1.5 页

---

#### 5. Evaluation（~4 页，从 ~1.5 页大幅扩展）

**测试环境**（统一描述）：
- CPU 微基准：240-core AMD EPYC 9575F
- GPU 实验：8x NVIDIA B300 SXM6，NVLink 5 (NV18, 1.8 TB/s/GPU)，CUDA 13.0，NCCL 2.29.7
- kernel eBPF 实验：内核 6.8，BTF 支持，bpftool 7.4

**5.1 Overhead（保留，约 0.5 页）**
- 保留 Table 1（CPU 微基准，80-130ns）
- 保留 GPU 端开销段落（NVLink 小消息 ~1.3us/4%，大消息不可测）
- **新增**：kernel-user map 读取开销（BPF_F_MMAPABLE mmap 路径 <10ns；作为 policy 信号获取的额外开销）

**5.2 Safety and Hot-Reload（保留，约 0.5 页）**
- 保留 14/14 verifier 矩阵
- 保留 native vs eBPF crash 对比（SIGSEGV vs VERIFIER REJECT）
- 保留热重载数字（9.4ms load, 1.07us swap, 0 lost calls）
- 说明：cross-boundary policy 中内核 eBPF 程序加载也经过 kernel verifier（双重验证）

**5.3 NVLink Performance Policy（保留，约 0.7 页）**
- 保留 Figure 1（AllReduce 带宽：Default vs eBPF policy vs bad_channels）
- 保留 Table 2（algorithm sweep：NVLS vs Ring 在各 size 的比较）
- 保留稳定性实验（20-run AllGather，CV 0.15% vs 0.10%）
- 保留 bad_channels 退化结果（87-95%，说明 policy 有真实控制力）

**5.4 Profiler-to-Tuner Composability（保留+扩展，约 0.5 页）**
- 保留当前 composability 结果（adaptive channels：tuner-only 3.84 GB/s vs mixed 4.49 GB/s，+16.9%）
- 保留三阶段演示（baseline→contention→recovery）

**5.5 CPU Interference Response（全新，约 1 页）**
- **实验设计**：stress-ng CPU 饱和实验（240 核）和 taskset cpuset 限制实验（4 核）
- **对比三种配置**：
  1. NCCL default（全程 NVLS，无干预）
  2. Static optimal（手动设置 `NCCL_ALGO=Ring` 或 `NCCL_ALGO=NVLS`，但静态选择无法兼顾两种条件）
  3. NCCLbpf cross-boundary policy（内核 sched_switch 感知干扰类型，动态切换）
- **关键展示**：cross-boundary policy 在两种干扰条件下均优于任一静态配置
  - CPU 饱和时：动态切换 Ring→NVLS，性能从 213 GB/s（Ring degraded）恢复到 231 GB/s（NVLS immune）
  - cpuset 限制时：保持 Ring（NVLS 降至 46 GB/s，Ring 免疫）
- **动态切换演示**：注入 CPU 压力→ policy 检测→切换算法→压力解除→恢复默认选择（时间序列图）
- 说明：`NCCL_ALGO=NVLS` 在 cpuset 场景反而有害；只有动态 policy 才能兼顾

**5.6 GPU Thermal Awareness（全新，约 0.5 页）**
- **实验设计**：持续 256M-1G AllReduce 高负载触发 SW_THERMAL_SLOWDOWN
- **可观测性验证**：展示 SM 时钟从 2032→1650 MHz，throttle reason bitmask = 0x20，Margin Temperature 从 55→31°C
- **uprobe 机制验证**：内核 uprobe on NVML 成功捕获 throttle 事件（14 次/30s on GPU 3）
- **policy 效果**：检测到热降频时降低 aggressiveness（减少 nChannels），展示可以减少后续 throttle 触发频率
- 说明 Margin Temperature 作为先行指标的优势（比原始温度更具操作性）
- 诚实说明局限：uprobe 实现在 PoC 阶段，全量端到端实验为 future work

**页数估算**：~4 页

---

#### 6. Related Work（~1 页，从 ~0.3 页扩展）

**重组为四个段落**（workshop 版只有两段，full paper 需要四段）：

6.1 Collective Communication Optimization
- MSCCL、TACCL（静态 schedule synthesis）
- AutoCCL NSDI'25（自动化调优，但静态配置，无动态 policy）
- OptiReduce NSDI'25（fault resilience，transport 层，与我们互补）
- Demystifying NCCL（NCCL 内部分析，背景参考）
- **差异**：上述工作关注 static 最优配置或 transport 改造；NCCLbpf 是在不修改 NCCL 的前提下实现 dynamic per-collective policy

6.2 eBPF for Distributed Systems
- Electrode NSDI'23（eBPF 加速分布式协议快路径）
- DINT NSDI'24（eBPF 加速分布式事务）
- eTran NSDI'25（eBPF 内核 transport extensibility）
- XRP OSDI'22（eBPF 内核 storage extension）
- PageFlex ATC'25（eBPF paging policy）
- BOAD eBPF@SIGCOMM'24（广播/聚合 in-kernel 化）
- ALEPH、XAgg（eBPF 梯度聚合，加速 communication 本身，而非 policy plane）

6.3 eBPF for GPU Infrastructure
- gpu_ext（arXiv 2025，GPU driver/device layer policy，最相关）
- eACGM IWQoS'25（eBPF 对 NCCL/GPU 的全栈 observability，只观测不 enforce）
- eInfer eBPF@SIGCOMM'25（distributed LLM inference tracing）
- Host-Side Telemetry（arXiv 2025，GPU 诊断，complementary）
- **差异**：上述工作在 OS/driver 层或只做 tracing；NCCLbpf 在 communication library 层做 enforce

6.4 Alternative Extension Mechanisms
- WebAssembly（沙箱但无 eBPF static 安全保证）
- Out-of-process services（IPC 开销 >us 量级）
- Intel MPK（内存隔离，无 verification）
- 说明 eBPF 是唯一同时提供 static verification + near-native JIT 的机制

**基于新 related work survey 的引用更新**：
新增引用：AutoCCL NSDI'25、eTran NSDI'25、UCCL CoRR'25、MSCCL++ CoRR'25、eACGM IWQoS'25、eInfer eBPF@SIGCOMM'25、Demystifying NCCL arXiv'25、gpu_ext CoRR'25、Darzi et al. CoRR'25

**页数估算**：~1 页

---

#### 7. Discussion（~0.7 页，从 ~0.3 页扩展）

**保留**：env plugin 扩展、RDMA future work、多节点限制
**新增**：

- **Deployment model for cross-boundary eBPF**：内核 eBPF 组件（sched_switch observer、NVML uprobe）由 cluster admin 部署，NCCL policy 由用户运行。这与 Linux eBPF 现有部署实践（例如 Cilium、Tetragon）一致，无需修改 NCCL。

- **Multi-tenant policy coordination**：当前 cross-boundary 实验使用单进程注入干扰（stress-ng）来模拟多租户 CPU 竞争。真实多租户场景（多 job 跨节点 InfiniBand）需要更复杂的协调机制，是 future work。

- **Generalization**：AMD RCCL 有相似 plugin 架构；MPI 实现（Open MPI、MPICH）有 MPIT 接口同样可以接入 eBPF policy。eBPF-for-extensions 模式在 HPC 通信库中有广泛应用潜力。

- **Limitations**：
  - 单节点（无 InfiniBand/RoCE），多节点实验为 future work
  - stress-ng 模拟的 CPU 竞争不完全代表生产 K8s 多租户场景
  - GPU thermal policy 目前为 PoC，全量实验待完成

---

#### 8. Conclusion（~0.4 页，略扩展）

升级结论以反映新贡献：NCCLbpf 通过 cross-boundary eBPF 实现了从"静态配置调优"到"动态运行时治理"的范式转变。内核 eBPF 提供用户态不可获取的系统级信号，userspace bpftime policy 在 NCCL 热路径执行经过验证的 per-collective 决策，两者通过 pinned BPF map 以 <10ns 延迟共享状态。

---

## 五、页数分配汇总

| Section | Workshop 版（估算）| Full Paper 目标 |
|---------|-----------------|----------------|
| Abstract | 0.25 页 | 0.25 页 |
| 1. Introduction | 1.3 页 | 1.5 页 |
| 2. Background & Motivation | 0.7 页 | 2.0 页 (+1.3) |
| 3. Design | 1.2 页 | 2.5 页 (+1.3) |
| 4. Implementation | 0.7 页 | 1.5 页 (+0.8) |
| 5. Evaluation | 1.5 页 | 4.0 页 (+2.5) |
| 6. Related Work | 0.3 页 | 1.0 页 (+0.7) |
| 7. Discussion | 0.15 页 | 0.7 页 (+0.55) |
| 8. Conclusion | 0.15 页 | 0.4 页 (+0.25) |
| References | ~0.7 页 | ~1.1 页 |
| **合计** | **~7 页（含 ref）** | **~14-15 页** |

注：LNCS 格式单栏，内容密度低于 ACM sigconf 双栏。当前 6 页 ACM sigconf 约等于 LNCS 8-9 页；扩展到 LNCS 13-14 页需要净增约 5-6 页新内容。

---

## 六、需要新制作的图表

### 新图 1：Cross-Boundary eBPF Architecture（取代或补充现有架构图）
- 展示三层：内核 eBPF（sched_switch/uprobe）→ pinned BPF map → userspace bpftime policy
- 标注数据流方向和延迟（kernel write <1us，mmap read <10ns）
- 格式：TikZ，与现有架构图风格一致

### 新图 2：CPU 竞争类型对算法选择的影响（核心 motivation 图）
- 条形图或折线图：Ring vs NVLS 在三种条件下（无干扰/CPU饱和/cpuset限制）的对比
- 突出"没有一个算法在所有条件下最优"
- 格式：pgfplots，与现有图风格一致

### 新图 3：Dynamic Algorithm Switching（cross-boundary policy 的时间序列演示）
- X 轴：时间/collective 调用序号，Y 轴：吞吐量（GB/s）
- 展示三段：无压力（Ring）→ 注入 CPU 饱和（自动切换 NVLS）→ 压力解除（恢复 Ring）
- 格式：pgfplots 时间序列

### 新图 4：GPU Thermal Signals
- 双 Y 轴图：SM 时钟频率（左轴）+ Throttle 事件（右轴标记）随时间变化
- 展示 idle → NCCL 高负载 → throttle 触发的全过程
- 可选：加入 Margin Temperature 曲线作为先行指标

### 现有图表调整
- Figure 1（AllReduce 带宽）：保留，可扩展 x 轴范围或加更多数据点
- Table 1（CPU 微基准）：保留，可考虑加入 kernel-user map 读取开销行
- Table 2（Algorithm sweep）：保留

---

## 七、需要补充的实验

### 必须做（论文核心数据）

1. **cross-boundary eBPF 端到端实验**（Section 5.5 核心）：
   - 实现内核 sched_switch eBPF 程序，写 pinned map
   - 修改 plugin.cpp 支持 kernel-user map 读取
   - 实现 cpu_aware_policy.bpf.c（综合 cpu_state + size-aware 逻辑）
   - 对比：NCCL default vs static Ring vs static NVLS vs NCCLbpf dynamic policy
   - 条件：无干扰 baseline、CPU 饱和（stress-ng）、cpuset 限制（taskset）
   - 目标：展示 dynamic policy 在两种条件下均优于最优静态选择

2. **kernel-user map 读取开销测量**（Section 5.1 补充）：
   - 测量 BPF_F_MMAPABLE mmap 路径的读取延迟（预期 <10ns）
   - 对比 syscall 路径（预期 1-5us）
   - 集成到现有微基准表

3. **Dynamic switching time series**（Section 5.5 图）：
   - 运行 NCCL workload，中途注入/撤除 stress-ng
   - 记录 policy 切换时机（通过 map 写入的 cpu_state 变化）和 throughput 变化
   - 展示 policy 响应延迟（从状态变化到 algorithm 切换的时间）

### 可选做（加强论文，但若时间不足可作为 future work）

4. **GPU thermal awareness 端到端**（Section 5.6）：
   - 实现 uprobe on NVML（`nvmlDeviceGetClockInfo`）
   - 当检测到 SW_THERMAL_SLOWDOWN 时降低 nChannels
   - 展示 throttle 触发频率降低（GPU 更快散热/恢复）

5. **Composability B300 版本**（将 composability 实验升级到 B300）：
   - 当前 composability 实验在 RTX 5090 单 GPU Socket 传输上，数字较小
   - 在 B300 NVLink 上重做，展示更大规模下的 profiler→tuner 闭环效果

---

## 八、待解决的关键问题

### 技术问题

1. **plugin.cpp kernel-user map 支持**：需要在 plugin.cpp 中实现注册 BPF_F_MMAPABLE map 的接口，并在 eBPF policy 中作为 map 访问。已评估工作量有限，需实现。

2. **sched_switch 区分两种干扰类型**：
   - CPU 饱和：全局 runqueue 长（`nr_running` 高），可通过 `tracepoint/sched/sched_wakeup` 或 `tracepoint/sched/sched_switch` + 滑动窗口检测
   - cpuset 限制：当前进程 cpuset 可用核数（`task_cpu_allowed_count`），需要读取 task struct 的 cpus_mask
   - 需要验证当前内核（6.8）是否允许在 tracepoint BPF 中读取 task->cpus_mask（CO-RE）

3. **pinned map 路径约定**：需要约定 `/sys/fs/bpf/ncclbpf/` 路径，并在 plugin 启动时检测（已有则 mmap，无则用 fallback）

### 论文写作问题

4. **stress-ng 代表性质疑的应对**：审稿可能质疑 stress-ng 制造的竞争不代表生产场景。回应策略：(1) stress-ng --cpu 240 模拟"过度 provisioning 的多租户集群"，taskset 模拟 K8s cpuset cgroup，两种模型都有实际对应场景；(2) 核心贡献是架构和机制，实验验证原理；(3) 实际部署中内核 eBPF 可以观测真实 K8s cgroup 调度事件。

5. **"为何需要双侧 eBPF"的说服力**：审稿可能说"为什么不直接用普通 C 读取 /proc/schedstat？"。回应：(1) /proc 需要文件 I/O，不适合 per-collective 热路径；(2) 内核 eBPF 能在 sched_switch 时刻原子地写入 mmapable map，用户态无需系统调用；(3) 统一的 eBPF 编程模型（内核+用户态同一 IR，同一验证器语义）是系统设计的一致性。

---

## 九、与 workshop 版本的差异对照

| 维度 | Workshop 版（6页）| Full Paper 版（12-14页）|
|------|-----------------|------------------------|
| 核心问题 | NCCL plugin 不安全 | NCCL policy 无法适应动态系统状态 |
| 主要动机 | Safety（验证器防崩溃） | Safety + Dynamic Optimality（no single best algorithm）|
| eBPF 范围 | Userspace only（bpftime） | Cross-boundary（kernel + userspace）|
| Policy 信号 | 消息大小 + profiler 延迟 | 消息大小 + profiler 延迟 + CPU 竞争类型 + GPU 热状态 |
| Evaluation 维度 | 4 个（overhead/safety/NVLink policy/composability）| 6 个（+CPU 干扰响应/GPU 热感知）|
| 贡献数 | 3 条 | 4 条 |
| Related work 段数 | 2 段 | 4 段 |
| 图表数 | 1 图 + 2 表 | 3-4 图 + 2-3 表 |
| 格式 | ACM sigconf 双栏 | LNCS 单栏 |

---

## 十、写作规则提醒（来自 CLAUDE.md）

- 无 em-dash（`---`）：用括号、逗号、分号或分句代替
- "safety" not "correctness"：eBPF 验证 memory safety + termination，不是 semantic correctness
- 无 "in-kernel"：我们用 userspace eBPF（bpftime），不是 kernel eBPF；当谈到内核侧的 eBPF 程序时，明确说 "kernel-side eBPF program" 或 "kernel eBPF"
- 对 cross-boundary 部分：内核 eBPF 程序（sched_switch/uprobe）是真正的 kernel eBPF；userspace policy 是 bpftime。要区分清楚，避免混淆。

---

## 十一、建议的写作顺序

1. 先做 cross-boundary 实验（必须做，Section 5.5）
2. 更新 Section 2.4（Dynamic Optimality Gap）写入实测数据
3. 写 Section 3.3（cross-boundary architecture）和新架构图
4. 写 Section 4.5（Kernel eBPF Components）和 Section 4.6
5. 写 Section 5.5 和 5.1 新内容（kernel-user map 开销）
6. 更新 Section 6（Related Work，扩展引用）
7. 更新 Abstract、Intro、Conclusion
8. 转换格式（ACM sigconf → LNCS）
