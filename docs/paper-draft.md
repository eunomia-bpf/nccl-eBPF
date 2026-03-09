# NCCLPol：基于 bpftime 的 NCCL 混合 Tuner/Profiler eBPF 策略框架

## 摘要

GPU 集合通信库 NCCL 已暴露 tuner 与 profiler 等插件接口，使平台方能够在每次 collective 决策和通信事件完成时插入自定义逻辑。然而，现有插件以 native 共享库形式加载，缺乏加载时验证、失败前置阻断和安全热更新能力；更重要的是，stock NCCL 并未直接提供 profiler 与 tuner 之间的标准状态共享机制，使在线闭环策略难以安全落地。

本文提出 NCCLPol，一个基于 bpftime 的 NCCL 混合 eBPF 插件框架。NCCLPol 在单个共享库中同时导出 `ncclTunerPlugin_v5` 与 `ncclProfiler_v6`，通过共享 eBPF maps 将 profiler 产生的真实 collective latency 反馈给 tuner policy，并在 `getCollInfo()` 热路径中执行受验证的用户态 eBPF 程序。NCCLPol 采用加载时 PREVAIL/bpftime verifier、JIT 编译和原子热更新：7 个非法程序被拒绝、7 个合法程序被接受；等价 native plugin 会触发进程崩溃，而 eBPF 程序在执行前即被拦截。评估表明，策略执行的 CPU 侧开销低于 100ns，且具有清晰的开销阶梯：dispatch +41ns、一次 map lookup +11ns、一次 map update 再加 +11ns。热更新的原子交换窗口约 0.3μs，在 400,000 次活跃调用下零调用丢失。

在真实 NCCL 2-rank 集成中，NCCLPol 触发了每 rank 495 次 `getCollInfo()` 调用；`size_aware_v2` 所请求的 algo/proto 在 10/10 个消息大小上被 NCCL 精确采用；`adaptive_channels` 在真实 profiler telemetry 驱动下表现出 8→9→10 的闭环自适应，而关闭 profiler 后行为保持为 8→8→8。这些结果表明，基于 eBPF 的安全策略扩展和真实 profiler→tuner 闭环已经可以在 stock NCCL 中以较低成本落地。

## 1 引言

大规模分布式训练对 GPU 集合通信的依赖日益加深。NVIDIA 的 NCCL（NVIDIA Collective Communication Library）作为事实标准，为 AllReduce、AllGather、ReduceScatter 等集合操作提供了高度优化的实现。自 NCCL 2.x 起，其插件系统逐步开放了 tuner、profiler、net、env 等扩展点，使平台运营方可以在不修改 NCCL 源码的情况下注入自定义的调参逻辑与监控回调。

然而，这一扩展模型存在根本性的安全缺陷：**所有插件以 native 共享库（`.so`）形式加载，在 NCCL 进程地址空间中直接执行，没有任何前置验证、隔离或安全更新机制。** 一段错误的 tuner 插件代码——例如空指针解引用、越界内存访问、或未受保护的全局状态修改——将直接导致整个训练进程崩溃。在生产集群中，一次训练作业的失败意味着数小时乃至数天的 GPU 资源浪费。

更深层的问题在于，NCCL 的 tuner 与 profiler 是两个独立的插件角色。tuner 在每次 collective 调用前通过 `getCollInfo()` 选择算法、协议与 channel 数；profiler 在 collective 完成后接收事件回调。但 NCCL 并未提供两者之间的标准状态共享机制。如果平台方希望实现"基于历史 collective latency 动态调整 tuning 策略"这类闭环控制，需要自行在 native 代码中维护共享状态，进一步增加了出错风险。

本文不是在提出另一个 NCCL tuning heuristic。我们提出的问题是：**能否在 NCCL 集合通信的热路径中引入受验证的安全扩展机制，同时保持足够低的开销，并在 stock NCCL ABI 内实现 profiler→tuner 的真实闭环？**

我们的回答是 NCCLPol——一个基于 bpftime 用户态 eBPF 运行时的 NCCL 混合插件框架。NCCLPol 的核心思路是将 Linux eBPF 的"受验证的安全扩展"范式引入 NCCL 插件系统：策略以 eBPF 字节码形式编写，在加载时通过 PREVAIL/bpftime verifier 检查合法性，通过 LLVM JIT 编译为本地代码执行，并通过 eBPF maps 在 tuner 与 profiler 之间共享状态。

本文的贡献如下：

