# Paper Draft: Abstract & Framing

## Title Options

1. **Governable Collectives: Safe and Verifiable Policy Execution for GPU Communication Runtimes**
2. **NCCL-Policy: An eBPF-Based Policy Plane for GPU Collective Communication**
3. **Extending GPU Collective Runtimes with Verifiable Policies**

## Unified Direction (One Sentence)

We build a **policy execution runtime** that turns NCCL's plugin interfaces into a programmable governance plane — where platform operators deploy verified, isolated, composable eBPF programs to enforce communication SLOs, automate fault recovery, and manage resource interference across multi-tenant GPU clusters, all without modifying NCCL or training frameworks.

## Abstract (Draft v1)

GPU collective communication libraries like NCCL are the backbone of distributed deep learning, yet they remain opaque execution engines with no mechanism for runtime governance. When multiple training jobs share GPU nodes, NCCL offers no communication SLOs, no multi-tenant fairness, and no automated fault recovery — operators can only set static environment variables and hope for the best. Meanwhile, NCCL's recent plugin system (tuner, network, environment, and profiler plugins) has quietly exposed a rich set of per-collective decision points and runtime actions (communicator revocation, elastic membership, zero-SM communication paths), creating an unprecedented opportunity for programmable control.

We present **NCCLPol**, a policy execution framework that bridges this gap. NCCLPol is deployed as a single NCCL plugin shared library that internally runs bpftime, a userspace eBPF runtime with LLVM JIT compilation and hardware-enforced memory isolation (Intel MPK). Platform operators write policies as standard eBPF programs — verified at load time for bounded execution and memory safety, JIT-compiled to native code, and hot-reloadable without restarting training jobs. Each policy attaches to NCCL's decision points and can: (1) override collective algorithm, protocol, and channel count selections to enforce per-job communication SLOs and fairness; (2) trigger communicator revocation, shrinking, and growth for automated fault isolation and elastic recovery; (3) dynamically adjust the SM-vs-CopyEngine communication balance to minimize compute interference; and (4) control network device selection and connection scheduling across shared NICs. A closed-loop telemetry path feeds NCCL profiler events back into eBPF maps, enabling policies to adapt to real-time conditions.

NCCLPol's key contribution is demonstrating that governance policies can execute in the collective communication hot path with negligible overhead: eBPF verification eliminates runtime safety checks, LLVM JIT achieves near-native execution speed, and the per-collective policy invocation adds < 200 ns to operations that typically take 10–1000 μs. We evaluate NCCLPol on multi-tenant GPU clusters running concurrent large-language-model training workloads. Compared to default NCCL, NCCLPol's SLO enforcement policy reduces P99 AllReduce latency variation by Xх under contention, its fault recovery policy restores communication within Y ms without job restart, and its interference control policy improves end-to-end training throughput by Z% when communication and computation compete for SM resources — all while maintaining the safety and isolation guarantees that production GPU clusters demand.

## Why This Is One Direction, Not Six

| Apparent "direction" | Role in the unified system |
|---|---|
| QoS / Fairness | **Policy instance #1**: uses tuner hook to adjust nChannels + net hook for rate shaping |
| Fault recovery | **Policy instance #2**: uses profiler telemetry → triggers revoke/shrink/grow actions |
| SM interference | **Policy instance #3**: uses tuner hook to balance SM budget + switch to zero-SM paths |
| Network device policy | **Policy instance #4**: uses net hook for NIC selection / vNIC management |
| Safe extensibility | **The system's core mechanism**: bpftime/EIM verification + MPK isolation |
| Cross-vendor portability | **Evaluation bonus**: same policy IR runs on NCCL + RCCL |

The paper has **one system** (NCCLPol), **one mechanism** (verified eBPF in NCCL plugin hooks), and **multiple policy case studies** that demonstrate its expressiveness.

## Paper Structure Sketch

1. **Introduction**: Multi-tenant GPU clusters need governance; NCCL has no policy mechanism; plugins + eBPF = opportunity
2. **Background & Motivation**: NCCL plugin system, bpftime/EIM, real-world pain points (measurements from production clusters)
3. **Design**: Policy hook model, eBPF ABI for NCCL, action space, telemetry loop, deployment model (one .so)
4. **Implementation**: bpftime integration, plugin adapter layers, policy composition, hot reload
5. **Policy Case Studies**: SLO enforcement, fault recovery, interference control (each 1-2 pages)
6. **Evaluation**: Multi-tenant training workloads, fault injection, microbenchmark overhead
7. **Discussion**: Limitations (MPK domain limit, eBPF expressiveness), RCCL portability, future (device-side policies)
8. **Related Work**: NCCL tuners, eBPF for networking/storage/observability, safe extensibility (Wasm, Lua), elastic training
9. **Conclusion**
