# Safety Motivation Research for NCCLPol Paper

Research compiled 2026-03-10 for the workshop paper motivation section.

---

## 1. Real-World NCCL Crash/Hang Incidents (Plugin-Related)

### 1.1 Tuner Plugin Causes Segfault (Issue #1974)

**Source:** https://github.com/NVIDIA/nccl/issues/1974 (opened Jan 7, 2026, closed)

**Summary:** A user on RTX 4090 (NCCL 2.29.2+cuda12.4) reports that enabling the `ext-tuner-example` plugin causes an immediate segmentation fault. The issue title: *"4090，use ext-tuner-example and default conf will Segmentation fault (core dumped)"*. Impact reported as "Lower performance than expected" — the user was trying to use the tuner, but the plugin crashes the entire process.

**Relevance:** The official NCCL tuner example plugin, as shipped, causes SIGSEGV on real hardware. This is precisely the failure mode NCCLPol/eBPF verification would prevent — an incorrect plugin crashes the training process rather than being rejected at load time.

**Citation:** NVIDIA/nccl GitHub Issue #1974 (January 2026)

---

### 1.2 Inspector Plugin Deadlock and UAF Crash (Issue #2000)

**Source:** https://github.com/NVIDIA/nccl/issues/2000 (opened Jan 26, 2026)

**Summary:** Title: *"[Issue]: Deadlock/crash due to UAF in inspector plugin"*. A production training integration using the NCCL inspector plugin encountered two critical bugs:

1. **Deadlock**: A GPU process hung (utilization dropped to 0), triggering a watchdog. Root cause: the proxy progress thread was processing a `collInfo` belonging to another communicator group, causing a rwlock deadlock. The inspector plugin has a dead function (`inspectorPluginCollInfoDeRefSafe`) that is never called.

2. **Use-After-Free / Undefined Behavior**: `inspectorPluginCollInfoDeRef()` calls `inspectorLockDestroy()` while the lock is still held — undefined behavior per POSIX. This caused EDEADLK errors and eventually a crash.

**Direct quote:** *"we encountered serious problems while trying to enhance the inspector plugin and integrate it into training."*

**Relevance:** A NCCL plugin shipped in the upstream repository contains UAF and rwlock bugs that crash production training. Neither the NCCL runtime nor any isolation layer catches these before the crash. eBPF verification (termination, memory safety) + sandbox isolation would prevent the UAF from corrupting process state.

**Citation:** NVIDIA/nccl GitHub Issue #2000 (January 2026)

---

### 1.3 Inspector Plugin Segfault in Production Training (Issue #1992)

**Source:** https://github.com/NVIDIA/nccl/issues/1992 (opened Jan 22, 2026)

**Summary:** Title: *"[Question]: Inspector Bug: Do RecordEvents recall after StopEvent? This results in a 'segfault encountered' error"*. A user enabling the NCCL profiler plugin (inspector) on H200 with NCCL 2.28.9+CUDA12.8 experienced application crash during training. Impact: "Application crash". The program runs normally after disabling the plugin.

**Direct quote:** *"The application crashed during training, and the error log points to the plugin. The program runs normally after I disable the plugin."*

**Relevance:** Classic "works without plugin, crashes with plugin" pattern. With native code plugins, there is no isolation — plugin bugs crash the entire training job. This is the core motivation for sandboxed eBPF plugin execution.

**Citation:** NVIDIA/nccl GitHub Issue #1992 (January 2026)

---

### 1.4 Profiler Plugin Wrong Context → Cross-Rank Crash (Issue #1586)

**Source:** https://github.com/NVIDIA/nccl/issues/1586

**Summary:** Title: *"Wrong profilerContext is used in profiler callback (when using PXN)"*. In a 2-node, 4-card-per-node environment, the profiler address of rank 1 is incorrectly passed to rank 0, causing rank 0 to crash when triggering profiler callbacks with the wrong address.

**Direct quote:** *"the `profilerContext` address of one rank might be passed to another rank and cause crash."*