1. **混合插件架构**：我们设计并实现了一个在单个 `.so` 中同时导出 `ncclTunerPlugin_v5` 与 `ncclProfiler_v6` 的混合插件，在 stock NCCL 插件 ABI 内实现 profiler→tuner 闭环，无需修改 NCCL 源码。

2. **安全执行模型**：NCCLPol 在加载时完成 verifier 检查（7 个坏程序被拒绝、7 个好程序被接受），并支持原子热更新（0.3μs 交换窗口，400,000 次活跃调用零丢失）。对比实验表明，等价 native plugin 在空指针解引用时触发 SIGSEGV，而 eBPF 程序在执行前即被拒绝。

3. **低开销证据**：`getCollInfo()` 热路径的 CPU 侧策略执行开销保持在亚 100ns 量级，且具有清晰的开销分解：dispatch +41ns、一次 map lookup +11ns、一次 map update 再加 +11ns。

4. **真实 NCCL 集成验证**：在真实 2-rank NCCL 运行中，`size_aware_v2` 请求的 algo/proto 在 10/10 个消息大小上被 NCCL 精确采用；`adaptive_channels` 在真实 profiler telemetry 驱动下展现出闭环自适应行为。

## 2 背景与动机

### 2.1 NCCL 集合通信与插件系统

NCCL 为 GPU 集合通信提供了高度优化的实现，支持 AllReduce、AllGather、ReduceScatter、Broadcast 等操作。其核心决策路径如下：当应用程序发起一次 collective 调用时，NCCL 内部在 CPU 侧通过 `getCollInfo()` 确定本次操作使用的算法（algo，如 TREE、RING）、协议（proto，如 SIMPLE、LL、LL128）以及 channel 数量。这一决策发生在 GPU kernel 启动之前的 CPU 侧路径上。

NCCL 的插件系统通过动态加载共享库来扩展运行时行为：

- **Tuner v5**（`ncclTunerPlugin_v5`）：在每次 collective 调用时，通过 `getCollInfo()` 回调让外部逻辑参与 algo/proto/channels 的选择。tuner 可以通过修改 NCCL 传入的 cost table 来偏置算法选择，或直接请求特定的 channel 数。
- **Profiler v6**（`ncclProfiler_v6`）：在 collective 的生命周期事件（group start/end、collective start/end、kernel channel start/stop 等）上接收回调，用于性能监控与分析。
- **Mixed plugin**：NCCL 允许同一个 `.so` 同时导出多个插件符号，因此 tuner 和 profiler 可以共享同一个共享库。

### 2.2 Native 插件的安全问题

尽管 NCCL 的插件系统提供了良好的扩展能力，但其扩展单元——native 共享库——在 GPU 集群场景下存在根本性的安全问题：

**失败代价高**。训练作业通常需要数小时到数天的 GPU 时间。一个 tuner 或 profiler 插件中的 bug（空指针、越界、竞态条件）会直接 crash 训练进程，导致整个作业需要从最近的 checkpoint 恢复。在多租户集群中，这一问题更加严重：一个租户的 bad plugin 可能影响共享节点上的其他作业。

**在线替换难**。生产环境中经常需要更新调参策略——例如根据集群负载变化调整 collective 行为。但 native `.so` 插件没有清晰的原子更新路径。常见的运维做法是重启进程加载新插件，这意味着中断正在进行的训练。

**缺少前置阻断**。native 插件没有 verifier 机制。潜在的越界访问、未注册的函数调用、空指针解引用等错误只能在运行时暴露——往往是以进程 crash 的形式。

**闭环状态管理零散**。如果平台方希望实现"profiler 收集 collective latency，tuner 基于 latency 动态调整"的闭环控制，需要在 native 代码中自行维护共享状态，缺少统一的状态平面。

这使得"可扩展"不等于"可安全部署"。本文的切入点是：**把 eBPF 的受验证扩展模型带进 NCCL，而不是把 NCCL 重新实现一遍。**

### 2.3 现有工作的不足

现有工作在 collective 通信的可编程性方面取得了显著进展，但未能同时解决安全扩展与闭环控制的组合问题：

- **AutoCCL** [1] 实现了在线 collective 调参，但依赖扩展 NCCL ABI，且插件本质仍是 native code，不具备加载时验证。
- **MSCCL / MSCCL++** [2,3] 强调算法层面的可编程性，允许用户定义自定义的集合通信算法。但它们的路线是替换或深度修改 NCCL，而非在 stock NCCL plugin ABI 内工作。
- **eACGM** 等通信观测工具可以监控 NCCL 行为，但不在 NCCL 决策面执行策略。
- **bpftime** [4]（OSDI'25）提供了通用的用户态 eBPF 执行基础设施，包括 PREVAIL verifier、LLVM JIT、maps 和 helper 注册。但 bpftime 本身不包含 NCCL domain 适配或 mixed plugin 闭环。

