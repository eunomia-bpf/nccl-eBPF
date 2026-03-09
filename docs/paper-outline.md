# NCCLPol：基于 eBPF 的 GPU 集合通信安全可扩展策略框架

## 论文定位与核心叙事

### 一句话定义

NCCLPol 把 eBPF 的安全可扩展模型（验证 + JIT + 隔离 + 热更新）引入 GPU 集合通信运行时，使平台方能以**受验证的 eBPF 程序**（而非任意 native 共享库）扩展 NCCL 的行为——在保证安全性和可控开销的前提下，实现 SLO 保障、故障自愈、资源干扰控制等治理能力。

### 核心贡献是什么（不是什么）

**核心贡献 = eBPF 运行时框架本身 + 它在集合通信 domain 的适配设计。**

Policy use case（SLO、故障恢复、干扰控制）是框架表达力的**展示**，不是独立贡献——因为这些逻辑用 native C++ plugin 同样能写。真正的贡献在于：

1. **为什么要用 eBPF 而不是 native plugin**：验证、隔离、热更新、可审计、可组合——这不是"可选的锦上添花"，而是使集合通信 policy 能在生产环境真正部署的**必要条件**
2. **为什么这个 domain 有独特挑战**：μs 级热路径的开销容忍度、多维动作空间的 eBPF 表达、跨插件状态共享的设计、$100K/hr GPU 集群的失败代价
3. **框架的 domain-specific 设计**：NCCL 的 policy hook 模型、eBPF helper/maps ABI、telemetry 闭环架构

### 类比定位