**Relevance:** Demonstrates how incorrect pointer handling in plugins causes cross-rank memory corruption and crashes — the kind of bug that eBPF memory safety checks (no arbitrary pointer dereferences, bounds-checked map access) would prevent.

**Citation:** NVIDIA/nccl GitHub Issue #1586

---

### 1.5 Profiler Plugin Memory Leak (Issue #1569)

**Source:** https://github.com/NVIDIA/nccl/issues/1569

**Summary:** Title: *"Potential group/collective life-time management issue in profiler plugin"*. When NET transport is not used, the group/collective record in the profiler plugin stops after 16 outputs and leaks memory. The lifecycle management for plugin objects is described as "really weird."

**Relevance:** Demonstrates that plugin lifetime and resource management is hard to get right in native C code. eBPF programs, with automatic resource cleanup via BPF maps and bounded execution, eliminate this class of leak.

**Citation:** NVIDIA/nccl GitHub Issue #1569

---

### 1.6 Net Plugin Segfault at Init (Issue #1881)

**Source:** https://github.com/NVIDIA/nccl/issues/1881 (a PR fixing a crash)

**Summary:** Title: *"plugin/net.cc: Fix segfault in ncclGin initialization"*. If `ncclGinIbGdaki.devices()` fails after `init()` succeeds, `pluginLib->ncclGin` is left at -1, leading to a segfault when calling `pluginLib->ncclGin->init()`.

**Relevance:** A null/invalid pointer dereference in the plugin initialization path crashes the training process. This is a classic C memory-safety bug that eBPF's verifier would reject at load time (no uninitialized pointer dereferences allowed).

**Citation:** NVIDIA/nccl GitHub Issue/PR #1881

---

### 1.7 RAS Component Segfault Crashing Training (Issue #1996)

**Source:** https://github.com/NVIDIA/nccl/issues/1996 (opened Jan 23, 2026)

**Summary:** Title: *"[Issue]: Segmentation fault at RAS in NCCL 2.27.7"*. Multiple production training clusters (H100 at ByteDance-like operators) encounter SIGSEGV in NCCL's RAS (Reliability, Availability, Serviceability) subsystem, crashing training jobs. Workaround: `export NCCL_RAS_ENABLE=0`.

**Direct quote:** *"NCCL 2.27.7 triggers segmentation fault errors in RAS related code. This problem can be bypassed using `export NCCL_RAS_ENABLE=0`. NCCL crash training tasks."*

**Relevance:** Even NCCL's own internal subsystems (analogous to plugins) crash training in production. Shows the systemic need for isolated, verified execution paths.

**Citation:** NVIDIA/nccl GitHub Issue #1996 (January 2026)

---

### 1.8 Stack Buffer Overflow in Config Parsing (Issue #2026)

**Source:** https://github.com/NVIDIA/nccl/issues/2026 (opened Feb 26, 2026)

**Summary:** Title: *"[Issue]: Stack Buffer Overflow in `parseStringList`"*. Found via fuzzing: a stack buffer overflow in NCCL's configuration string parsing is reachable via environment variables (`NCCL_COMM_ID`, `NCCL_SOCKET_IFNAME`) or config files. A crafted 128-character string overflows a 64-byte `prefix` buffer with no bounds check, confirmed with ASAN.

**Relevance:** NCCL configuration inputs (environment variables, tuner config files) can trigger memory corruption. This motivates verifiable, sandboxed policy execution over ad-hoc string parsing.

**Citation:** NVIDIA/nccl GitHub Issue #2026 (February 2026)

---

### 1.9 Net Plugin Reload Failure (Issue #1978)

**Source:** https://github.com/NVIDIA/nccl/issues/1978 (opened Jan 14, 2026)

**Summary:** Title: *"[Issue]: Fails re-loading net-plugin after comm destroy"*. Non-default NCCL network plugins specified via `NCCL_NET_PLUGIN` fail to reload after the communicator they were first used in is destroyed. During plugin unload, a `memset` clears the plugin state including the name, preventing subsequent reload.