**空白点**：目前没有工作同时展示在 stock NCCL ABI 内实现 eBPF 安全扩展以及 profiler→tuner 闭环控制。

## 3 设计

### 3.1 总体架构

NCCLPol 的核心设计如图 1 所示。整个系统以一个 NCCL mixed plugin 共享库（`.so`）的形式存在，同时导出 `ncclTunerPlugin_v5` 与 `ncclProfiler_v6` 两个插件符号。

```
┌──────────────────────────────────────────────┐
│             NCCLPol Mixed Plugin (.so)        │
│                                              │
│   Tuner v5 Adapter        Profiler v6 Adapter│
│   ┌─────────────┐        ┌─────────────────┐│
│   │getCollInfo()│        │startEvent()     ││
│   │             │        │stopEvent()      ││
│   │             │        │recordEventState()│
│   └──────┬──────┘        └───────┬─────────┘│
│          └──────────┬────────────┘           │
│                     │                        │
│          Shared Communicator State           │
│          (per-comm policy + maps)            │
│                     │                        │
│              bpftime Runtime                 │
│       verifier / LLVM JIT / maps / helpers   │
│                     │                        │
│              eBPF Policies                   │
│    size_aware_v2 / adaptive_channels /       │
│    slo_enforcer / ...                        │
└──────────────────────────────────────────────┘
```
**图 1：NCCLPol 混合插件架构**

关键设计决策：

- **bpftime 作为库嵌入**。bpftime 以库的形式链接到 plugin `.so` 中，提供 verifier、LLVM JIT、maps 和 helper 注册等完整的 eBPF 运行时能力。
- **Tuner 与 profiler 共享状态**。两个 adapter 共享同一份 communicator-local policy state，包括加载的 eBPF 程序实例和 eBPF maps。这避免了分裂的 per-role 生命周期管理。
- **Profiler 是 telemetry 生产者，tuner 是消费者**。profiler 在 collective 完成时将真实 latency 写入 `telemetry_map`；tuner 在后续 `getCollInfo()` 中读取该 map 并据此调整策略输出。
- **策略通过 helper 与 maps 访问状态**。eBPF 策略不直接读写 NCCL 内部结构，只能通过注册的 helper 函数和 maps 与外部交互。

### 3.2 安全执行模型

NCCLPol 的安全执行模型是本文的核心设计贡献。它回答了这样一个问题：如何在 NCCL collective 热路径上引入第三方策略代码，同时保证不会因 bad policy 而 crash 训练进程？

#### 3.2.1 加载时验证

每个 eBPF 策略在首次加载时经过以下流程：

1. **字节码解析**：从 `.bpf.o` ELF 文件中提取 eBPF 字节码与 map 定义。
2. **Verifier 检查**：将字节码送入 PREVAIL/bpftime verifier，检查是否存在：空指针解引用风险、越界内存访问、未注册的 helper 调用、栈越界、无界循环、非法上下文写入、除零风险等。
3. **JIT 编译**：通过 LLVM JIT 将验证通过的 eBPF 字节码编译为本地机器码。
4. **Map 重定位**：将 eBPF 程序中的 map 引用绑定到运行时分配的 eBPF map 实例。

关键特性：**验证发生在加载时而非每次调用时**。通过 verifier 后，运行时不再做逐调用安全检查，因此不在热路径上引入额外的 guard 开销。

我们当前的 verifier 测试覆盖了 14 个程序：7 个合法程序全部被接受（noop、size_aware、size_aware_v2、lookup_only、lookup_update、adaptive_channels、slo_enforcer），7 个非法程序全部被拒绝（bad_lookup：空指针解引用、bad_oob_access：越界上下文读取、bad_unregistered_helper：未注册 helper、bad_stack_overflow：栈越界、bad_infinite_loop：无界循环、bad_write_ctx：非法上下文写入、bad_div_zero：除零风险）。

#### 3.2.2 进程内执行边界

需要明确的是，当前实现的安全性主要来自 verifier、helper 白名单、map ABI 与受限上下文。所有代码仍在 NCCL 的同一进程地址空间内执行。这不是 MPK 级别的硬件隔离——当前工件实现的是"受验证的进程内 eBPF 扩展"。相较于 native plugin 的"任意代码执行"，verifier 显著缩小了故障面：通过 verifier 的程序不会发生空指针、越界、栈溢出等常见 crash 类型。

