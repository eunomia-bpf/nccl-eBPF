# NCCLPol：基于 eBPF 的 GPU 集合通信安全可扩展策略框架

## 论文定位与核心叙事

### 一句话定义

NCCLPol 是一个**跨栈 eBPF 策略平面**：通过 bpftime 将内核 eBPF（网络/调度可见性）与用户态 eBPF（NCCL 插件决策）通过共享 maps 连接，使平台方能以受验证的 eBPF 程序安全扩展集合通信行为。

### 核心贡献

**= 跨栈 eBPF 运行时 + 安全可扩展框架 + 集合通信 domain 适配**

1. **跨栈可见性（bpftime 的独特价值）**：NCCL tuner 只能看到 collType/nBytes/nRanks，完全看不到真实网络拥塞、RDMA 错误、CPU 调度干扰。内核 eBPF 能看到这些，bpftime 的 shared maps 让内核观测 → 用户态决策成为可能。这是任何 native plugin 做不到的。
2. **安全可扩展**：验证、隔离、热更新、可审计——使 collective 通信 policy 能在生产环境真正部署
3. **domain-specific 设计**：NCCL policy hook 模型、eBPF helper/maps ABI、telemetry 闭环架构

### 跨栈架构（为什么用 bpftime 而不是 llvmbpf）

```
┌───────────────── Shared eBPF Maps ──────────────────┐
│  congestion_map, error_map, bw_quota_map, telemetry │
└──────┬──────────────────────────────────┬───────────┘
       │ 内核侧写入                        │ 用户态读取+决策
┌──────┴──────┐                    ┌──────┴──────────┐
│  Kernel eBPF │                    │  Userspace eBPF  │
│  (观测层)     │                    │  (bpftime/决策层) │
│              │                    │                  │
│ • XDP/TC:    │                    │ • Tuner policy:  │
│   RoCE拥塞   │                    │   读congestion   │
│   ECN/PFC    │                    │   map→调algo     │
│ • kprobe IB: │                    │ • Profiler hook: │
│   重传/错误   │                    │   写telemetry    │
│ • tracepoint:│                    │ • uprobe NCCL:   │
│   调度干扰    │                    │   更深可见性      │
└──────────────┘                    └──────────────────┘
```

### 三个核心 Use Case

**UC1: 网络感知 collective 调参**
- 内核 XDP 检测 RoCE 拥塞（ECN/PFC/重传）→ 写 `congestion_map`
- NCCL tuner policy 读 → 拥塞时切低带宽算法、降 nChannels
- **价值**：NCCL 插件看不到网络状态，只有内核能看到真实拥塞

**UC2: 多租户带宽隔离（内核执行 + 用户态适应）**
- 内核 TC/cgroup 对每个 job 做 RDMA 硬限速 → 写 `bw_quota_map`
- NCCL tuner policy 读 → 让 NCCL 主动适配配额（调 nChannels 匹配）
- **价值**：NCCL 不知道自己被限速，盲目选高带宽算法反而更差

**UC3: RDMA 错误检测 → 自动缓解**
- 内核 kprobe 在 IB verbs 错误路径 → 写 `error_map`
- NCCL tuner policy 读 → 切换 NIC 路径或降级协议
- **价值**：NCCL profiler 只能看到"变慢了"，内核能看到"为什么"

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

**硬件环境**：单节点 1×RTX 5090 (32GB)，24 核 x86_64，125GB RAM，CUDA 12.9，MPI 可用。
**评估策略**：不依赖多节点 GPU 集群。分四层递进，从纯 CPU 微基准到单 GPU 集成测试。

#### 6.1 纯 CPU 微基准：Policy 执行开销（最核心，不需要 GPU）

**方法**：复用 NCCL 的 `test_plugin.c` 模式——直接调用 plugin API，不经过真实 NCCL 运行时。

```
测试矩阵（每组 100 万次调用，取 P50/P99/max）：
┌──────────────────────┬──────────────────────────────┐
│ 配置                  │ 测什么                        │
├──────────────────────┼──────────────────────────────┤
│ 空函数 (baseline)     │ C 函数调用开销下限             │
│ Native C 逻辑         │ 等价 policy 的 native 实现     │
│ bpftime no-op eBPF    │ eBPF 运行时最小开销            │
│ bpftime 简单 policy   │ 1 次 map lookup + 条件判断     │
│ bpftime 复杂 policy   │ 多次 map lookup + 计算         │
│ bpftime + MPK 隔离    │ MPK 域切换额外开销             │
└──────────────────────┴──────────────────────────────┘

输出图：
- 柱状图：各配置的 per-invocation 延迟 (ns)
- 分解图：bpftime 调用 vs map lookup vs JIT 执行 vs MPK 切换
```