**Relevance:** Plugin lifecycle management bugs cause silent fallback to the wrong network plugin (the built-in default instead of the custom one), potentially with different performance or correctness properties. This demonstrates the fragility of NCCL's native plugin loading system.

**Citation:** NVIDIA/nccl GitHub Issue #1978 (January 2026)

---

### 1.10 Plugin Loading Bug: Mangled Path (Issue #1732)

**Source:** https://github.com/NVIDIA/nccl/issues/1732

**Summary:** Title: *"[nccl2.27.3] plugin loading bug"*. NCCL 2.27.3 cannot find and load the profiler plugin. The path resolution is buggy — NCCL constructs the path as `libnccl-profiler-./libnccl_example_profiler_plugin.so.so` (double-prepended `libnccl-profiler-` and `.so` suffix), making it impossible to use a local `.so` file as a plugin.

**Relevance:** Plugin discovery and loading in NCCL is fragile. Cloud providers and operators who ship custom tuner plugins must work around underdocumented path resolution rules. Any misspecification silently falls back to the default behavior, potentially with wrong performance.

**Citation:** NVIDIA/nccl GitHub Issue #1732

---

### 1.11 AWS OFI Plugin Incompatibility with GIN (Issue #1913, #1921)

**Source:** https://github.com/NVIDIA/nccl/issues/1913, #1921

**Summary:** NCCL 2.28.9's GIN feature is incompatible with the AWS OFI NCCL network plugin on multi-rail EFA (AWS P5 instances). GIN silently disables itself when the external plugin loads, producing no diagnostic output. When GIN is enabled with OFI, the job fails with "internal error" immediately. Without GIN but with OFI on 2.28.9, there is a 54-minute deadlock.

**Test matrix:**
- NCCL 2.27.7 + AWS OFI: ✅ 12.01 GB/s
- NCCL 2.28.9 + GIN + TCP: ✅ 1.25 GB/s (TCP-limited)
- NCCL 2.28.9 + GIN + AWS OFI: ❌ immediate crash
- NCCL 2.28.9 + OFI (no GIN): ❌ 54-minute deadlock

**Relevance:** Plugin/feature interaction bugs cause silent degradation (GIN disabled) or hard crashes (deadlock, internal error). Verified policies with explicit capability declarations would prevent incompatible plugin combinations at load time.

**Citation:** NVIDIA/nccl GitHub Issues #1913, #1921 (November–December 2025)

---

## 2. Training Job Failure Costs

### 2.1 Llama 3 Pre-training: 466 Interruptions in 54 Days (Meta, 2024)

**Source:** "The Llama 3 Herd of Models," Meta AI, arXiv:2407.21783 (2024). Section 3.3.4 "Reliability and Operational Challenges."

**Key statistics:**
- **Scale:** 16,000 GPUs for Llama 3 405B pre-training
- **466 job interruptions over 54 days** (8.6 interruptions per day on average)
- Of 466 interruptions, 419 were unexpected (unplanned)
- **~78% of unexpected interruptions attributed to confirmed or suspected hardware issues** (GPU or host component failures, silent data corruption)
- **GPU issues are the largest single category, accounting for 58.7% of all unexpected interruptions** (GPU kernel errors: 30.1%, GPU machine checks: 17.2%, GPU memory: 12.9%)
- Network failures: 8.4% of unexpected interruptions
- Despite failures: **achieved >90% effective training time** (defined as useful training iterations / total elapsed time)
- At least one training interruption daily due to automated maintenance alone
- Recovery time: system catches up to prior progress **within 15 minutes from latest checkpoint**
- Average time for failure detection + diagnostic test execution: **<10 minutes**

**Direct quote:** *"The complexity and potential failure scenarios of 16K GPU training surpass those of much larger CPU clusters that we have operated. Moreover, the synchronous nature of training makes it less fault-tolerant—a single GPU failure may require a restart of the entire job."*

