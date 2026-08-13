# Camera-ready 最小写作修改建议

说明：以下行号均指当前版本的 `docs/paper/paper.tex`。建议只替换列出的句子或段落，不调整章节结构、实验结果或技术主张。第 1 项在全文有多处同义绝对表述，因此需要同步修改，避免前后矛盾。

## 1. Review #3C：收窄 “no cross-plugin communication” 的 claim

### 1.1 引言中的问题定义（第 154--156 行）

原文：

```latex
Moreover, NCCL plugins lack
structured state-sharing capabilities, making closed-loop policy
adjustments across plugins impossible.
```

建议改为：

```latex
Moreover, NCCL provides no general structured state-sharing interface
across plugin types, so profiler-to-tuner feedback requires ad hoc
native mechanisms.
```

这样把 claim 从“完全不能跨插件通信”收窄为“没有通用、结构化的跨插件共享状态接口”，也不再使用 `impossible` 这一绝对措辞。

### 1.2 引言中的系统属性（第 174--180 行）

原文：

```latex
\sysname provides three properties: (1)~load-time static
verification guarantees safety before execution, preventing crashes
and state corruption; (2)~structured, typed maps enable composable,
cross-plugin state sharing, enabling previously impossible
closed-loop adaptations; and (3)~atomic policy hot-reload
capability allows operators to update policies seamlessly at
runtime without interruption.
```

建议改为：

```latex
\sysname provides three properties: (1)~load-time static
verification guarantees safety before execution, preventing crashes
and state corruption; (2)~structured, typed maps provide a general
shared-state path across plugin types, enabling closed-loop adaptation
without ad hoc native shared memory; and (3)~atomic policy hot-reload
capability allows operators to update policies seamlessly at
runtime without interruption.
```

这里删除 `previously impossible`，同时保留 typed-map 机制的实际增量。

### 1.3 背景中明确承认 NCCL 已有的特定通信路径（第 233--235 行）

原文：

```latex
Despite this growing ecosystem, plugins
remain independent extension points with no cross-plugin
communication and no safety verification.
```

建议改为：

```latex
Despite this growing ecosystem, NCCL exposes only a specific
net-to-profiler event path through \texttt{ncclProfilerCallback\_t},
not a general shared-state interface among tuner, profiler, and net
plugins; plugin code also receives no safety verification.
```

这是对 reviewer 指出的 API 事实的直接回应：承认 Net$\rightarrow$Profiler callback，同时把缺口限定为通用共享状态，尤其是本文需要的 Profiler$\rightarrow$Tuner 闭环。

### 1.4 代码示例后的总结（第 384--386 行）

原文：

```latex
The two programs share \texttt{latency\_map} via the eBPF map
subsystem, a capability absent from NCCL's native plugin
architecture, where tuner and profiler have no shared state.
```

建议改为：

```latex
The two programs share \texttt{latency\_map} via the eBPF map
subsystem. NCCL's native API provides a net-to-profiler callback but
no general profiler-to-tuner shared-state interface; \sysname supplies
that interface through typed maps.
```

### 1.5 composability 实验的 takeaway（第 655--658 行）

原文：

```latex
This three-phase response (baseline$\to$contention$\to$recovery)
validates \sysname's composability model: two independently
deployed eBPF programs cooperate through shared typed maps, a
capability absent from NCCL's native plugin architecture.
```

建议改为：

```latex
This three-phase response (baseline$\to$contention$\to$recovery)
validates \sysname's composability model: two independently
deployed eBPF programs cooperate through shared typed maps, providing
a general profiler-to-tuner state path not exposed by NCCL's native APIs.
```

## 2. Review #3A：说明 restricted C 的具体限制

### 修改首次介绍 policy 源语言的位置（第 333--335 行）

原文：

```latex
Figure~\ref{fig:architecture} shows the architecture. Policy authors
write restricted C compiled to BPF ELF objects. At load time, each
program is verified and JIT-compiled to x86-64 code.
```

建议改为：

```latex
Figure~\ref{fig:architecture} shows the architecture. Policy authors
write restricted C compiled to BPF ELF objects: policies cannot use
dynamic allocation or recursion, loops and memory accesses must be
statically bounded, and external functions are limited to whitelisted
eBPF helpers. At load time, each program is verified and JIT-compiled
to x86-64 code.
```