**为什么这够**：`getCollInfo` 是纯 CPU 函数，不涉及 GPU。在真实 NCCL 中它的调用路径也是 CPU 侧。所以直接调用 plugin API 的开销测量 = 真实场景的开销测量。

#### 6.2 安全属性演示（不需要 GPU）

**验证器拒绝演示**：
- 准备 5-10 个故意错误的 eBPF 程序（无限循环、数组越界、调用未注册 helper、栈溢出）
- 展示验证器在加载时拒绝，并给出具体错误信息
- 表格：错误类型 × 验证器响应 × 是否正确拒绝

**MPK 隔离演示**：
- 一个 eBPF policy 故意写越界 → 触发 MPK fault → NCCL 进程不 crash
- 对比：等价的 native plugin 写越界 → 进程直接 segfault

**热更新演示**：
- 持续循环调用 `getCollInfo`，同时替换 policy 文件
- 测量：切换延迟、切换前后行为一致性、零丢失调用
- 对比：等价的 native plugin dlclose + dlopen → 展示 UB 风险

#### 6.3 单 GPU 集成测试：Policy 实际影响 NCCL 行为

**方法**：编译 NCCL + NCCLPol plugin，用 nccl-tests 的单 GPU 多线程模式。

```bash
# 2 ranks on 1 GPU (loopback)
NCCL_TUNER_PLUGIN=./libnccl-policy.so \
  ./all_reduce_perf -b 8 -e 128M -t 2 -g 1 -n 100
```

**实验 A：开销集成验证**
- 对比：无 plugin vs NCCLPol(no-op) vs NCCLPol(简单 policy)
- 量 AllReduce 延迟，确认 plugin 集成后的实际开销与微基准一致
- 出图：延迟 vs 消息大小（三条线应几乎重叠）

**实验 B：Policy 功能验证**
- Policy 1：小消息强制 Ring/LL，大消息强制 Tree/Simple
- 通过 `NCCL_DEBUG=INFO` 日志确认 NCCL 实际选择了 policy 指定的算法
- 出图/表：消息大小 × policy 输出 × NCCL 实际选择 × 是否匹配

**实验 C：闭环 telemetry 验证**
- Profiler adapter 在每次 collective 完成时写 telemetry 到 eBPF maps
- Tuner policy 读 maps 中的历史延迟，动态调整 nChannels
- 出图：nChannels 随时间/调用次数的变化曲线（展示自适应行为）

#### 6.4 模拟多租户竞争（单 GPU，多进程）

**方法**：同一台机器启动 2 个 nccl-tests 进程，共享 1 个 GPU。

```bash
# 进程 A：有 SLO policy
NCCL_TUNER_PLUGIN=./libnccl-policy.so POLICY_FILE=slo.bpf.o \
  ./all_reduce_perf -b 1M -e 1M -t 2 -g 1 -n 1000 &

# 进程 B：无 policy（竞争者）
./all_reduce_perf -b 1M -e 1M -t 2 -g 1 -n 1000 &
```

**测量**：两个进程的 collective 延迟分布（CDF）
**限制**：单 GPU 竞争主要是 SM 和 PCIe 带宽，不是 NIC/NVLink。在论文中诚实说明这是模拟，全规模评估留作 future work。
**但仍有价值**：展示 SLO policy 在竞争下能稳定延迟（即使场景有限）

#### 6.5 与 native plugin baseline 的性能对比

- 将 SLO policy 逻辑用纯 C 实现为 native tuner plugin
- 功能完全等价
- 对比延迟：eBPF JIT 应与 native 几乎相同
- 关键论点：eBPF 的安全保证（验证+隔离+热更新）不以性能为代价

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

**硬件**：单节点 1×RTX 5090, 24 核, 125GB RAM, CUDA 12.9, MPI
**目标**：Workshop 级别 preliminary evaluation（eBPF workshop / HotOS / poster）

### Phase 0：编译验证（3-5 天）
- [ ] 编译 NCCL（`make -j24 src.build`）
- [ ] 编译运行 example tuner plugin 的 test（`nccl/plugins/tuner/example/test/`，纯 CPU，不需 GPU）
- [ ] clone nccl-tests，编译，单 GPU 跑通 `all_reduce_perf -t 2 -g 1`
- [ ] 确认 `NCCL_TUNER_PLUGIN=./libnccl-tuner-example.so` 生效（看 NCCL_DEBUG 日志）