**Direct quote:** *"During a 54-day snapshot period of pre-training, we experienced a total of 466 job interruptions."*

**Relevance for NCCLPol:** Communication misconfiguration or plugin bugs contribute to the "software/configuration" category of failures (not hardware). NCCL's own flight recorder is explicitly mentioned as critical for diagnosing hangs/performance issues — NCCLPol's eBPF telemetry loop is complementary. The 90% effective training time target leaves 10% wasted, and reducing software-induced failures preserves more of that budget.

**Citation:** Meta AI (Dubey et al.), "The Llama 3 Herd of Models," arXiv:2407.21783, July 2024.

---

### 2.2 MegaScale: 100+ Restarts, 10% Time Lost to Failures (ByteDance, NSDI 2024)

**Source:** "MegaScale: Scaling Large Language Model Training to More Than 10,000 GPUs," NSDI 2024. (arXiv:2402.15627)

**Key statistics:**
- **Scale:** >10,000 GPUs for weeks of training, 175B parameter model
- **Over 100 training restarts** in a single production run over several weeks
- **Over 90% of software and hardware faults automatically identified and fixed** by their robust training framework
- **<10% effective training time rate lost to failures** (since they maintain >90% effective training time)
- Average time for failure detection + diagnostic tests: **<10 minutes**
- **Recovery from crash within 15 minutes** from latest checkpoint
- Failures include: CUDA errors, segmentation faults, network interface flapping, computational stragglers

**Direct quote (Figure 11 caption):** *"MegaScale repairs and recovers the training process for over 100 times in presence of failures."*

**Direct quote:** *"Over the several weeks of this run, we experience training restarts over 100 times. With the robust training framework, over 90% of software and hardware faults are automatically identified and fixed."*

**Direct quote:** *"the system can catch up to the training progress prior to the crash within 15 minutes from the latest checkpoints, maintaining over 90% effective training time rate."*

**Relevance:** The paper explicitly notes that NCCL timeout misconfiguration causes training stalls during network interface flapping: *"the default value will make NCCL timeout very quickly and return a completion error before the network card up again."* This is exactly the kind of misconfiguration NCCLPol's verified policies would address.

**Citation:** Jiang et al., "MegaScale: Scaling Large Language Model Training to More Than 10,000 GPUs," NSDI 2024, arXiv:2402.15627.

---

### 2.3 Microsoft Philly GPU Cluster: 30% of Jobs Fail (SoCC 2019)