这一句给出 reviewer 所需的可操作限制，而不展开成新的背景段落。第 397 行再次出现 `restricted C` 时无需重复解释。

## 3. Review #3A：解释为何使用 PREVAIL，而不是 Linux verifier

### 扩充 bpftime integration 段落（第 399--406 行）

原文：

```latex
\paragraph{bpftime integration.}
We embed bpftime~\cite{bpftime} as the userspace eBPF runtime.
bpftime provides an LLVM-based JIT compiler, a
PREVAIL-based~\cite{prevail} static verifier, and a map subsystem.
The verifier runs in the same address space as the plugin, adding
approximately 1--5\,ms of one-time startup overhead amortized over
the lifetime of the training job. The LLVM JIT produces optimized x86-64 code, narrowing the gap
to native performance.
```

建议改为：

```latex
\paragraph{bpftime integration.}
We embed bpftime~\cite{bpftime} as the userspace eBPF runtime.
bpftime provides an LLVM-based JIT compiler, a
PREVAIL-based~\cite{prevail} static verifier, and a map subsystem.
We use PREVAIL rather than invoking the Linux kernel verifier because
its in-process API accepts bpftime's userspace map descriptors and
helper whitelist, avoiding a kernel program-loading dependency.
PREVAIL therefore remains part of our trusted computing base
(\S\ref{sec:threat-model}). Verification adds approximately
1--5\,ms of one-time startup overhead amortized over the lifetime of
the training job. The LLVM JIT produces optimized x86-64 code,
narrowing the gap to native performance.
```

该修改同时回答“为什么选 PREVAIL”和对应代价：它适配 userspace map/helper 模型并避免内核加载依赖，但属于系统 TCB；不要声称 PREVAIL 比 Linux verifier 更强。

## 4. Review #3A：在术语首次出现时添加简短解释

### 4.1 LL/LL128、Simple 和 channel（第 133--136 行）

原文：

```latex
performance of NCCL collectives (e.g., AllReduce, AllGather)
significantly depends on runtime decisions: algorithm selection
(ring vs.\ tree), transport protocols (LL vs.\
Simple~\cite{demystifying-nccl}), and parallelization channels.
```

建议改为：

```latex
performance of NCCL collectives (e.g., AllReduce, AllGather)
significantly depends on runtime decisions: algorithm selection
(ring vs.\ tree), transport protocol (the low-latency LL/LL128
variants vs.\ the bandwidth-oriented Simple
protocol~\cite{demystifying-nccl}), and the number of parallel communication
channels.
```

### 4.2 `getCollInfo`（第 222--226 行）

原文：

```latex
For each collective call, the \emph{tuner} plugin's
\texttt{getCollInfo} receives the collective type, message size, and
rank topology, and selects an algorithm (ring, tree), protocol (LL,
LL128, Simple~\cite{demystifying-nccl}), and channel count by
modifying a cost table.
```

建议改为：

```latex
For each collective call, \texttt{getCollInfo}, NCCL's per-collective
tuner callback, receives the collective type, message size, and rank
topology and selects an algorithm (ring, tree), protocol (LL, LL128,
Simple~\cite{demystifying-nccl}), and channel count by modifying a
cost table.
```

### 4.3 NVLS（在第 537 行后、图之前插入；并去掉后文重复定义）

原文（第 537--539 行）：

```latex
\paragraph{NVLink-aware adaptive policy.}

\begin{figure}[t]
```

建议改为：

```latex
\paragraph{NVLink-aware adaptive policy.}
NVLink SHARP (NVLS) is NCCL's hardware-multicast collective algorithm
and the default AllReduce algorithm on our B300 testbed.

\begin{figure}[t]
```

随后将第 586--588 行的重复定义：

```latex
On our 8$\times$~B300 NVLink testbed, NCCL~2.29.7 defaults to the
NVLS algorithm (NVLink SHARP with hardware multicast) for all
message sizes.
```

改为：

```latex
On our 8$\times$~B300 NVLink testbed, NCCL~2.29.7 uses NVLS for all
message sizes.
```

