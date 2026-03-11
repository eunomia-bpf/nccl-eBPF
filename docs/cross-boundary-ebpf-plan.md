# Cross-Boundary eBPF for GPU Collective Communication

## 核心论点

eBPF 是唯一能跨越内核-用户空间边界、用统一语言和共享状态（pinned maps）实现端到端 policy 的机制。当前 NCCLbpf 只用了用户态一半；将内核态 eBPF 网络观测与用户态 NCCL policy 打通，实现 **网络感知的集合通信自适应**，是质的飞跃。

## 目标会议

Euro-Par 2025/2026, EuroSys, NSDI（12-14 页 full paper）

## 研究问题

**NCCL 的算法选择是静态的**：在 communicator 创建时基于拓扑选定 algorithm/protocol/channels，之后不再改变。但在多节点 GPU 集群中，网络状态是动态的（拥塞、干扰、故障）。NCCL tuner plugin 看不到网络层信息，无法做网络感知的自适应。

**Cross-boundary eBPF** 解决这个问题：内核 eBPF（tc/XDP/tracepoint）实时观测网络状态，通过 pinned BPF map 将信息传递给 NCCL 用户态 eBPF policy，policy 据此动态调整集合通信参数。

---

## Phase 0: 环境调研（必须先完成）

### 0.1 当前机器网络配置
- [ ] 查看网卡类型（InfiniBand? RoCE? 纯 Ethernet?）
- [ ] 查看 NCCL 在多 GPU 时使用的传输路径（NVLink intra-node, 什么 inter-node?）
- [ ] 是否有多节点可用？还是只有这一台 8-GPU 机器？
- [ ] 内核版本和 BPF 支持情况（BTF, bpf_link, tc/XDP attach）

### 0.2 bpftime pinned map 能力
- [ ] bpftime 是否支持 pinned BPF map（与内核 eBPF 共享）？
- [ ] 如果不支持，实现难度如何？替代方案？
- [ ] 内核 eBPF map 与 bpftime userspace map 的内存模型差异

### 0.3 NCCL 网络层调研
- [ ] NCCL net plugin API（v9）的 transport 抽象
- [ ] NCCL 在 InfiniBand/RoCE 上的 transport 选择逻辑
- [ ] NCCL 的 NCCL_SOCKET_NTHREADS, NCCL_NSOCKS_PERTHREAD 等网络参数是否可被 tuner 影响
- [ ] NCCL 的错误恢复机制（timeout, retry）

### 0.4 内核 eBPF 网络 hook 点调研
- [ ] tc eBPF: 能挂在 RDMA 网卡上吗？对 RoCE 有效吗？
- [ ] XDP: 对 InfiniBand verbs 传输有意义吗？
- [ ] tracepoint: rdma_core, ib_verbs 有哪些 tracepoint 可用？
- [ ] kprobe: NCCL 通过 libibverbs 调用内核，哪些函数可 probe？
- [ ] 能获取什么信息：ECN 标记、重传次数、QP 状态、RTT？

---

## Phase 1: 可行性验证（PoC）

### 场景选择（根据 Phase 0 调研结果选一个）

**场景 A: 纯 NVLink 单节点 + 模拟网络干扰**
- 如果没有 InfiniBand，可在 NVLink 场景下用 tc netem 模拟延迟/丢包
- 内核 tc eBPF 观测模拟的网络状况
- NCCL net plugin（socket transport）读取内核观测，调整行为
- 优点：当前机器即可做；缺点：不是真实 RDMA 场景

**场景 B: Socket transport + tc eBPF 拥塞感知**
- NCCL 的 socket transport 走 TCP/IP 栈，tc eBPF 可以完全观测
- 内核 eBPF 在 tc egress/ingress 观测 RTT、拥塞窗口、重传
- 用户态 policy 根据拥塞信号调整 algorithm/channels/aggressiveness
- 优点：技术上最直接可行；缺点：socket transport 性能不代表生产环境

**场景 C: 真实 InfiniBand + RDMA tracepoint**
- 如果有 IB 网卡，用 rdma tracepoint 或 ib_verbs kprobe
- 内核 eBPF 观测 QP 状态、completion latency、congestion signals
- 优点：最接近生产环境；缺点：需要 IB 硬件和多节点

**场景 D: GPU PCIe 带宽竞争感知**
- 8 GPU 通过 PCIe 连接 CPU，GPU 之间的 P2P 可能走 PCIe
- 内核 eBPF 通过 perf events 或 PMU 观测 PCIe 带宽使用
- NCCL policy 在 PCIe 拥塞时切换策略
- 优点：单节点可做；缺点：B300 NVLink 可能不走 PCIe