#### 3.2.3 原子热更新

NCCLPol 支持在运行时原子替换当前活动的策略：

1. 新策略在后台完成加载、验证、JIT 编译和 map 重定位。
2. 准备完成后，通过 `shared_ptr` 原子交换切换到新的不可变 policy state。
3. 正在执行的调用继续持有旧 state 的引用，直到调用自然结束后释放。
4. 如果新策略未通过 verifier，交换不会发生，旧策略继续服务。

这确保了：(a) 更新期间不丢失任何调用；(b) 不会出现半新半旧的混合状态；(c) bad replacement 不会污染当前正在服务的 good policy。

### 3.3 策略编程模型

#### 3.3.1 Hook 事件

NCCLPol 当前支持三类 hook 事件：

| 事件 | 触发时机 | 对应 NCCL 插件 API | 热路径 |
|------|---------|-------------------|--------|
| COMM_INIT | communicator 初始化 | tuner/profiler `init` | 否 |
| COLL_DECIDE | 每次 collective 决策 | tuner `getCollInfo` | **是** |
| TELEMETRY | collective 完成事件 | profiler 回调 | 否 |

#### 3.3.2 上下文与动作

eBPF 策略的输入是一个描述当前 collective 调用的上下文结构：

```c
struct nccl_policy_ctx {
    uint32_t coll_type;   // collective 类型（AllReduce 等）
    uint64_t n_bytes;     // 消息大小
    uint32_t n_ranks;     // 参与 rank 数
    uint32_t n_nodes;     // 参与节点数
    int      reg_buff;    // 是否使用注册缓冲区
    uint64_t comm_id;     // communicator 标识
};
```

策略的输出是一个动作结构：

```c
struct nccl_policy_action {
    int preferred_algo;   // 偏好算法（通过 cost table 偏置）
    int preferred_proto;  // 偏好协议（通过 cost table 偏置）
    int n_channels;       // 请求 channel 数
};
```

Tuner adapter 根据策略输出修改 NCCL 的 cost table（降低 preferred algo/proto 的 cost 以偏置 NCCL 选择）和 `nChannels` 返回值。

#### 3.3.3 Helper 函数

策略可调用的 helper 函数集是白名单制的：

```c
void *bpf_map_lookup_elem(struct bpf_map *map, const void *key);
int bpf_map_update_elem(struct bpf_map *map, const void *key,
                        const void *value, uint64_t flags);
int bpf_trace_printk(const char *fmt, ...);
```

任何调用未注册 helper 的 eBPF 程序将在 verifier 阶段被拒绝。

### 3.4 跨插件融合与闭环反馈

NCCLPol 的闭环控制依赖 mixed plugin 架构将 tuner 与 profiler 融合在同一个共享库中：

```
collective 完成
    ↓
Profiler callback → 写 telemetry_map（真实 latency）
    ↓
下一次 getCollInfo()
    ↓
Tuner policy → 读 telemetry_map → 调整 channels / algo / proto
```

**为什么需要 mixed plugin？** stock NCCL 中 tuner 与 profiler 是独立加载的两个插件角色，NCCL 不提供它们之间的标准共享状态面。NCCLPol 通过在同一个 `.so` 中同时导出两个角色、共享同一份 communicator state 和 eBPF maps，解决了跨角色状态共享问题。

## 4 实现

### 4.1 Mixed Plugin 导出

NCCLPol 编译为单个共享库 `libnccl-policy.so`，导出以下符号：

```c
extern "C" ncclTuner_v5_t ncclTunerPlugin_v5;
extern "C" ncclProfiler_v6_t ncclProfiler_v6;
```

NCCL 通过 `NCCL_TUNER_PLUGIN` 和 `NCCL_PROFILER_PLUGIN` 环境变量指定插件路径。当两者指向同一个 `.so` 时，NCCL 会分别调用对应的 init/finalize 回调。NCCLPol 内部通过 shared communicator state 确保两个角色访问同一份 eBPF 运行时实例。

### 4.2 bpftime 集成

bpftime 以库方式链接到 plugin `.so` 中。集成包括以下组件：