## 5. Review #3B：在 T3 首次解释 function pointer

### 修改 T3 开头（第 291--297 行）

原文：

```latex
\paragraph{T3: Availability vs.\ consistency during updates.}
Hot-reload atomically swaps the function pointer, so any in-progress call
completes under the old policy and the next call uses the new one, ensuring
no call is lost. If the new policy fails verification, the old policy
continues uninterrupted. The tradeoff: different threads may briefly execute
different policies, which is acceptable because collective decisions
are independent across calls.
```

建议改为：

```latex
\paragraph{T3: Availability vs.\ consistency during updates.}
\sysname invokes the active JIT-compiled policy through an atomic
function pointer, a variable that stores the policy's entry address.
Hot-reload replaces this pointer using compare-and-swap: a call that
already loaded the old pointer completes under the old policy, while
later calls load the new one, so no call is lost. If the new policy
fails verification, the old policy continues uninterrupted. The
tradeoff is that different threads may briefly execute different
policies, which is acceptable because collective decisions are
independent across calls.
```

该改法先定义指针存放的内容，再解释原子替换语义；Implementation 第 430--437 行可保持不变。

## 6. Review #3C：明确区分 bpftime 与本文系统的贡献

### 重写贡献列表的引导句和前两项（第 192--210 行）

原文：

```latex
This paper makes three contributions:

\begin{enumerate}[leftmargin=*,topsep=2pt,itemsep=1pt]
\item \textbf{\sysname}, the first framework to bring verified,
composable eBPF policy execution to NCCL's plugin architecture,
transparently enhancing safety and extensibility without modifying
NCCL source code
(\S\ref{sec:design}--\ref{sec:impl}).

\item A structured, typed-map-based composability mechanism
enabling previously isolated NCCL plugins to coordinate and
dynamically adapt, facilitating closed-loop policy control
(\S\ref{sec:design}, \S\ref{sec:eval-composability}).

\item An evaluation on 8$\times$ NVIDIA B300 NVLink GPUs
demonstrating low overhead (80--130\ns per decision), verified
runtime safety (rejecting all unsafe plugins tested), and a
message-size-aware policy that improves AllReduce throughput by
up to 27\% (\S\ref{sec:eval}).
```

建议改为：

```latex
bpftime supplies the generic userspace eBPF verifier, JIT compiler,
and map subsystem; our contributions are the NCCL-specific integration
and policy mechanisms built on these components:

\begin{enumerate}[leftmargin=*,topsep=2pt,itemsep=1pt]
\item An NCCL integration layer that adapts tuner, profiler, and net
callbacks to verified eBPF policy contexts and translates policy
outputs back to stock NCCL, requiring no NCCL source changes
(\S\ref{sec:design}--\ref{sec:impl}).

\item An NCCL-specific composition and update mechanism that routes
profiler telemetry to tuner decisions through bpftime's typed maps
and hot-reloads verified policies via atomic pointer replacement
without losing calls
(\S\ref{sec:design}, \S\ref{sec:eval-composability}).

\item An evaluation on 8$\times$ NVIDIA B300 NVLink GPUs
demonstrating low overhead (80--130\ns per decision), load-time
rejection of all seven tested unsafe programs, and a message-size-aware
policy that improves AllReduce throughput by up to 27\%
(\S\ref{sec:eval}).
```

保留原来的三项结构，但把归属边界写在列表前：verifier/JIT/map primitives 来自 bpftime；本文贡献是 NCCL callback/上下文适配、输出翻译、跨 callback 的 map wiring、热更新生命周期及其评估。同时删除未经本文系统性证明的 `the first`，并把 `verified runtime safety` 改成与实验一致的 `load-time rejection`。

## 建议的应用顺序

先应用第 1 项的 claim 收窄，再应用第 6 项贡献列表，最后补第 2--5 项的局部解释。修改后全文应统一使用以下边界：NCCL 已提供特定的 Net$\rightarrow$Profiler callback，但没有本文所需的通用 Profiler$\rightarrow$Tuner 结构化共享状态接口；bpftime 提供通用 eBPF 基础设施，\sysname 提供 NCCL-specific integration 和 policy lifecycle。