### Phase 1：最小 eBPF Plugin（1-2 周）
- [ ] 写一个最小 tuner plugin .so，在 `getCollInfo` 中调用 bpftime 执行一个 no-op eBPF 程序
  - bpftime 作为静态库链接（不用 LD_PRELOAD，避免 CUDA 干扰）
  - 如果静态链接有问题，退化为：plugin 中手动创建 llvmbpf_vm + JIT，不依赖完整 bpftime
- [ ] 用 `test_plugin.c` 模式写 CPU-only 微基准，量 per-call 开销
- [ ] **产出第一组数据**：eBPF policy 的 per-invocation 延迟（ns）

### Phase 2：Policy 编程模型 + 三个实例（2-3 周）
- [ ] 定义 `nccl_policy_ctx` 和 `nccl_policy_action` 结构体
- [ ] 注册 helper 函数（map lookup, trace_printk）
- [ ] 实现三个 eBPF policy 程序（.bpf.c → .bpf.o）：
  - **size-aware policy**：按消息大小选 algo/proto（功能验证）
  - **adaptive nChannels policy**：读 telemetry map 动态调 nChannels（闭环验证）
  - **SLO policy**：基于历史 P99 做决策（表达力展示）
- [ ] 实现等价的 native C plugin baseline

### Phase 3：安全属性演示（1 周）
- [ ] 写 5-10 个"坏" eBPF 程序，展示验证器拒绝
- [ ] MPK 隔离演示（如果硬件支持）
- [ ] 热更新演示：循环调用中替换 policy
- [ ] **产出第二组数据**：安全属性 pass/fail 表

### Phase 4：单 GPU 集成 + 模拟竞争（1-2 周）
- [ ] NCCLPol plugin 加载到真实 NCCL，跑 nccl-tests `-t 2 -g 1`
- [ ] 通过 NCCL_DEBUG 日志确认 policy 实际改变了 algo 选择
- [ ] Profiler adapter → eBPF maps → tuner 闭环验证
- [ ] 两个 nccl-tests 进程同时跑，模拟竞争
- [ ] **产出第三组数据**：集成后的 overhead + policy 功能验证 + 模拟竞争下的延迟 CDF

### Phase 5：论文撰写（2-3 周）
- 对 workshop 投稿足够的数据：overhead 微基准 + 安全演示 + 功能验证 + 模拟竞争
- 诚实说明：全规模多节点评估是 future work

### 风险与备选

| 风险 | 影响 | 备选方案 |
|---|---|---|
| bpftime 链接到 plugin .so 失败 | 系统无法构建 | 直接用 llvmbpf（更轻量，仅 JIT 编译器，无 runtime 依赖） |
| NCCL 编译失败 | 无法做 GPU 集成 | 仅用 CPU-only 微基准（直接调 plugin API），仍可出 overhead 数据 |
| 单 GPU nccl-tests 太受限 | 结果不够说服力 | 强调 CPU-only 微基准才是核心（getCollInfo 是 CPU 函数），GPU 集成只是 bonus |
| 模拟竞争不真实 | Reviewer 质疑 | 论文定位为 workshop/preliminary，诚实标注限制，承诺 future work |
| MPK 不可用（需要 Intel 特定 CPU） | 无法演示硬件隔离 | 本机是 x86_64 应该支持，但若不支持则仅展示验证器隔离 |

### 最终可出的图表（workshop 级别）

| 图表 | 数据来源 | 需要 GPU？ |
|---|---|---|
| **Fig 1**: per-invocation overhead 柱状图（6 种配置） | CPU-only 微基准 | 否 |
| **Fig 2**: overhead 分解（bpftime/map/JIT/MPK） | CPU-only 微基准 | 否 |
| **Fig 3**: 安全属性 pass/fail 表 | 验证器+MPK 演示 | 否 |
| **Fig 4**: 热更新切换延迟 | CPU-only 演示 | 否 |
| **Fig 5**: nccl-tests 延迟 vs 消息大小（三条线） | 单 GPU 集成 | 是（1 GPU） |
| **Fig 6**: Policy 功能验证（algo 选择匹配表） | 单 GPU + NCCL_DEBUG | 是（1 GPU） |
| **Fig 7**: 闭环 nChannels 自适应曲线 | 单 GPU 集成 | 是（1 GPU） |
| **Fig 8**: 模拟竞争下延迟 CDF | 单 GPU 双进程 | 是（1 GPU） |
| **Fig 9**: eBPF vs native plugin 性能对比 | CPU 微基准+GPU 集成 | 混合 |
