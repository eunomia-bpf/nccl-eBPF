# NCCLPol：基于 bpftime 的 NCCL 混合 Tuner/Profiler eBPF 策略框架

## 论文定位与核心叙事

### 一句话定义

NCCLPol 是一个**基于 bpftime 的 NCCL 混合插件**：在单个 `.so` 中同时导出 `ncclTunerPlugin_v5` 与 `ncclProfiler_v6`，以受验证的用户态 eBPF 程序扩展 `getCollInfo()` 热路径，并通过共享 eBPF maps 形成 profiler→tuner 的真实闭环。

### 核心贡献

**= 混合 NCCL 插件 + 安全 eBPF 执行 + 真实 telemetry 闭环**

1. **混合插件架构**：单个 NCCL 插件共享库同时导出 tuner v5 与 profiler v6，共享同一套 bpftime runtime、maps 与 communicator 状态，在 stock NCCL 插件 ABI 内实现 profiler→tuner 闭环。
2. **安全执行模型**：在加载时完成 PREVAIL/bpftime verifier 检查，7 个坏程序被拒绝、7 个好程序被接受；对比实验中，等价 native plugin 会直接 `SIGSEGV`，eBPF 策略则在加载期被拒绝。
3. **热路径低开销**：`getCollInfo()` 的 CPU 侧策略执行开销保持在亚 100ns 量级；分解结果显示 dispatch `+41ns`、一次 map lookup `+11ns`、一次 map update 再加 `+11ns`。
4. **真实 NCCL 闭环验证**：在真实 2-rank NCCL 运行中，profiler 写入非零 collective latency，tuner 读取后调整 channels；同时通过 `NCCL_DEBUG_SUBSYS=TUNING` 验证 10/10 个消息大小上的 policy 请求 `algo/proto` 被 NCCL 精确采用。

### 混合插件架构（为什么用 bpftime 而不是 native .so）

```
┌──────────────── NCCL Mixed Plugin (.so) ────────────────┐
│                                                         │
│   ncclTunerPlugin_v5            ncclProfiler_v6         │
│          │                              │               │
│   getCollInfo()                    event callbacks      │
│          └──────────────┬───────────────┘               │
│                         │                               │
│                   bpftime Runtime                       │
│             verifier + JIT + helper ABI                │
│                         │                               │
│                 Shared eBPF Maps                        │
│             telemetry_map / config_map                 │
│                         ▲                               │
│                         │                               │
│                 eBPF policies                           │
│      size_aware_v2 / adaptive_channels / slo_enforcer  │
└─────────────────────────────────────────────────────────┘
```

关键点：
- 不是“跨栈 kernel↔userspace policy plane”，而是**用户态 NCCL mixed plugin**。
- 不是替换 NCCL，也不是 fork NCCL；它工作在 stock NCCL 的 tuner/profiler ABI 之内。
- 不是通用沙箱论文，而是**面向 collective tuning 热路径**的 domain-specific 落地。

### 三个核心 Use Case

**UC1: 可验证的 algo/proto 政策注入**
- `size_aware_v2` 依据消息大小重写 cost table、请求 `algo/proto/channels`
- 在真实 2-rank NCCL 路径上，10/10 个消息大小的 `algo/proto` 选择与 policy 请求完全一致
- 价值：证明 eBPF policy 不只是“跑了”，而是实际改变了 NCCL 选择结果

**UC2: 真实 profiler→tuner telemetry 闭环**
- profiler adapter 在 collective 完成时写入真实 latency 到 `telemetry_map`
- `adaptive_channels` 在后续 `getCollInfo()` 中读取并调整 channels
- 对照组无 profiler 时 channels 保持不变；有 profiler 时出现 `8 -> 9 -> 10/9` 的真实自适应

**UC3: 安全策略演进**
- 新策略在加载期先验证、再 JIT、再原子替换
- 原子交换窗口约 `0.3us`，`400000` 次并发调用零丢失
- 坏替换策略会被拒绝，旧策略继续服务

### 类比定位