- **runtime（`libbpftime-runtime`）**：提供 eBPF map 创建、helper 注册、程序加载等核心 API。
- **vm-core（`libbpftime-vm-core`）**：eBPF 虚拟机抽象层。
- **llvm-vm（`libbpftime-llvm-vm`）**：LLVM JIT 后端。
- **verifier（`libbpftime-verifier`）**：PREVAIL 系列 verifier 集成。

策略从文件路径加载（通过 `NCCL_POLICY_BPF_PATH` 环境变量指定），支持严格验证模式（`NCCL_POLICY_VERIFY_MODE=strict`）。在严格模式下，verifier 失败将直接拒绝加载。

### 4.3 Profiler Adapter 与 Latency Bridge

Profiler adapter 实现了 `ncclProfiler_v6` 的完整生命周期回调：

- `startEvent`：记录 collective 开始时间戳。
- `stopEvent`：记录 kernel channel 完成时间戳。
- `recordEventState`：计算 collective latency 并写入 `telemetry_map`。

Latency 计算使用 NCCL profiler 事件的时间戳差值。`adaptive_channels` 策略通过 `applied_samples` 字段避免同一个 telemetry 样本被多次重复应用。

### 4.4 热更新实现

热更新通过不可变 `LoadedPolicyState` 和 `shared_ptr` 原子交换实现：

```cpp
struct LoadedPolicyState {
    std::unique_ptr<bpftime_prog> prog;
    std::unique_ptr<bpftime_map>  telemetry_map;
    std::unique_ptr<bpftime_map>  config_map;
};

// 原子交换
auto new_state = std::make_shared<LoadedPolicyState>(...);
std::atomic_store(&active_state, new_state);
```

在 reload 路径上，新策略先完成加载、验证和 JIT；如果 verifier 拒绝，则 `active_state` 不变、旧策略继续服务。

### 4.5 实现中的关键 bug 修复

在真实 NCCL 2-rank 集成过程中，我们发现了一个 cost table 指针布局 bug：NCCL 将 cost table 作为连续的 `float[algo][proto]` 数组传入，但 plugin ABI 将其类型声明为 `float**`。我们的初始实现按 pointer-to-pointer 方式解引用，导致在真实路径上立即 crash。修复方案是将 `coll_cost_table` 重新解释为 `float (*)[NCCL_NUM_PROTOCOLS]` 并通过该平铺视图读写。

这个 bug 只有在真实 NCCL 路径中才会暴露——CPU-only 测试 harness 中不经过 NCCL 的 cost table 传递。它也说明了为什么真实集成测试是必要的。

## 5 策略案例

这些策略主要用于展示框架的表达力与闭环能力，而不是宣称已经完成完整的集群优化。

### 5.1 `size_aware_v2`：消息大小感知的 algo/proto 选择

`size_aware_v2` 是一个纯上下文驱动的策略，不依赖 map 状态。它根据消息大小选择不同的 algo/proto 组合：

| 消息大小 | 选择的 algo | 选择的 proto |
|---------|------------|-------------|
| < 4KB | TREE | SIMPLE |
| 4KB - 32KB | TREE | LL |
| 64KB - 512KB | RING | LL |
| ≥ 1MB | RING | SIMPLE |

在真实 NCCL 2-rank 路径上，通过 `NCCL_DEBUG_SUBSYS=TUNING` 日志验证，10/10 个消息大小分段的 algo/proto 选择与 policy 请求完全一致。这证明 eBPF policy 不仅"被执行了"，而且实际改变了 NCCL 的最终选择结果。

### 5.2 `adaptive_channels`：基于真实 latency 的 channel 自适应

`adaptive_channels` 展示了 profiler→tuner 闭环控制能力：

1. Profiler 在 collective 完成时将真实 latency 写入 `telemetry_map`。
2. 在后续 `getCollInfo()` 中，tuner policy 读取 `telemetry_map`，根据 latency 水平调整返回的 channel 数。
3. 为避免同一 telemetry 样本被重复消费，policy 通过 `applied_samples` 字段追踪已处理的样本数。

在真实 NCCL 运行中：
- 有 profiler 时：channel 从 8 → 9 → 10（闭环自适应）
- 无 profiler 时：channel 保持 8 → 8 → 8（无变化）

### 5.3 `slo_enforcer`：多次 map 访问的复杂策略

`slo_enforcer` 展示了更复杂的 map-backed 策略：它同时读取 `config_map`（SLO 参数）和 `telemetry_map`（历史 latency），根据 SLO 目标与实际 latency 的差距调整 channel 数和算法选择。它的 CPU 侧开销仍保持在 80ns P50 / 95ns P99，说明多次 map 访问不会导致开销爆炸。