**Source:** "Analysis of Large-Scale Multi-Tenant GPU Clusters for DNN Training Workloads," SoCC 2019. arXiv:1901.05758. (Microsoft's "Philly" cluster, 2-month trace, ~100,000 jobs, hundreds of users.)

**Key statistics:**
- **~30% of jobs are killed or finish unsuccessfully due to failures** (Table 6: 44.53% passed, 37.69% killed, 17.76% unsuccessful)
- Failed/killed jobs **consume 55% of total GPU time** in the trace (killed + unsuccessful GPU time)
- **Largest failure category: programming errors** (including configuration errors, invalid memory access, MPI runtime failures)
- "Invalid memory access" explicitly listed: *"Training job dies because of violating access on memory address space, e.g., using an invalid pointer value, or having race condition while copying data. This failure is observed in both CPU memory and memory allocated for GPU access."*
- Top-8 failure reasons cover 88.9% of all failures
- Single job causes 2.3 failures on average (repetition factor); single user causes 38.8 failures on average

**Direct quote:** *"Around 30% of jobs are killed or finish unsuccessfully due to failures. Failures are caused by errors across the stack, with programming errors dominating failures and occurring early in the training process."*

**Direct quote:** *"many failures ought to be caught early, well before they are scheduled on a larger shared cluster."*

**Relevance for NCCLPol:** This is the canonical citation for training job failure rates. 30% failure rate, with programming/configuration errors as the dominant cause. NCCLPol's verifier catches configuration errors (wrong cost table access, invalid nChannels) before they manifest as runtime crashes.

**Citation:** Jeon et al., "Analysis of Large-Scale Multi-Tenant GPU Clusters for DNN Training Workloads," SoCC 2019, arXiv:1901.05758.

---

## 3. Plugin Ecosystem Growth

### 3.1 Tuner API Evolution: v2 → v3 → v4 → v5

**Source:** NCCL GitHub repository. Files: `src/include/plugin/tuner/tuner_v2.h`, `tuner_v3.h`, `tuner_v4.h`, `tuner_v5.h`. Commits show tuner support first added in NCCL 2.19.1-1 (September 2023).

**API evolution timeline:**
- **v2 (NCCL 2.19, Sept 2023):** First tuner plugin support. `getCollInfo` takes `(context, collType, nBytes, collNetSupport, nvlsSupport, numPipeOps, *algorithm, *protocol, *nChannels)`. Output fields are separate int pointers.
- **v3 (NCCL 2.21):** Added output fields, changed function signatures.
- **v4:** Added `regBuff` parameter.
- **v5 (current, NCCL 2.26+):** `getCollInfo` takes `float** collCostTable` (a 2D cost table), `numAlgo`, `numProto` dimensions. Plugin modifies cost table entries in-place. Also introduces `ncclTunerConstants_v5_t` — a large struct with bandwidth/latency parameters that the plugin can modify during `init()`. Copyright includes **Meta Platforms** as co-author.

**v5 `getCollInfo` signature:**
```c
ncclResult_t (*getCollInfo)(void* context, ncclFunc_t collType, size_t nBytes,
                            int numPipeOps, float** collCostTable, int numAlgo, int numProto,
                            int regBuff, int* nChannels);
```

**v2 `getCollInfo` signature (for comparison):**
```c
ncclResult_t (*getCollInfo)(void* context, ncclFunc_t collType, size_t nBytes,
                            int collNetSupport, int nvlsSupport, int numPipeOps,
                            int* algorithm, int* protocol, int* nChannels);
```

**Significance:** The API has grown substantially more complex, from simple integer outputs to in-place modification of a 2D float cost table. The `float**` vs `float(*)[NCCL_NUM_PROTOCOLS]` ABI is a known crash source (our own experience: this was the root cause of SIGSEGV in v1–v4 of our plugin). Co-authorship by Meta signals that major cloud/AI companies are now writing custom tuners, driving API evolution.

**Plugin prevalence:**
- **AWS OFI NCCL plugin** (aws-ofi-nccl): 207 GitHub stars, 91 forks, 56 releases, v1.18.0 as of January 2026. Enables AWS EFA-based training for all AWS GPU instances (P3, P4, P5). Used by every serious multi-node training job on AWS.
- **NCCL Tuner plugins** shipped by cloud providers (AWS, Azure, GCP) for their specific interconnect topologies.
- **Azure NDv5 series** documentation recommends custom NCCL configurations and plugins for their InfiniBand fabric.
- Meta co-authored the tuner API header itself.

**Relevant quotes from issue #1661:** Users trying to use ext-tuner to enforce specific algo+protocol combinations discovered that invalid combos silently fail (chosen by the tuner but not used by NCCL runtime), and that env variables override tuner settings in undocumented ways. This fragility motivates a verified policy execution model.

**Citation:** NCCL GitHub repository; AWS OFI NCCL: https://github.com/aws/aws-ofi-nccl

---

### 3.2 Plugin Incompatibility Creates New Version-Specific Failure Modes

**NCCL Issue #1292 (Symbol Version Mismatch):** When NCCL version and plugin ABI version mismatch, NCCL silently falls back to the internal implementation with no error beyond an INFO log:
```
NCCL INFO NET/Plugin: Failed to find ncclNetPlugin_v8 symbol.
NCCL INFO NET/Plugin: Failed to find ncclNetPlugin symbol (>= v5). ncclNetPlugin symbols v4 and lower are not supported.
```
The job continues but uses the (potentially slower or incorrect) built-in implementation, not the custom plugin the operator configured.

**Relevance:** Version mismatch is silent. NCCLPol's eBPF approach provides explicit capability declarations and version-independent policy expression.

---

## 4. eBPF Safety Precedent

### 4.1 eBPF Verifier: What It Guarantees

**Source:** Linux kernel eBPF verifier documentation (https://www.kernel.org/doc/html/latest/bpf/verifier.html); LWN.net; BPFd article (LWN #744522).

The eBPF verifier statically checks all programs before execution. Key safety guarantees:

1. **No unbounded loops / guaranteed termination:** The verifier requires all CFGs to be DAGs (no backward edges). Programs are proven to terminate.
2. **No uninitialized register reads:** Every register must be written before it is read. The verifier tracks register state across all paths.
3. **No out-of-bounds memory access:** All stack accesses and map reads/writes are bounds-checked. Stack writes must precede reads.
4. **Type safety:** Pointer types are tracked. Invalid pointer arithmetic converts registers to SCALAR_VALUE, preventing subsequent use as memory addresses.
5. **Resource leak prevention:** Reference-counted resources (e.g., socket pointers) must be released before program exit.
6. **No dead code / unreachable instructions:** The verifier rejects programs with unreachable paths.

**For NCCLPol context:** An eBPF tuner policy:
- Cannot dereference an invalid `collCostTable` pointer (the ABI bug that caused our SIGSEGV)
- Cannot write outside the cost table bounds (preventing buffer overflow)
- Cannot loop indefinitely (guaranteed O(1) policy decisions per collective)
- Cannot leak memory (no malloc/free, map lifetime managed by kernel)

**From LWN #744522 on BPFd:** *"eBPF programs are verified before they're run and are guaranteed to be safe when loaded into the kernel, unlike kernel modules."* (In contrast: kernel modules "can crash the kernel" and are "quite unsafe.")

**Citation:** Linux kernel documentation; LWN.net Articles 744522, 853489.

---

### 4.2 bpftime: Userspace eBPF with MPK Isolation (OSDI 2025)

**Source:** "bpftime: Userspace eBPF Runtime" (OSDI 2025). arXiv:2311.07923.

**Key claims:**
- Userspace eBPF via bpftime achieves **10x speedup for uprobes** vs. kernel-based uprobes (eliminates dual context switches)
- Provides userspace isolation without requiring root access or kernel privileges
- Uses **Intel MPK (Memory Protection Keys)** for intra-process isolation: the eBPF policy program runs in a protected domain, preventing it from reading/writing the host process's memory arbitrarily
- LLVM JIT compilation of eBPF bytecode enables near-native performance
- The eBPF verifier runs before JIT, providing static safety guarantees

**Relevance for NCCLPol:** bpftime is the execution substrate for our eBPF policies. The combination of eBPF verification (safety) + MPK isolation (containment) + LLVM JIT (performance) provides the three properties needed for a trustworthy NCCL plugin sandbox.

**Citation:** Jia et al., "bpftime: Userspace eBPF Runtime for Uprobe, Syscall and Kernel-User Interactions," OSDI 2025.

---

### 4.3 eBPF as Precedent in Systems

**Network:** Cilium (eBPF-based Kubernetes networking) replaces iptables kernel modules with verified eBPF programs. Claims: no kernel panics from networking code, hot-reload without service interruption, verifiable per-policy behavior. Used in production at Google, Meta, and most major cloud providers.

**Observability:** BPF/eBPF tracing tools (bpftrace, BCC) are run in production 24x7 precisely because the verifier guarantees no kernel destabilization — something impossible with kernel modules.

**Storage/XDP:** XDP (eXpress Data Path) uses eBPF to safely run packet processing in the kernel fast path with verification guarantees. The XDP verifier has prevented numerous bugs that would have caused kernel panics if implemented as native kernel code.

---

## 5. Silent Performance Degradation Cases

### 5.1 Protocol Collapse: LL at Large Messages (Our Own Work)

**Source:** Our experimental data (project memory; docs/tmp/ sweep logs).

NCCL's Low-Latency (LL) protocol is optimal for small messages but causes **39.5x throughput degradation at 16MB** compared to NCCL_PROTO=SIMPLE. This degradation is invisible at 4KB (<0.2% difference). If a tuner plugin incorrectly selects LL for large messages (e.g., because of a cost table bug), training silently slows down with no error message.

**Relevance:** The strongest quantitative result in our paper. Demonstrates that silent misconfiguration — exactly what unverified native plugins risk causing — can be catastrophic.

---

### 5.2 GDR Detection Bug: 10x Throughput Collapse (Issue #1676)

**Source:** https://github.com/NVIDIA/nccl/issues/1676

**Summary:** After upgrading to DGX OS 25.01, users experienced NCCL all_reduce dropping from **~180 GB/s to ~10 GB/s** for 1GB messages on 2 nodes — approximately a **10x slowdown**. Root cause: a module naming change (`nv_peer_mem` → `nvidia-peermem`) caused NCCL ≤2.22.3 to silently disable GPU Direct RDMA, falling back to slower communication.

**Direct quote from issue:** *"NCCL all_reduce on 2 nodes dropped from ~180GB/s to ~10GB/s for 1GB message size."*

**Relevance:** A silent configuration failure (wrong module path → GDR disabled) causes an order-of-magnitude performance regression with no error. Policy-based governance with observable telemetry would detect and alert on this immediately.

**Citation:** NVIDIA/nccl GitHub Issue #1676

---

### 5.3 Wrong Transport Selection: 2x+ Slowdown (Issue #1829)

**Source:** https://github.com/NVIDIA/nccl/issues/1829 (opened Dec 2025)

**Summary:** Title: *"Performance degradation due to incorrect (?) transports being used with NCCL 2.23+"*. A user reports that upgrading from NCCL <2.23 to ≥2.23 caused the same training to be **"over twice as slow"**. The difference: NCCL 2.23+ uses different transports for the same hardware topology. NCCL <2.23 used P2P/CUMEM; NCCL 2.23+ selects a different transport. No error is reported.

**Relevance:** NCCL's default transport selection algorithm can silently choose suboptimal transports after a version upgrade. This is a prime example of where verified tuner policies would enforce correct transport selection independent of NCCL version changes.

**Citation:** NVIDIA/nccl GitHub Issue #1829

---

### 5.4 Tree Graph Search Failure: nChannels=1, 1 NIC Used (Issue #1946)

**Source:** https://github.com/NVIDIA/nccl/issues/1946 (Dec 2025)

**Summary:** Title: *"NCCL2.25+ tree-graph searching may fail (B-tree interType=SYS, simple tree are not enabled for sm80) and resulting nChannels=1 (only 1 nic is used globally)"*. On 8×A800 + 8×RDMA NICs, NCCL 2.25+ incorrectly selects only 1 channel and 1 NIC due to a chain of topology search failures. NCCL 2.21 works correctly with 8 NICs.

**Relevance:** NCCL's topology auto-discovery can silently under-utilize the hardware (8 NICs reduced to 1). The result is severe throughput degradation with no error. Policy-driven channel and NIC selection would prevent this regression.

**Citation:** NVIDIA/nccl GitHub Issue #1946

---

### 5.5 NCCL Version Upgrade Causes 45% More Memory Instructions (Issue #1997)

**Source:** https://github.com/NVIDIA/nccl/issues/1997 (opened Jan 2026)

**Summary:** Upgrading from NCCL 2.22 to 2.24/2.29 causes a **45% increase in local memory instructions** (178 stl + 420 ldl vs. 138 stl + 273 ldl), silently degrading all_reduce performance on H200.

**Relevance:** Silent performance regression from version upgrade, requiring expert kernel analysis to diagnose. Observable telemetry from the eBPF profiler plugin would surface this regression immediately.

**Citation:** NVIDIA/nccl GitHub Issue #1997

---

### 5.6 NCCL Fails Silently on Missing Network Interface (Issue #1837)

**Source:** https://github.com/NVIDIA/nccl/issues/1837

**Summary:** Title: *"[Issue]: NCCL fails silently when the interface set by NCCL_SOCKET_IFNAME is not found"*. When `NCCL_SOCKET_IFNAME` is set to a non-existent interface, NCCL proceeds without warning, eventually failing with a cryptic `ncclInternalError` about address family `59936` instead of the expected AF_INET/AF_INET6.

**Relevance:** Misconfiguration is silent until a cryptic error appears. Verified policies would validate configuration parameters at load time and reject invalid configurations immediately.

**Citation:** NVIDIA/nccl GitHub Issue #1837

---

## Summary for Paper Use

| Category | Evidence | Source |
|----------|----------|--------|
| Plugin crash (SIGSEGV) | Tuner example segfaults on 4090 | NCCL #1974 |
| Plugin crash (UAF/deadlock) | Inspector plugin UAF causes training hang | NCCL #2000 |
| Plugin crash (wrong context) | Profiler passes rank 1's context to rank 0 → crash | NCCL #1586 |
| Plugin crash (null deref) | GIN plugin null deref at init | NCCL #1881 |
| Plugin crash (version mismatch) | Tuner v5 `float**` ABI bug causes SIGSEGV | Our work (plugin.cpp) |
| Training failure rate | 30% of DNN training jobs fail (Microsoft cluster) | Jeon et al., SoCC 2019 |
| Training failure rate | 466 interruptions in 54 days (Llama 3 at 16K GPUs) | Meta, arXiv:2407.21783 |
| Training failure cost | 100+ restarts in weeks of training | ByteDance MegaScale, NSDI 2024 |
| Silent degradation | 39.5x slowdown from wrong protocol (LL at 16MB) | Our work |
| Silent degradation | 10x throughput collapse from GDR misconfiguration | NCCL #1676 |
| Silent degradation | 2x+ slowdown from wrong transport selection (NCCL 2.23) | NCCL #1829 |
| Plugin ecosystem | Tuner API v2→v5 in 2 years; Meta co-authored header | NCCL tuner_v2.h–v5.h |
| Plugin ecosystem | AWS OFI plugin: 207★, 91 forks, 56 releases, production use | github.com/aws/aws-ofi-nccl |
| Plugin incompatibility | NCCL+GIN+AWS OFI: crash and 54-minute deadlock | NCCL #1913, #1921 |
| eBPF safety precedent | Verifier guarantees: termination, mem safety, type safety | Linux kernel docs |
| eBPF safety precedent | bpftime: MPK isolation + LLVM JIT + verification | OSDI 2025 |

---

## Key Citable Quotes for Paper

**On training job failure rates:**
> "Around 30% of jobs are killed or finish unsuccessfully due to failures. Failures are caused by errors across the stack, with programming errors dominating." — Jeon et al., SoCC 2019 (Microsoft Philly cluster, 100,000 jobs)

> "During a 54-day snapshot period of pre-training, we experienced a total of 466 job interruptions." — Meta Llama 3 (arXiv:2407.21783), 16,000 GPU cluster

**On plugin danger:**
> "The application crashed during training, and the error log points to the plugin. The program runs normally after I disable the plugin." — NCCL #1992 (inspector plugin segfault)

> "we encountered serious problems while trying to enhance the inspector plugin and integrate it into training." — NCCL #2000 (UAF/deadlock in inspector)

**On silent misconfiguration:**
> "NCCL all_reduce on 2 nodes dropped from ~180GB/s to ~10GB/s for 1GB message size" — NCCL #1676 (GDR detection bug, 10x silent degradation)

**On eBPF safety:**
> "eBPF programs are verified before they're run and are guaranteed to be safe when loaded into the kernel, unlike kernel modules." — LWN.net (BPFd article)