### PoC 实现
- [ ] 写一个最简单的内核 eBPF 程序（tc/tracepoint），观测一个指标，写入 pinned map
- [ ] 修改 NCCL tuner eBPF policy，读取 pinned map 中的内核观测值
- [ ] 验证跨边界数据流通：内核写 → pinned map → 用户态读
- [ ] 端到端 demo：触发网络事件 → 内核 eBPF 检测 → NCCL policy 反应

---

## Phase 2: 系统设计

### 2.1 架构
```
┌─────────────────────────────────────────────────┐
│ Kernel Space                                     │
│                                                   │
│  tc/XDP eBPF ──────┐    tracepoint eBPF ────┐   │
│  (packet-level)     │    (RDMA events)       │   │
│                     ▼                         ▼   │
│              ┌──────────────┐                     │
│              │ Pinned BPF   │                     │
│              │ Maps (shared)│                     │
│              └──────┬───────┘                     │
├─────────────────────┼─────────────────────────────┤
│ User Space          │                             │
│              ┌──────▼───────┐                     │
│              │ bpftime map  │                     │
│              │ (read-only)  │                     │
│              └──────┬───────┘                     │
│                     │                             │
│  ┌──────────────────▼──────────────────────┐     │
│  │ NCCL Tuner eBPF Policy                   │     │
│  │  - read network state from pinned map    │     │
│  │  - read profiler telemetry from local map│     │
│  │  - output: algo/proto/channels           │     │
│  └──────────────────────────────────────────┘     │
│                                                   │
│  NCCL Process (unchanged)                         │
└───────────────────────────────────────────────────┘
```

### 2.2 Map Schema
```c
// 内核 → 用户态: 网络状态
struct network_state {
    __u64 timestamp_ns;
    __u32 rtt_us;           // 当前 RTT
    __u32 retransmits;      // 累计重传次数
    __u32 ecn_marks;        // ECN 标记计数
    __u8  congestion_level; // 0=normal, 1=mild, 2=severe
};

// 用户态 profiler → tuner: 集合通信延迟
struct coll_state {
    __u64 avg_latency_ns;
    __u32 n_channels;
    __u8  algorithm;
    __u8  protocol;
};
```

### 2.3 Policy 逻辑
```c
SEC("tuner")
uint64_t network_aware_policy(struct nccl_policy_ctx *ctx) {
    // 读取内核网络观测（pinned map）
    __u32 key = 0;
    struct network_state *net = bpf_map_lookup_elem(&net_state_map, &key);

    // 读取本地 profiler 数据
    struct coll_state *coll = bpf_map_lookup_elem(&coll_map, &key);

    if (net && net->congestion_level >= 2) {
        // 网络拥塞: 切换到低带宽需求的 Tree 算法，减少 channel
        return pack_action(TREE, LL, min_channels, ...);
    }

    // 正常状态: 用 size-aware 策略
    if (ctx->n_bytes >= 4M && ctx->n_bytes <= 128M)
        return pack_action(RING, LL128, 32, ...);

    return 0; // default
}
```

---

## Phase 3: 实验设计

### 3.1 Baseline
- NCCL 默认（静态算法选择，无网络感知）
- NCCL + 用户态 eBPF only（当前 NCCLbpf，size-aware but not network-aware）

### 3.2 实验变量
- **网络干扰注入**: tc netem 延迟/丢包，或真实多 job 干扰
- **消息大小**: 全范围 sweep
- **GPU 数量**: 2/4/8 GPU（单节点），多节点（如可用）

### 3.3 指标
- 集合通信吞吐量和延迟（busbw, algbw）
- Policy 反应时间（从网络事件到 NCCL 策略切换的延迟）
- 跨边界 map 读取开销
- 与静态最优配置的差距

### 3.4 Case Studies
1. **拥塞自适应**: 注入网络拥塞 → policy 自动降级 → 拥塞消除后恢复
2. **多 Job 干扰**: 两个 NCCL job 竞争带宽 → policy 协调公平共享
3. **故障恢复**: 模拟链路降级 → policy 切换传输路径

---

## Phase 4: 论文扩展

### 新增内容（相对当前 6 页 workshop paper）
1. **Cross-boundary eBPF 架构**（新 Section 3.x）
2. **内核 eBPF hook 设计**（新 Section 4.x）
3. **网络感知 policy 语义**（扩展 Section 3）
4. **多节点实验**（扩展 Section 5）
5. **拥塞/干扰/故障 case studies**（新 Section 5.x）
6. **性能模型: 跨边界开销分析**（新 Section 5.x）

### 论文结构（14 页）
1. Introduction (1.5p)
2. Background: NCCL + eBPF + 网络感知挑战 (2p)
3. Design: cross-boundary architecture (2.5p)
4. Implementation (2p)
5. Evaluation (4p)
6. Related Work (1p)
7. Conclusion (0.5p)