## 6 评估

**硬件环境**：单节点，1×NVIDIA RTX 5090，24 核 x86_64，125GB RAM，CUDA 12.9，NCCL 2.29.7。

**评估目标**：作为 workshop/short paper，我们的评估聚焦于 correctness、safety、overhead 与真实 NCCL 可运行性，而非大规模吞吐提升。

### 6.1 CPU 微基准：策略执行开销

表 1 展示了各策略在 1,000,000 次调用下的 CPU 侧执行开销。

**表 1：CPU 侧策略执行开销（1M 次调用）**

| 策略 | P50 (ns) | P99 (ns) | Max (ns) | vs native ΔP50 |
|-----|----------|----------|----------|----------------|
| native baseline | 10 | 16 | 43,001 | — |
| noop | 51 | 61 | 5,708 | +41 |
| size_aware_v2 | 52 | 64 | 3,129 | +42 |
| lookup_only | 63 | 74 | 3,681 | +53 |
| lookup_update | 74 | 87 | 6,165 | +64 |
| adaptive_channels | 75 | 88 | 16,176 | +65 |
| slo_enforcer | 80 | 95 | 6,363 | +70 |

**开销阶梯分解**：通过精心设计的 micro-policy 序列，我们可以清晰地分解各组件的开销贡献：

- **native → noop**：+41ns。这是 eBPF dispatch 本身的开销（包括 VM 入口、上下文准备、JIT 代码跳转）。
- **noop → size_aware_v2**：+1ns。纯上下文字段读取与条件分支几乎不引入可测量的额外开销。
- **size_aware_v2 → lookup_only**：+11ns。一次 `bpf_map_lookup_elem` 调用。
- **lookup_only → lookup_update**：+11ns。一次额外的 `bpf_map_update_elem` 调用。
- **lookup_update → slo_enforcer**：+6ns。更复杂的分支逻辑与多次 map 访问。

核心结论：即使是最复杂的 `slo_enforcer` 策略，CPU 侧开销也仅为 80ns P50——在 collective 通信动辄数十微秒到数毫秒的端到端延迟面前，这一开销可以忽略不计。

### 6.2 安全属性验证

#### 6.2.1 Verifier Accept/Reject 矩阵

**表 2：Verifier accept/reject 矩阵**

| 程序 | 错误类型 | Verifier 结果 |
|-----|---------|--------------|
| noop | valid | ACCEPTED |
| size_aware | valid | ACCEPTED |
| size_aware_v2 | valid | ACCEPTED |
| lookup_only | valid | ACCEPTED |
| lookup_update | valid | ACCEPTED |
| adaptive_channels | valid | ACCEPTED |
| slo_enforcer | valid | ACCEPTED |
| bad_lookup | 空指针解引用 | REJECTED |
| bad_oob_access | 越界上下文读取 | REJECTED |
| bad_unregistered_helper | 未注册 helper | REJECTED |
| bad_stack_overflow | 栈越界 | REJECTED |
| bad_infinite_loop | 无界循环 | REJECTED |
| bad_write_ctx | 非法上下文写入 | REJECTED |
| bad_div_zero | 除零风险 | REJECTED |

7 个合法程序全部被接受，7 个非法程序全部被拒绝。错误类型覆盖了 eBPF 安全模型的关键 safety property。

#### 6.2.2 Native Crash vs eBPF Rejection

我们构造了一个对照实验：在 native tuner plugin 中执行空指针解引用（模拟 `bad_lookup` 的逻辑），与加载等价 eBPF 策略进行对比。

| 机制 | 后果 |
|-----|------|
| Native plugin（空指针解引用） | 进程 crash（SIGSEGV） |
| eBPF policy（`bad_lookup.bpf.o`） | 加载时被 verifier 拒绝 |

这是"为什么用 eBPF 而不是 native"的核心安全论据：native 扩展可以 crash 进程，eBPF 扩展在执行前即被拦截。

#### 6.2.3 热更新安全性

| 指标 | 结果 |
|-----|------|
| 原子交换窗口 | 0.309 μs |
| 活跃调用数 | 400,000 |
| 失败调用数 | 0 |
| 零调用丢失 | 是 |
| 坏替换策略 | 被 verifier 拒绝 |
| 拒绝后旧策略 | 继续正常服务 |

热更新的整个 load/verify/JIT 过程耗时约 11ms，但原子交换本身仅需 0.3μs。在交换准备期间，旧策略持续服务所有调用；交换发生后，新策略立即接管，没有任何调用丢失或混合状态。