| 系统 | 做的事 | 我们的关系 |
|---|---|---|
| Linux eBPF | 内核模块 → 受验证的 eBPF 程序 | 同样的“安全扩展”范式 |
| bpftime/EIM (OSDI'25) | 通用用户态 eBPF 执行基础设施 | 我们的执行基础 |
| XRP / Electrode | eBPF 进入新的性能敏感 domain | 同类“domain application”论文 |
| AutoCCL / MSCCL++ | 可编程 collective，但需 fork/替换 NCCL | 我们坚持 stock NCCL ABI |
| **NCCLPol (ours)** | **面向 NCCL tuner/profiler 的受验证 eBPF policy** | **安全扩展 + 闭环控制** |

### 为什么 native plugin 不够——安全作为核心论点

NCCL 已经支持 native `.so` 插件，但在 GPU 集群场景下，这个扩展模型并不稳健：

| 维度 | Native .so Plugin | NCCLPol 当前实现 |
|---|---|---|
| **部署前安全检查** | 无，加载后才能发现错误 | 加载时 verifier 拒绝非法程序 |
| **坏策略后果** | 可能直接 crash/hang 训练进程 | 坏 eBPF 程序在执行前被拒绝 |
| **热更新** | 常见做法是重启进程，在线替换风险高 | 原子替换，零调用丢失 |
| **跨角色共享状态** | 需要手写共享内存/全局状态 | tuner/profiler 共享 eBPF maps |
| **开销上界** | 取决于任意 native 代码 | 热路径实测亚 100ns |
| **隔离能力** | 无 | 当前以 verifier + helper/map ABI 为主；MPK/EIM 为未来工作 |

**关键论点**：本文不是在证明“eBPF 比 native 更快”，而是在证明**在 collective 热路径中引入安全扩展，不必牺牲可用性能**，同时获得 verifier、热更新、和可审计的策略部署模型。

---

## Abstract（中文）

GPU 集合通信库 NCCL 已暴露 tuner 与 profiler 等插件接口，使平台方能够在每次 collective 决策和通信事件完成时插入自定义逻辑。然而，现有插件以 native 共享库形式加载，缺乏加载时验证、失败前置阻断和安全热更新能力；更重要的是，stock NCCL 并未直接提供 profiler 与 tuner 之间的标准状态共享机制，使在线闭环策略难以安全落地。

本文提出 NCCLPol，一个基于 bpftime 的 NCCL 混合 eBPF 插件框架。NCCLPol 在单个共享库中同时导出 `ncclTunerPlugin_v5` 与 `ncclProfiler_v6`，通过共享 eBPF maps 将 profiler 产生的真实 collective latency 反馈给 tuner policy，并在 `getCollInfo()` 热路径中执行受验证的用户态 eBPF 程序。NCCLPol 采用加载时 PREVAIL/bpftime verifier、JIT 编译和原子热更新：7 个非法程序被拒绝、7 个合法程序被接受；等价 native plugin 会触发进程崩溃，而 eBPF 程序在执行前即被拦截。评估表明，策略执行的 CPU 侧开销低于 100ns，且具有清晰的开销阶梯：dispatch `+41ns`、一次 map lookup `+11ns`、一次 map update 再加 `+11ns`。热更新的原子交换窗口约 `0.3us`，在 `400000` 次活跃调用下零调用丢失。

在真实 NCCL 2-rank 集成中，NCCLPol 触发了每 rank `495` 次 `getCollInfo()` 调用；`size_aware_v2` 所请求的 `algo/proto` 在 10/10 个消息大小上被 NCCL 精确采用；`adaptive_channels` 在真实 profiler telemetry 驱动下表现出 `8 -> 9 -> 10/9` 的闭环自适应，而关闭 profiler 后行为保持为 `8 -> 8 -> 8`。这些结果表明，基于 eBPF 的安全策略扩展和真实 profiler→tuner 闭环已经可以在 stock NCCL 中以较低成本落地。本文同时诚实指出当前限制：系统仅覆盖用户态 tuner/profiler 路径，不包含 kernel eBPF、MPK、EIM、net/env adapter 或多租户集群级评估。

---

## 论文结构

### 1. Introduction（0.75 页）

**开篇**：NCCL 已经可扩展，但其扩展模型仍然是“不安全的 native code”。一段错误的 tuner/profiler 插件代码，可能直接把训练进程带崩；而生产环境又确实需要在线部署、替换、回滚 collective policy。我们的问题不是“如何再做一个 tuning heuristic”，而是“如何把**安全可演进的 policy plane**带到 NCCL 热路径里”。

**问题**：
- stock NCCL tuner/profiler ABI 已足够实用，但默认扩展单元是 native `.so`
- native 插件缺乏 verifier、缺乏安全热更新、缺乏 profiler→tuner 的统一状态平面
- 现有工作大多要么 fork/替换 NCCL，要么只做观测，不解决“安全扩展 + 闭环控制”的组合问题

**贡献**：
1. **NCCLPol 混合插件**：单 `.so` 导出 `ncclTunerPlugin_v5` 与 `ncclProfiler_v6`，在 stock NCCL ABI 内实现 mixed tuner+profiler
2. **安全执行模型**：加载时 verifier、native crash vs eBPF rejection、原子热更新
3. **低开销证据**：`getCollInfo()` 热路径亚 100ns，且开销分解清晰
4. **真实 NCCL 证据**：2-rank 集成、495 次真实调用、10/10 algo/proto 采用、真实 telemetry 闭环

### 2. Background & Motivation（0.75 页）

#### 2.1 NCCL 集合通信与插件系统
- 简述 collective 决策路径，强调 `getCollInfo()` 是 CPU 侧热路径
- 介绍 tuner v5 与 profiler v6 的职责边界
- 介绍 mixed plugin 机制：一个 `.so` 可同时导出多个插件符号

#### 2.2 Native 插件的安全问题（核心 motivation）

论据：
- **失败代价高**：一个坏的 tuner/profiler 插件会直接影响训练作业，不是普通用户态 bug
- **在线替换难**：native `.so` 缺少清晰的原子更新路径，常见运维手段是重启进程
- **缺少前置阻断**：native 插件没有 verifier；越界、空指针、未注册调用等错误只能在运行时暴露
- **闭环状态管理零散**：profiler 和 tuner 间若要共享状态，只能自己维护共享对象，缺少统一机制

这使得“可扩展”不等于“可安全部署”。本文的切入点是：**把 eBPF 的受验证扩展模型带进 NCCL，而不是把 NCCL 重新实现一遍。**

#### 2.3 现有工作的不足
- AutoCCL：在线调参，但依赖扩展/修改 NCCL ABI，本质仍是 native code
- MSCCL / MSCCL++：强调算法可编程，但不是 stock NCCL plugin 路线
- eACGM 等工作：可以观测 NCCL，但不在 NCCL 决策面执行 policy
- bpftime/EIM：提供用户态 eBPF 执行基础，但没有 NCCL domain 适配与 mixed plugin 闭环

**空白点**：还没有工作同时展示 stock NCCL ABI、eBPF 安全扩展、以及 profiler→tuner 闭环控制三者的结合。

### 3. Design（1.25 页）

#### 3.1 总体架构

```
┌──────────────────────────────────────────────┐
│             NCCLPol Mixed Plugin             │
│                                              │
│   Tuner v5 Adapter        Profiler v6 Adapter│
│          │                       │           │
│          └──────────┬────────────┘           │
│                     │                        │
│               Shared Communicator State      │
│                     │                        │
│                bpftime Runtime               │
│         verifier / JIT / maps / helpers      │
│                     │                        │
│      eBPF policies  ◄──── telemetry_map ─────┘
└──────────────────────────────────────────────┘
```

关键设计决策：
- bpftime 作为库嵌入 NCCL plugin `.so`
- tuner 与 profiler 共享同一 communicator-local policy state
- profiler 是 telemetry 生产者，tuner 是 telemetry 消费者
- 策略本身只通过 helper 与 maps 访问状态，不直接读写 NCCL 内部结构

#### 3.2 安全执行模型（核心设计贡献）

##### 3.2.1 加载时验证
- 文件策略在加载时进入 verifier 路径，再进入 JIT/执行
- 验证目标包括：非法 helper、潜在空指针解引用、越界访问、栈越界、无界循环、非法上下文写、除零风险
- 当前实证：7 个坏程序全部拒绝，7 个好程序全部接受
- 运行时不再做逐调用安全检查，因此不在热路径上引入额外 guard

##### 3.2.2 进程内执行边界（当前实现）
- 当前实现的安全性主要来自 verifier、helper 白名单、map ABI 与受限上下文
- 这不是 MPK 级别的硬件隔离；所有代码仍在同一进程内
- 论文中应明确：**当前工件实现的是“受验证的进程内 eBPF 扩展”，不是硬件隔离沙箱**

##### 3.2.3 原子热更新
- 新策略先完成加载、验证、重定位、JIT
- 准备完成后原子交换到新的不可变 policy state
- 旧 state 对正在执行的调用保持可见，直到引用自然结束
- 坏替换不会污染当前服务中的好策略

##### 3.2.4 最小权限接口（当前 helper 白名单；EIM 为未来工作）
- 当前 policy 只能使用注册的 helper，并通过约定的 map/schema 读写状态
- 没有实现 per-policy capability manifest，也没有 EIM 风格细粒度 capability enforcement
- 因此本文把“最小权限”描述为**当前 helper surface 有界**，而把 EIM 放到 future work

#### 3.3 Policy 编程模型

##### 3.3.1 Hook 事件类型
| 事件 | 触发时机 | 对应 NCCL 插件 API | 热路径？ |
|---|---|---|---|
| `COMM_INIT` | communicator 初始化 | tuner/profiler `init` | 否 |
| `COLL_DECIDE` | 每次 collective 决策 | tuner `getCollInfo` | **是** |
| `TELEMETRY` | collective 完成事件 | profiler 回调 | 否 |

说明：
- 当前工件只覆盖 tuner/profiler；没有 net/env hook
- 真实闭环依赖 `TELEMETRY -> telemetry_map -> COLL_DECIDE`

##### 3.3.2 上下文结构（eBPF 程序的输入）
```c
struct nccl_policy_ctx {
    uint32_t coll_type;
    uint64_t n_bytes;
    uint32_t n_ranks;
    uint32_t n_nodes;
    int      reg_buff;
    uint64_t comm_id;
    /* profiler 产生的历史统计通过 telemetry_map 读取 */
};
```

##### 3.3.3 动作空间（eBPF 程序的输出）
```c
struct nccl_policy_action {
    int preferred_algo;    /* 通过 cost table 偏置 */
    int preferred_proto;   /* 通过 cost table 偏置 */
    int n_channels;        /* 直接请求 channel 数 */
};
```

##### 3.3.4 Helper 函数（policy 可调用的 API）
```c
void *bpf_map_lookup_elem(struct bpf_map *map, const void *key);
int bpf_map_update_elem(struct bpf_map *map, const void *key,
                        const void *value, uint64_t flags);
int bpf_trace_printk(const char *fmt, ...);
```

#### 3.4 跨插件融合与闭环反馈

**为什么 stock 分离插件不方便**：tuner 与 profiler 在 API 上是两个独立角色，NCCL 并未提供标准“共享状态面”。

**NCCLPol 的做法**：
- 单 `.so` 同时导出 tuner/profiler
- 两个 adapter 共享 communicator state 与 eBPF maps
- profiler 在 collective 完成时写入真实 latency 样本
- tuner 在后续 `getCollInfo()` 中读取 telemetry 并更新动作

```
collective 完成
    ↓
Profiler callback → 写 telemetry_map（真实 latency）
    ↓
下一次 getCollInfo()
    ↓
Tuner policy → 读 telemetry_map → 调 channels / algo / proto
```

这就是本文的核心闭环，不包含 kernel eBPF，也不依赖跨进程共享 map。

### 4. Implementation（0.75 页）

#### 4.1 Mixed Plugin 实现
- 导出符号：`ncclTunerPlugin_v5`、`ncclProfiler_v6`
- tuner 与 profiler 共用一份共享状态，避免分裂的 per-role 生命周期
- 修复了真实 NCCL 2-rank 路径中 cost table 指针布局问题，使 tuner 对 NCCL 的 cost-table 改写符合实际内存布局

#### 4.2 bpftime 集成
- 以库方式链接到 plugin `.so`
- 当前使用自定义 loader/relocation 路径，接入 bpftime maps、verifier 与 `bpftime_prog`
- 策略从文件路径加载，支持严格验证模式
- 不声称使用 kernel-side shared maps，也不声称完整 EIM runtime 集成

#### 4.3 Profiler Adapter 的 latency bridge 设计
- 通过 profiler 生命周期回调记录 collective 完成事件
- 以 NCCL profiler 事件时间戳计算 latency，并写入 `telemetry_map`
- `adaptive_channels` 不再依赖 synthetic latency，而是消费 profiler-fed 样本
- 使用 `applied_samples` 避免同一个 telemetry 样本被多次重复应用

#### 4.4 热更新实现
- 新策略离线完成加载、验证、JIT
- 用不可变 `LoadedPolicyState` 承载活动策略
- 通过原子交换切换 `shared_ptr`
- 当前结果来自测试钩子/显式 reload 路径，不宣称实现了 `inotify` 自动监听

### 5. Policy Case Studies（0.5 页）

**定位**：这些 policy 主要用于展示框架表达力与闭环能力，而不是宣称已经完成完整集群优化。

#### 5.1 `size_aware_v2`：消息大小感知的 algo/proto 选择
- 小消息偏向 `TREE+SIMPLE`
- 中等消息偏向 `TREE+LL`
- 大消息偏向 `RING+LL` / `RING+SIMPLE`
- 真实 NCCL 日志已验证 10/10 个消息大小上的 `algo/proto` 精确采用

#### 5.2 `adaptive_channels`：基于真实 latency 的 channels 自适应
- profiler 写入上一轮 collective latency
- tuner 在下一轮基于 `telemetry_map` 调整 channels
- 单独对照组证明“没有 profiler 就没有闭环变化”

#### 5.3 `slo_enforcer`：更复杂的 map-backed policy
- 展示多次 map 访问与状态维护仍保持亚 100ns
- 作为“更复杂 policy”的代表，说明表达力不止于 no-op / size-aware
- 当前不宣称真实多租户 SLO 收益，只作为可表达 policy 的示例

### 6. Evaluation（1.5 页）

**硬件环境**：单节点 `1×RTX 5090`，24 核 x86_64，125GB RAM，CUDA 12.9，NCCL 2.29.7。
**评估目标**：workshop/short paper 风格，强调 correctness、safety、overhead 与真实 NCCL 可运行性，而不是大规模吞吐提升。

#### 6.1 纯 CPU 微基准：Policy 执行开销

核心结论：
- native baseline：`10ns` P50 / `16ns` P99
- `noop`：`51ns` P50 / `61ns` P99
- `adaptive_channels`：`75ns` P50 / `88ns` P99
- `slo_enforcer`：`80ns` P50 / `95ns` P99

建议图表：
- **Fig 2**：开销柱状图（native / noop / size_aware_v2 / adaptive_channels / slo_enforcer）
- **Fig 3**：开销阶梯图

强调的分解：
- dispatch：`+41ns`
- 单次 telemetry map lookup：`+11ns`
- 单次 update：再 `+11ns`

#### 6.2 安全属性演示

**验证器矩阵**：
- 7 个好程序：全部 `ACCEPTED`
- 7 个坏程序：全部 `REJECTED`
- 错误类型覆盖：null-deref、OOB、未注册 helper、stack overflow、无界循环、非法 ctx 写、除零风险

**native crash vs eBPF rejection**：
- 等价 native tuner plugin：`SIGSEGV`
- 等价 eBPF policy：加载期被 verifier 拒绝

**热更新**：
- 原子交换窗口：约 `0.309us`
- `400000` 次活跃调用：`zero call loss=yes`
- 坏替换被拒绝，旧策略保持可用

建议图表：
- **Table 1**：验证器 accept/reject 矩阵
- **Fig 4**：native crash vs eBPF rejection
- **Fig 5**：热更新时间线与原子边界

#### 6.3 真实 NCCL 集成：从“插件被调用”到“闭环真的发生”

**真实 `getCollInfo()` 调用证据**：
- 真实 2-rank communicator 成功建立
- 每 rank `finalize calls=495`
- 说明插件不仅被加载，而且进入了真实 NCCL 决策路径

**真实 `algo/proto` 采用验证**：
- 通过 `NCCL_DEBUG_SUBSYS=TUNING` 收集 NCCL 最终选择
- `size_aware_v2` 的 10 个消息大小分段中，`algo/proto` 选择 10/10 完全匹配 policy 请求：
  - `1024, 2048`: `TREE + SIMPLE`
  - `4096..32768`: `TREE + LL`
  - `65536..524288`: `RING + LL`
  - `1048576`: `RING + SIMPLE`

**真实 profiler→tuner 闭环**：
- 有 profiler：`channels 8 -> 9 -> 10/9`
- 无 profiler：`channels 8 -> 8 -> 8`
- 真实样本非零，例如 rank 0 第一条 profiler latency 为 `9264480ns`

**端到端性能说明**：
- 在 `1MiB`、single-GPU、socket-only 的受限设置下：
  - 有 profiler telemetry：`4391.60us`
  - 无 profiler：`4404.07us`
- 本实验的意义是**闭环 correctness**，不是吞吐提升

建议图表：
- **Table 2**：10 个消息大小上的 `policy request` vs `NCCL final selection`
- **Fig 6**：真实闭环与无 profiler 对照的 channels 演化

#### 6.4 当前评估边界（必须诚实写）

- 没有 kernel eBPF，也没有 kernel↔userspace shared maps
- 没有 net adapter、env adapter
- 没有 MPK 硬件隔离，也没有 EIM capability model
- 没有真实多租户竞争 / SLO enforcement 的 workload 评估
- 真实 NCCL 路径目前是单机单 GPU 强制形成的 2-rank socket-only 路径
- `algo/proto` 的最终采用已精确验证，但 `n_channels` 的最终采用只证明“有影响”，并非每次都精确等于请求值

#### 6.5 与 native plugin baseline 的对照方式

本文的 native baseline 不是“功能等价的完整系统”，而是两类必要对照：
- **性能对照**：native baseline `10ns` P50，说明 eBPF 安全执行只增加几十纳秒量级开销
- **安全对照**：native 插件可直接 crash；eBPF 程序在执行前被拒绝

这足以支撑 workshop 论文的核心论点：**安全扩展不是零成本，但成本极低且收益明确。**

### 7. Discussion（0.5 页）

- **表达力边界**：当前动作空间主要是 `algo/proto` 偏置与 `n_channels`，仍受 stock tuner ABI 限制
- **channel 采用的解释**：plugin 返回的 `n_channels` 会受到 NCCL 运行时 clamp；因此本文只把“exact algo/proto adoption”作为强 claim，把“channel influenced”作为弱 claim
- **未来工作 1：cross-stack**：如果后续引入 kernel eBPF 与 shared maps，可把网络/调度信号接入当前框架；但这不是本文工件
- **未来工作 2：更强隔离**：MPK、EIM、per-policy capability manifest 都值得做，但应明确属于下一阶段
- **未来工作 3：集群级多租户**：真实竞争、SLO enforcement、故障恢复等大规模评估需多节点环境

### 8. Related Work（0.5 页）

#### eBPF 安全扩展
- bpftime/EIM：提供用户态 eBPF 执行与安全扩展基础
- XRP / Electrode / eTran / PageFlex：把 eBPF 引入存储、协议、传输、分页等性能敏感路径
- 我们的差异：面向 NCCL collective tuning/profiling 的 domain-specific mixed plugin 落地

#### 可编程集合通信
- AutoCCL：在线调参，但依赖扩展 ABI / native code
- MSCCL / MSCCL++ / TACCL：强调算法生成或替换 NCCL
- 我们的差异：坚持 stock NCCL plugin ABI，而不是 fork 或替换整个通信栈

#### 训练通信观测
- eACGM 等工作提供观测能力
- 我们的差异：不是只观测，而是在 NCCL 决策面执行 policy，并形成 profiler→tuner 闭环

### 9. Conclusion

本文应收敛到一个明确、可信的 workshop 叙事：

- 我们已经实现了一个 **bpftime-backed NCCL mixed tuner+profiler plugin**
- 它支持 **加载时 verifier、安全热更新、低热路径开销**
- 它在 **真实 NCCL** 中完成了 **2-rank 集成、10/10 algo/proto 采用验证、以及 profiler→tuner 真实闭环**
- 它**没有**实现 cross-stack kernel eBPF、MPK、EIM、net/env adapter、或多租户集群评估

这个版本的故事足够诚实，也足够完整，适合 4-6 页 workshop/short paper。

---

## 研究计划与时间线

**硬件**：单节点 `1×RTX 5090`，24 核，125GB RAM，CUDA 12.9，MPI  
**目标**：4-6 页 workshop/short paper，聚焦“安全扩展 + 闭环 correctness + 低开销”

### Phase 0：编译验证（已完成）
- [x] 编译 NCCL 与 nccl-tests
- [x] 编译 mixed plugin 与测试 harness
- [x] 确认 `NCCL_TUNER_PLUGIN` / `NCCL_PROFILER_PLUGIN` 正常加载

### Phase 1：最小 eBPF Plugin（已完成）
- [x] 在 `getCollInfo()` 中执行 bpftime-backed eBPF policy
- [x] 建立 CPU-only 微基准
- [x] 得到热路径开销结果

### Phase 2：Policy 编程模型 + 基本策略（已完成）
- [x] 定义 policy context / action / maps ABI
- [x] 实现 `size_aware_v2`
- [x] 实现 `adaptive_channels`
- [x] 实现 `slo_enforcer`

### Phase 3：安全属性演示（已完成）
- [x] verifier accept/reject 矩阵
- [x] native crash vs eBPF rejection
- [x] 原子热更新与坏替换拒绝

### Phase 4：真实 NCCL 集成（已完成）
- [x] 构造真实 2-rank NCCL 路径
- [x] 验证 `getCollInfo()` 真实被调用（495 次/每 rank）
- [x] 验证 `algo/proto` 在 10/10 个消息大小上的精确采用
- [x] 验证真实 profiler→tuner telemetry 闭环

### Phase 5：论文撰写（进行中）
- [ ] 将故事压缩到 4-6 页，只保留已实现主张
- [ ] 统一使用“mixed tuner+profiler plugin”作为 headline
- [ ] 在 abstract、introduction、discussion 中显式写出限制
- [ ] 清理任何残留的 cross-stack / MPK / EIM / net/env overclaim

### 风险与备选

| 风险 | 影响 | 备选方案 |
|---|---|---|
| Reviewer 质疑单 GPU 强制 2-rank 路径过于人工 | 真实集成说服力下降 | 明确把该结果定位为“真实 NCCL correctness proof”，不宣称集群级性能收益 |
| Reviewer 追问 channel 是否精确采用 | 强 claim 可能被击穿 | 只强 claim `algo/proto` 精确采用；`channels` 只 claim “受 telemetry 影响且 NCCL 采纳部分变化” |
| 读者误以为已有 kernel eBPF / MPK / EIM | 论文可信度受损 | 所有相关内容收束到 Discussion/Future Work |
| 闭环性能收益不显著 | 结果看起来“不够强” | 强调本文主结果是“闭环 correctness + 安全扩展 + 低开销”，不是吞吐优化 |

### 最终可出的图表（workshop 级别）

| 图表 | 数据来源 | 说明 |
|---|---|---|
| **Fig 1**: mixed tuner+profiler 架构图 | 设计章节 | 单 `.so`、共享 maps、profiler→tuner 闭环 |
| **Fig 2**: 热路径开销柱状图 | CPU 微基准 | native / noop / size_aware_v2 / adaptive_channels / slo_enforcer |
| **Fig 3**: 开销阶梯分解 | Revise #2 微基准 | `+41ns` dispatch、`+11ns` lookup、`+11ns` update |
| **Table 1**: verifier accept/reject 矩阵 | Phase 3 | 7 good accepted / 7 bad rejected |
| **Fig 4**: native crash vs eBPF rejection | Phase 3 | `SIGSEGV` vs load-time reject |
| **Fig 5**: 热更新时间线 | Phase 3 / Revise #2 | `0.309us` swap、零调用丢失 |
| **Table 2**: 10 个消息大小上的 algo/proto 采用表 | Phase 4 tuning log | 10/10 精确匹配 |
| **Fig 6**: 真实 profiler 闭环 vs 无 profiler 对照 | profiler-adapter 结果 | `8 -> 9 -> 10/9` vs `8 -> 8 -> 8` |
| **Table 3**: 当前限制与未来工作 | Discussion | cross-stack / MPK / EIM / net/env / multi-tenant |