---

## Phase 0 调研结果 ✅

1. **无 InfiniBand / RoCE** — 单网卡 virtio-net 虚拟机（DataCrunch），只有 TCP → **场景 B（socket + tc eBPF）**
2. **无多节点** — 单机 8 GPU，无集群 → 单节点实验，NCCL_P2P_DISABLE=1 强制走 net transport 模拟
3. **bpftime 支持 kernel map 共享** — `array_map_kernel_user` / `hash_map_kernel_user` 通过 `bpf_map_get_fd_by_id` 与内核 map 双向同步
4. **getCollInfo 每次 collective call 都调用** — 在 `enqueue.cc:2054`，热路径，policy 可实时生效
5. **内核 6.8 + BTF + bpftool 7.4** — tc/XDP eBPF 开发环境完备

---

## Phase 0 深度调研结果 ✅

### tc eBPF 网络观测能力
- tc sched_cls 在 lo ingress 可观测 NCCL socket TCP 流量
- `bpf_tcp_sock(skb->sk)` 可获取 `srtt_us`, `snd_cwnd`, `total_retrans`, `retrans_out`
- `skb->sk` 在 lo ingress 是否非 NULL 需实测（若 NULL 可 fallback 到 `bpf_sk_lookup_tcp`）
- NCCL socket 使用动态端口，但单机 lo 上流量有限，可观测全部

### bpftime kernel map 共享机制
- `array_map_kernel_user`: 若内核 map 设 `BPF_F_MMAPABLE`，mmap 零拷贝读取 <10ns
- `hash_map_kernel_user`: syscall 路径，~1-5us
- 用户态 eBPF 通过标准 `bpf_map_lookup_elem` helper 透明访问
- **需修改 plugin.cpp**: 当前 `create_maps_from_bpf_object()` 不支持 kernel-user map 注册

### NCCL socket transport
- 动态端口（bind port=0），通过 socket cookie 或全量观测识别
- 8-GPU ring: 约 8-16 个 TCP 连接（默认 nSocks=1）
- `NCCL_P2P_DISABLE=1 NCCL_SHM_DISABLE=1 NCCL_SOCKET_IFNAME=lo` 强制全部走 socket over lo

### tc netem 干扰注入
- lo 上可用 netem（`sch_netem.ko.zst` 已存在）
- 可注入 delay, jitter, loss, reorder, duplicate
- netem 在 egress，eBPF observer 在 ingress，互不干扰
- TCP RTT/重传会反映 netem 注入的变化

### PoC 实现路径

**内核侧**（新文件）:
- `src/tc_observer/nccl_tc_observer.bpf.c`: tc sched_cls 程序，挂在 lo ingress
- 读取 TCP metrics → 写入 `BPF_F_MMAPABLE` array map → pin 到 `/sys/fs/bpf/nccl_net_state`
- 独立 loader 脚本（需 root）加载 tc eBPF

**用户侧**（修改现有文件）:
- `plugin.cpp`: 在 init 时 `bpf_obj_get("/sys/fs/bpf/nccl_net_state")` 获取 kernel map fd
- 创建 `BPF_MAP_TYPE_KERNEL_USER_ARRAY` 传入 kernel_bpf_map_id
- 新 eBPF policy `net_aware_policy.bpf.c`: 读取 net_state_map + size-aware 逻辑

**实验流程**:
1. 加载 tc eBPF observer 到 lo ingress（root）
2. 启动 NCCL with `P2P_DISABLE=1 SHM_DISABLE=1 SOCKET_IFNAME=lo`
3. 运行 baseline（无干扰 + 无 policy）
4. 注入 netem delay → 观测 policy 反应
5. 撤除 netem → 观测 policy 恢复

---

## 风险评估（更新后）

| 风险 | 影响 | 缓解 |
|------|------|------|
| `skb->sk` 在 lo ingress 为 NULL | tc eBPF 无法直接获取 TCP metrics | 用 `bpf_sk_lookup_tcp()` + 解析包头 fallback |
| plugin.cpp 不支持 kernel-user map | eBPF policy 无法读取内核 map | 需修改 plugin.cpp 添加 kernel-user map 注册 |
| socket transport 性能太低 | 实验结果不代表生产 | 论文定位为架构验证，性能优化在 IB 环境补充 |
| 单节点实验不够有说服力 | Euro-Par 审稿质疑 scale | 强调跨边界架构贡献 + NVLink 上的算法选择实验互补 |
| 内核 eBPF 需要 root 权限 | 部署受限 | 论文讨论：tc observer 由 cluster admin 部署，NCCL plugin 由 user 运行 |
| netem 影响所有 lo 流量 | 非 NCCL 流量也受干扰 | 单机测试环境影响有限；可用 cgroup classid 精确控制 |