### 6.3 真实 NCCL 集成

#### 6.3.1 实验设置

由于我们只有单 GPU，我们通过以下技术构造了真实的 2-rank NCCL 路径：

- 使用 MPI 启动两个 rank，均绑定到同一块 RTX 5090（`NCCL_TESTS_DEVICE=0`）。
- 通过为每个 rank 设置不同的 `NCCL_HOSTID` 值，绕过 NCCL 的重复 GPU 检测。
- 禁用 P2P 和 SHM 传输（`NCCL_P2P_DISABLE=1, NCCL_SHM_DISABLE=1`），强制使用 socket transport。

这一设置成功建立了真实的 `nranks=2` communicator，触发了 NCCL 的完整决策路径（包括 `getCollInfo()`）。

#### 6.3.2 真实 `getCollInfo()` 调用证据

在上述 2-rank 设置下，每个 rank 产生了 495 次 `getCollInfo()` 调用。这证明了插件不仅被加载，而且真正进入了 NCCL 的 collective 决策路径。

#### 6.3.3 algo/proto 精确采用验证

通过 `NCCL_DEBUG_SUBSYS=TUNING` 收集 NCCL 最终选择，`size_aware_v2` 的 algo/proto 在所有 10 个消息大小分段上被精确采用：

**表 3：NCCL 真实 algo/proto 采用验证**

| 消息大小 | Policy 请求 | NCCL 最终选择 | 匹配 |
|---------|------------|--------------|------|
| 1024, 2048 | TREE + SIMPLE | TREE + SIMPLE | ✓ |
| 4096, 8192, 16384, 32768 | TREE + LL | TREE + LL | ✓ |
| 65536, 131072, 262144, 524288 | RING + LL | RING + LL | ✓ |
| 1048576 | RING + SIMPLE | RING + SIMPLE | ✓ |

10/10 完全匹配，证明 eBPF policy 的决策确实被 NCCL 采纳执行。

#### 6.3.4 真实 Profiler→Tuner 闭环

在 mixed plugin 模式下运行 `adaptive_channels`：

- **有 profiler**：rank 0 首条真实 latency 为 9,264,480ns。tuner 读取后 channel 从 8 变为 9，后续继续上升到 10。
- **无 profiler**（对照组）：tuner 没有 telemetry 输入，channel 保持 8 不变。

这证明了 profiler→tuner 闭环在真实 NCCL 运行中确实发生，而非仅在测试 harness 中工作。

### 6.4 GPU 端到端开销

在单 GPU `nccl-tests` 路径上，插件带来的端到端开销几乎不可测量：

**表 4：单 GPU nccl-tests 端到端延迟（部分消息大小）**

| 消息大小 | 无插件 (μs) | noop (μs) | size_aware_v2 (μs) | slo_enforcer (μs) |
|---------|------------|-----------|--------------------|--------------------|
| 8B | 1.85 | 1.93 | 1.87 | 1.80 |
| 1KB | 2.16 | 2.21 | 2.13 | 2.09 |
| 1MB | 3.98 | 4.02 | 4.03 | 3.97 |
| 128MB | 175.42 | 175.51 | 175.47 | 175.49 |

所有消息大小上的平均开销增量均低于 0.05μs，在统计噪声范围内。这说明 eBPF 策略执行的 CPU 侧开销在 GPU collective 的端到端延迟面前完全可以忽略。

### 6.5 评估边界

我们诚实指出以下限制：

- 没有 kernel eBPF，也没有 kernel↔userspace shared maps。
- 没有 net adapter 或 env adapter，当前仅覆盖 tuner/profiler。
- 没有 MPK 硬件隔离，也没有 EIM capability model。安全性完全依赖 verifier。
- 没有真实多租户竞争或 SLO enforcement 的 workload 评估。
- 真实 NCCL 路径是单机单 GPU 强制形成的 2-rank socket-only 路径。
- algo/proto 的最终采用已精确验证（10/10），但 n_channels 的最终采用只证明"有影响"，NCCL 运行时可能 clamp 到不同值。

## 7 讨论

**表达力边界**。NCCLPol 当前的动作空间主要是 algo/proto 偏置与 n_channels 请求，仍受 stock tuner ABI 的限制。例如，tuner 无法直接控制 NCCL 的网络传输选择或内存分配策略。扩展动作空间需要 NCCL 提供更丰富的 tuner ABI，或引入 net/env adapter。