| 系统 | 做的事 | 我们的关系 |
|---|---|---|
| Linux eBPF | 内核模块 → 受验证的 eBPF 程序 | 同样的范式转变 |
| bpftime/EIM (OSDI'25) | 通用用户态安全扩展框架 | 我们的执行基础设施 |
| gpu_ext | eBPF policy 用于 GPU driver/device 层 | 互补：driver 层 vs collective runtime 层 |
| XRP (OSDI'22) | eBPF 用于存储热路径 | 同类论文：把 eBPF 带入新的性能敏感 domain |
| Electrode (NSDI'23) | eBPF 用于分布式协议快路径 | 同类论文 |
| **NCCLPol (ours)** | **eBPF 用于 GPU 集合通信治理** | 首次 |

### 为什么 native plugin 不够——安全作为核心论点

NCCL 已有 dlopen .so 插件机制，但这恰恰是问题所在：

| 维度 | Native .so Plugin | eBPF Policy (NCCLPol) |
|---|---|---|
| **故障隔离** | 插件 bug 可 crash/hang 整个训练进程 | 有界执行保证，验证时排除无限循环/OOB |
| **热更新** | dlclose 正在执行的 .so = UB | eBPF 程序原子替换，零宕机 |
| **多方共存** | 多团队的 policy 共享地址空间，互相可破坏 | MPK 硬件隔离，每个 policy 独立内存域 |
| **审计** | 不透明二进制，无法在部署前验证行为 | eBPF 字节码可静态分析，验证器保证属性 |
| **部署模型** | 需编译/链接/版本管理，平台碎片化 | 小字节码文件，统一下发，跨 NCCL 版本兼容 |
| **权限控制** | 全进程权限，可调用任意函数/读写任意内存 | EIM 最小权限：只能调用注册的 helper，只能访问声明的 state |
| **开销可控性** | 无保证——复杂逻辑可能引入不可预测延迟 | 验证器限制指令数 + JIT 优化，开销上限可证明 |

**关键论点**：在 GPU 集群这种**失败代价极高、运行周期极长、多方协作**的场景，"信任 native code"不是一个可持续的扩展模型。eBPF 验证+隔离不是可选的性能优化，而是**使 collective 通信可扩展的前提**——就像 Linux 需要 eBPF 而不是让所有人写内核模块一样。

---

## Abstract（中文）

GPU 集合通信库（如 NCCL）是分布式深度学习的核心基础设施。NCCL 近期的插件系统（tuner、network、environment、profiler）暴露了丰富的 per-collective 决策点，但这些插件以 native 共享库形式加载，缺乏安全验证、故障隔离、和热更新能力——这使得平台运维方难以在生产 GPU 集群中安全地部署和演进通信治理策略。

本文提出 NCCLPol，一个基于 eBPF 的集合通信安全可扩展策略框架。NCCLPol 将 eBPF 的安全扩展模型（加载时验证、LLVM JIT 编译、MPK 硬件隔离、原子热更新）引入 NCCL 的插件体系，使平台方以受验证的 eBPF 程序——而非任意 native 代码——扩展集合通信行为。NCCLPol 以单个 NCCL 插件共享库部署，内嵌 bpftime 用户态 eBPF 运行时，同时实现 tuner、net、env、profiler 四种插件接口；通过 eBPF maps 实现跨插件状态共享和 profiler-to-tuner 闭环反馈——这是 NCCL 分离式 stock 插件无法做到的。

我们基于 NCCLPol 框架实现了三个治理策略：多租户 SLO 保障、故障检测与自动恢复、计算-通信 SM 干扰控制，以展示框架的表达力和实用性。评估表明，NCCLPol 的 per-collective policy 开销 < 200 ns（collective 操作本身 10-1000 μs），验证在加载时完成无运行时安全检查开销；SLO 策略将竞争场景下 P99 延迟波动降低 X 倍，故障恢复策略在 Y ms 内恢复通信且无需重启训练进程，干扰控制策略提升端到端训练吞吐 Z%。

---

## 论文结构

### 1. Introduction（2 页）

**开篇**：GPU 集群是共享基础设施。NCCL 虽然高性能，但不可安全扩展——插件是 native .so，bug 可以 crash 训练、无法热更新、无法审计。这与 Linux 内核在 eBPF 出现前面临的困境一样：想扩展就得加载内核模块，但内核模块是不安全的。

**问题**：
- NCCL 插件系统已暴露丰富的决策点（tuner v5 cost table、net v11 NIC 选择、profiler 事件）
- 但 native .so 插件模型不支持：故障隔离、安全热更新、多 policy 共存隔离、行为验证
- 现有工作要么 fork NCCL（AutoCCL）、要么替换 NCCL（MSCCL++）、要么只做观测（eACGM）
- 没有工作把 eBPF 安全扩展模型引入集合通信

**贡献**：
1. **NCCLPol 框架**：首个将 eBPF 安全扩展模型（验证+JIT+隔离+热更新）引入 GPU 集合通信运行时的系统
2. **NCCL policy 编程模型**：定义 hook 事件、上下文结构、动作空间、helper ABI、eBPF maps 布局——domain-specific 的设计
3. **跨插件融合架构**：单 .so 实现四种 NCCL 插件，通过 eBPF maps 实现 profiler→tuner 闭环和跨层状态共享
4. **三个治理策略实例 + 评估**：证明框架在 μs 级热路径上开销可忽略，且能表达有实际价值的治理逻辑

### 2. Background & Motivation（2.5 页）

#### 2.1 NCCL 集合通信与插件系统
- Collective 调用路径和决策点
- 四种插件类型的 API 概述（tuner v5、net v11、env v1、profiler v6）
- Mixed plugin 机制（一个 .so 实现多种插件）

#### 2.2 Native 插件的安全问题（核心 motivation）

**这一节是论文的关键——必须让 reviewer 相信 native plugin 模型在 GPU 集群场景下是不够的。**

论据：
- **失败代价**：一个 tuner plugin bug 导致选错 algo → 单次 collective 超时 → 整个 communicator hang → N 个 GPU 空转。大规模集群（数千 GPU）单小时成本 $100K+，一次 hang 可能浪费数小时。
- **运行周期**：训练跑数天/数周。Plugin 需要更新（bug fix、策略调整），但 dlclose + dlopen 在运行中不安全，唯一选择是重启训练进程——代价巨大。
- **多方协作**：基础设施团队想控制 NIC 选择，调度团队想控制 SM 预算，可靠性团队想加故障检测。三个团队的 native 代码在同一地址空间，无隔离。
- **审计需求**：云厂商需要在部署前验证 plugin 行为（不能 panic、不能死循环、不能读写越界）。Native binary 无法提供这种保证。

对比论据：Linux 内核模块 vs eBPF。内核模块也是"用户态编译、内核态加载"的 native code，同样面临上述所有问题。eBPF 的出现使得内核扩展变得安全可控。**NCCL 的 native plugin → eBPF policy 是同样的范式转变。**

#### 2.3 现有工作的不足
- AutoCCL (NSDI'25)：需 fork NCCL ABI，是 native code，无安全保证
- MSCCL/MSCCL++：替换 NCCL，不治理现有 NCCL
- eACGM (IWQoS'25)：eBPF 观测 NCCL，不做 policy 执行
- gpu_ext：GPU driver 层 policy，不涉及 collective 语义
- bpftime/EIM (OSDI'25)：通用框架，未适配 NCCL domain

**空白点**：没有工作把 eBPF 安全扩展引入 collective communication 并解决 domain-specific 的设计挑战。

### 3. Design（4 页）

#### 3.1 总体架构

```
┌──────────────────────────────────────┐
│        NCCLPol Plugin (.so)          │
│                                      │
│  ┌──────┐ ┌──────┐ ┌─────┐ ┌──────┐│
│  │Tuner │ │ Net  │ │ Env │ │Prof. ││
│  │Adapt.│ │Adapt.│ │Adpt.│ │Adapt.││
│  └──┬───┘ └──┬───┘ └──┬──┘ └──┬───┘│
│     └────────┼────────┘       │    │
│         ┌────┴────┐           │    │
│         │ bpftime │◄──────────┘    │
│         │ Runtime │  telemetry     │
│         ├─────────┤                │
│         │  eBPF   │ ← 验证+JIT    │
│         │Programs │ ← MPK隔离     │
│         ├─────────┤ ← 原子热更新  │
│         │  eBPF   │                │
│         │  Maps   │ ← 跨插件共享  │
│         └─────────┘                │
└──────────────────────────────────────┘
```

关键设计决策：
- bpftime 作为**库**链接（不是 LD_PRELOAD），嵌入 NCCL plugin .so
- 所有插件 adapter 共享同一个 bpftime 实例和 eBPF maps 空间
- Policy 程序通过 helper 函数访问 NCCL 上下文，不直接访问 NCCL 内存

#### 3.2 安全执行模型（核心设计贡献）

**这一节详细阐述为什么 eBPF 的安全属性对 collective communication domain 特别重要，以及如何实现。**

##### 3.2.1 加载时验证
- PREVAIL 验证器检查：有界执行（指令数上限，保证 policy 不会在热路径上挂住）、内存安全（无 OOB）、helper 白名单（policy 只能调用注册的函数）
- **Domain-specific 验证约束**：例如限制 tuner policy 的指令数上限为 N（保证 per-collective 开销 < 200ns），限制 fault policy 更宽松（因为不在热路径上）
- 验证在加载时完成 → 运行时零安全检查开销

##### 3.2.2 硬件隔离（MPK）
- 多 policy 共存时，每个 policy 的内存通过 Intel MPK 标记不同 protection key
- WRPKRU 切换开销：~11-260 cycles（可忽略，相比 collective 操作的数万 cycles）
- 保证一个 policy 的 bug 不会破坏另一个 policy 或 NCCL 本身的内存

##### 3.2.3 原子热更新
- 新 policy 加载 → 验证 → JIT → 原子替换函数指针
- 正在执行的旧 policy 自然完成当前调用后释放
- 对比：dlclose native .so 在多线程环境下是 undefined behavior

##### 3.2.4 最小权限（EIM 能力模型）
- 每个 policy 声明它需要的能力（读哪些 state、调哪些 helper）
- 验证器在加载时强制执行能力约束
- 例：观测型 policy 只能读 telemetry、不能修改 cost table；治理型 policy 可以读写

#### 3.3 Policy 编程模型

##### 3.3.1 Hook 事件类型
| 事件 | 触发时机 | 对应 NCCL 插件 API | 热路径？ |
|---|---|---|---|
| `COMM_INIT` | communicator 创建 | tuner `init` / net `init` | 否 |
| `COLL_DECIDE` | 每次 collective 决策 | tuner `getCollInfo` | **是** |
| `CONN_SETUP` | 网络连接建立 | net `connect`/`accept` | 否 |
| `TELEMETRY` | collective 完成事件 | profiler 回调 | 否 |
| `FAULT` | 超时/异常检测 | profiler + 内部检测 | 否 |

##### 3.3.2 上下文结构（eBPF 程序的输入）
```c
struct nccl_policy_ctx {
    // 来自 tuner getCollInfo
    uint32_t coll_type;     // AllReduce, AllGather, ...
    uint64_t n_bytes;       // 消息大小
    uint32_t n_ranks;       // communicator 大小
    uint32_t n_nodes;
    uint64_t comm_id;
    int      reg_buff;      // 是否支持 registered buffer
    // cost table 指针（通过 helper 访问）
    // telemetry 数据（通过 map lookup 访问）
};
```

##### 3.3.3 动作空间（eBPF 程序的输出）
```c
struct nccl_policy_action {
    float cost_table_overrides[NCCL_NUM_ALGO][NCCL_NUM_PROTO]; // -1 = 不修改
    int   n_channels;       // -1 = 不覆盖
    int   nic_mask;          // bit mask 选择 NIC 子集
    int   fault_action;      // 0=none, 1=revoke, 2=shrink
};
```

##### 3.3.4 Helper 函数（policy 可调用的 API）
```c
// Telemetry 读取
int bpf_nccl_get_p99_latency(uint64_t comm_id, uint32_t coll_type);
int bpf_nccl_get_error_count(uint64_t comm_id);
int bpf_nccl_get_bandwidth(uint64_t comm_id, uint32_t algo, uint32_t proto);

// Map 操作
void *bpf_map_lookup_elem(struct bpf_map *map, const void *key);
int bpf_map_update_elem(struct bpf_map *map, const void *key, const void *value, uint64_t flags);

// 日志
int bpf_trace_printk(const char *fmt, ...);
```

#### 3.4 跨插件融合与闭环反馈

**为什么 stock 分离插件做不到**：NCCL 的 tuner、net、profiler 是三个独立 dlopen 的 .so（即使 mixed plugin 共享 .so，NCCL 也不提供标准跨插件通信 API）。

**NCCLPol 的解决方案**：
- 所有 adapter 在同一进程内共享 bpftime 的 eBPF maps
- Profiler adapter 在 collective 完成时将 telemetry 写入 maps
- Tuner adapter 的 `getCollInfo` 调用 eBPF policy 时，policy 读 maps 获取历史统计
- 形成闭环：observe → decide → act → observe

```
NCCL collective 完成
    ↓
Profiler adapter → 写入 telemetry_map（延迟、带宽、错误）
    ↓
下一次 collective 调用
    ↓
Tuner adapter → 调用 eBPF policy
    ↓
Policy 读 telemetry_map → 根据历史统计决策 → 输出 action
    ↓
Tuner adapter → 应用 action（修改 cost table、nChannels）
```

### 4. Implementation（2 页）

#### 4.1 Mixed Plugin 实现
- 导出符号：`ncclTunerPlugin_v5`、`ncclNetPlugin_v11`、`ncclEnvPlugin_v1`、`ncclProfiler_v6`
- 初始化顺序：env（最早，process-global）→ net → profiler → tuner
- 参考 NCCL upstream `plugins/mixed/example/plugin.c`

#### 4.2 bpftime 集成
- 编译为静态库链接到 .so（避免 LD_PRELOAD 对 CUDA 的干扰）
- Policy 从配置路径加载（支持 inotify 监听变更）
- LLVM JIT 编译（AOT 也可选，减少加载时间）
- MPK domain 分配策略（16 domains 上限下的复用）

#### 4.3 Net Adapter 的 passthrough 设计
- NCCLPol 不替换底层网络传输——它**包装** NCCL 的内部 IB/Socket 实现
- Net adapter 在 `getProperties` 中根据 policy 调整可见 NIC 子集和属性
- `connect`/`accept` 透传给底层实现，policy 只做"准入决策"

#### 4.4 热更新实现
- inotify 监听 policy 目录
- 新 policy 文件 → PREVAIL 验证 → LLVM JIT → RCU-style 替换
- 旧 policy 函数指针在当前调用返回后自动失效

### 5. Policy Case Studies（2.5 页）

**定位**：这些不是独立贡献，而是**展示框架表达力**的实例。

#### 5.1 多租户 SLO 保障
- 背景：同节点多 job 竞争 NIC/NVLink
- Policy 逻辑：基于 telemetry_map 中的 P99 延迟调整 nChannels 和 algo 偏好
- 说明点：policy 需要跨插件融合（profiler telemetry → tuner action）

#### 5.2 故障检测与自动恢复
- 背景：rank 故障/慢节点导致全 job hang
- Policy 逻辑：基于 telemetry_map 中的异常模式触发 revoke/shrink
- 说明点：policy 需要安全保证（错误的故障检测 policy 不应加剧故障）

#### 5.3 SM 干扰控制
- 背景：通信 kernel 抢 SM 导致计算变慢
- Policy 逻辑：基于通信/计算时间比动态调 nChannels
- 说明点：policy 需要热更新（运行中切换不同干扰控制策略）

### 6. Evaluation（3.5 页）

#### 6.1 微基准测试（框架开销）—— 最重要的实验
- **核心问题**：eBPF policy 在 collective 热路径上的开销是否可忽略？
- 测量：no-op policy vs 无 plugin、简单 policy vs 复杂 policy
- 分解开销：bpftime 调用、map lookup、JIT'd 代码执行、MPK 切换
- 目标：per-invocation < 200 ns（collective 操作 10-1000 μs → < 2%）
- 对比 native plugin 的 function call 开销

#### 6.2 安全属性验证
- 验证器拒绝不安全 policy 的示例（无限循环、OOB、非法 helper）
- MPK 隔离：一个故障 policy 不影响其他 policy 和 NCCL 运行
- 热更新：运行中替换 policy 的行为一致性和切换延迟

#### 6.3 Policy 表达力评估（三个 case study）
- 多租户 SLO：P50/P99 step time CDF、Jain's fairness、总吞吐
- 故障恢复：检测时间、恢复时间、丢失迭代数
- SM 干扰：iteration time、通信/计算 overlap

#### 6.4 与 native plugin baseline 的对比
- 将三个 policy 的逻辑用 native C++ plugin 实现
- 对比：功能等价，但 NCCLPol 额外提供安全+热更新+隔离
- 性能差异应可忽略（证明 eBPF 的安全保证不以性能为代价）

### 7. Discussion（1 页）

- **eBPF 表达力边界**：512 字节栈、有界循环。对简单 policy 足够，复杂 ML-based policy 可能需要通过 maps 与用户态 daemon 协作
- **Stock ABI 天花板**：tuner v5 无法控制 chunk size、thread count（AutoCCL 发现）。这限制 policy 的动作空间，但不影响安全框架本身的价值
- **RCCL 可移植性**：RCCL 有类似 tuner API，NCCLPol 的 policy 字节码可跨平台复用
- **gpu_ext 互补**：gpu_ext 在 GPU driver 层，NCCLPol 在 collective runtime 层，未来可协同

### 8. Related Work（1.5 页）

#### eBPF 安全扩展（最近的对比类）
- bpftime/EIM (OSDI'25)：通用框架，NCCLPol 是其在 collective communication 的 domain application
- XRP (OSDI'22)：eBPF for storage。同类论文：把 eBPF 带入新 domain
- Electrode (NSDI'23)：eBPF for distributed protocols
- eTran (NSDI'25)：eBPF for kernel transports
- PageFlex (ATC'25)：eBPF for paging policy
- gpu_ext：eBPF for GPU driver（互补层次）

#### 可编程集合运行时（功能对比，但无安全保证）
- AutoCCL (NSDI'25)：在线调参，需 fork NCCL，native code
- MSCCL/MSCCL++ (ASPLOS'22)：可编程算法，替换 NCCL
- TACCL (NSDI'23)、ForestColl、SyCCL：离线算法合成

#### eBPF 用于分布式训练通信
- XAgg (ToN'24)、ALEPH (ToN'24)、BOAD (eBPF@SIGCOMM'24)：eBPF 加速梯度聚合（不是 NCCL policy）
- eACGM (IWQoS'25)、eInfer (eBPF@SIGCOMM'25)：eBPF 观测（不做 policy 执行）

### 9. Conclusion

---

## 研究计划与时间线

### Phase 0：可行性验证（2-3 周）
- [ ] 编译 NCCL + example tuner plugin，验证 plugin 加载流程
- [ ] 写最小 mixed plugin（tuner + profiler），确认 hook 被调用
- [ ] 在 mixed plugin 内链接 bpftime 库，加载 no-op eBPF 程序，测量 overhead
- [ ] 验证 profiler 回调 → eBPF maps → tuner 读取 的数据通路

### Phase 1：核心框架（4-6 周）
- [ ] 完整 NCCLPol .so（四种 adapter + bpftime + maps）
- [ ] Policy 编程模型实现（ctx 结构、action 结构、helper 注册）
- [ ] 验证器集成（PREVAIL + domain-specific 指令数约束）
- [ ] MPK 隔离 + 热更新

### Phase 2：Policy 实例 + 评估（4-6 周）
- [ ] 三个 policy 实现（eBPF + 等价 native baseline）
- [ ] 微基准测试（开销）
- [ ] 安全属性验证
- [ ] 多租户/故障/干扰 端到端实验

### Phase 3：论文撰写（3-4 周）

### 风险与备选

| 风险 | 影响 | 备选方案 |
|---|---|---|
| bpftime 链接到 NCCL .so 有 CUDA 冲突 | 系统无法构建 | bpftime uprobe 模式（LD_PRELOAD，hook NCCL 导出函数） |
| Stock ABI 动作空间太窄 | Policy 表达力不足 | 混合路径：plugin ABI + bpftime uprobe hook 内部函数 |
| revoke/shrink 无法从 plugin 内触发 | 故障恢复受限 | action_map → 用户态 daemon 异步触发 |
| 无 GPU 集群 | 无法做端到端 | NCCL tests + 模拟 workload + 单节点多 GPU |
| Reviewer 说"只是 bpftime 的 domain application" | novelty 质疑 | 强调 domain-specific 设计（policy 模型、hook ABI、闭环架构）和 μs 级热路径的工程挑战 |