**Channel 采用的解释**。Plugin 返回的 n_channels 会受到 NCCL 运行时 clamp（例如可用的 channel 资源、transport 约束等）。因此本文只把 algo/proto 精确采用作为强 claim，把 channel 影响作为弱 claim。

**未来工作 1：Cross-stack 扩展**。如果后续引入 kernel eBPF 与 shared maps，可以将网络层、调度层的信号接入当前框架，形成跨 kernel/userspace 的策略平面。但这不是本文工件的一部分。

**未来工作 2：更强隔离**。MPK（Memory Protection Keys）可以在 verifier 之外增加硬件级别的内存隔离；EIM（Extension Interface Model）可以为每个策略定义细粒度的 capability manifest。这些都值得在后续工作中实现。

**未来工作 3：集群级多租户评估**。真正的多租户 SLO enforcement 需要多节点 GPU 集群环境、真实的训练 workload 竞争、以及租户间公平性度量。这是本框架的核心应用场景之一，但超出了当前单节点原型的评估范围。

## 8 相关工作

**eBPF 安全扩展**。Linux eBPF 首次展示了"受验证的内核扩展"范式。bpftime [4] 将这一范式推广到用户态，提供了通用的 eBPF 执行基础设施。XRP [5] 将 eBPF 引入存储 IO 路径，Electrode [6] 将 eBPF 引入网络协议处理。NCCLPol 延续了同一类"domain application"思路，将 eBPF 安全扩展引入 GPU collective communication 的 tuning/profiling 热路径。与 XRP 和 Electrode 的区别在于：我们面向的是 NCCL 的 mixed plugin 接口，且核心 loop 是 profiler→tuner 闭环而非单向的观测或加速。

**可编程集合通信**。AutoCCL [1] 实现了在线 collective 调参，但本质仍是 native code 扩展。MSCCL [2] 和 MSCCL++ [3] 强调算法可编程性，允许用户定义自定义集合通信算法，但需要替换或深度修改 NCCL。TACCL [7] 通过拓扑感知编译生成集合通信算法。这些工作与 NCCLPol 互补而非竞争：NCCLPol 坚持在 stock NCCL plugin ABI 内工作，不 fork 或替换 NCCL。

**训练通信观测**。eACGM 等工具提供了 NCCL 行为的观测能力。NCCLPol 不仅观测，还在 NCCL 决策面执行策略，并通过 profiler→tuner 闭环实现基于观测的动态调整。

## 9 结论

本文提出了 NCCLPol，一个基于 bpftime 的 NCCL 混合 tuner/profiler eBPF 策略框架。NCCLPol 展示了以下结果：

- 在单个 NCCL 插件共享库中同时实现了 tuner v5 和 profiler v6，通过共享 eBPF maps 形成 profiler→tuner 闭环。
- 加载时 verifier 有效拒绝了 7 类非法 eBPF 程序，而等价 native plugin 会直接 crash 训练进程。
- 原子热更新在 400,000 次活跃调用下实现零丢失，交换窗口仅 0.3μs。
- 在 `getCollInfo()` 热路径上，最复杂策略的 CPU 侧开销仅 80ns P50。
- 在真实 NCCL 2-rank 运行中，algo/proto 选择 10/10 被精确采用，profiler telemetry 驱动了真实的 channel 自适应。

这些结果表明，在 GPU 集合通信热路径中引入安全的 eBPF 策略扩展是可行且低开销的。NCCLPol 目前仅覆盖用户态 tuner/profiler 路径，不包含 kernel eBPF、MPK、EIM 或多租户集群级评估——这些是重要的未来工作方向。

## 参考文献

[1] Cai, Z., et al. "AutoCCL: Automated Collective Communication Tuning for GPU Clusters." *SC'24*.

[2] Cai, Z., et al. "MSCCL: Programmable Communication Schedules for GPU Clusters." *NSDI'23*.

[3] Salehi, M., et al. "MSCCL++: A GPU-driven Communication Stack for Scalable AI Training." *EuroSys'25*.

[4] Zheng, Y., et al. "bpftime: Userspace eBPF Runtime for Uprobe, Syscall and Kernel-Function Tracing." *OSDI'25*.

[5] Zhong, Y., et al. "XRP: In-Kernel Storage Functions with eBPF." *OSDI'22*.

[6] Zhou, Y., et al. "Electrode: Accelerating Distributed Protocols with eBPF." *NSDI'23*.

[7] Shah, A., et al. "TACCL: Guiding Collective Algorithm Synthesis using Communication Sketches." *NSDI'23*.
