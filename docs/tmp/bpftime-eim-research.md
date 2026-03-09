# bpftime and EIM Research Summary for NCCL Policy Plane

## 1. bpftime: Userspace eBPF Runtime

### Overview

bpftime is a high-performance userspace eBPF runtime and general extension framework published at OSDI 2025 ("Extending Applications Safely and Efficiently" by Zheng et al.). It enables running eBPF programs entirely in userspace, bypassing the kernel for significantly lower overhead on uprobes, syscall tracing, and application-level hooks.

- **GitHub**: https://github.com/eunomia-bpf/bpftime
- **Paper**: https://www.usenix.org/conference/osdi25/presentation/zheng-yusheng
- **arXiv (earlier version)**: https://arxiv.org/abs/2311.07923

### Architecture

bpftime comprises several modular components:

1. **VM (Virtual Machine)**: The eBPF bytecode execution engine. Users can choose between:
   - **llvmbpf** (LLVM JIT/AOT compiler) -- highest performance
   - **ubpf** (lightweight JIT and interpreter)

2. **Runtime**: Userspace implementation of eBPF maps, helper functions, ufuncs, and runtime safety features. Provides the full eBPF execution environment outside the kernel.

3. **Attach Events**: Supports attaching eBPF programs to:
   - Uprobes (function entry/exit in userspace)
   - Syscall tracepoints
   - XDP (via AF_XDP or DPDK)
   - GPU kernels (CUDA via PTX injection)
   - Custom extension entries (via EIM)

4. **Verifier**: Supports PREVAIL (userspace abstract-interpretation-based verifier) or the Linux kernel verifier for stronger guarantees.

5. **Loader**: Two modes:
   - **LD_PRELOAD** library: Works with existing eBPF toolchains (clang, libbpf) without kernel involvement
   - **Daemon mode**: Cooperates with kernel eBPF when available

6. **Shared Memory**: eBPF program bytecode and maps reside in shared memory, enabling communication between the loader process and the target process, as well as inter-process map sharing for control plane communication.

### Core Capabilities

| Capability | Details |
|---|---|
| **eBPF Verification** | PREVAIL verifier (abstract interpretation) for memory bounds, type safety, helper access control. Can also use Linux kernel verifier. |
| **JIT Compilation** | LLVM-based JIT/AOT via llvmbpf; ubpf JIT as alternative. AOT can compile eBPF to native ELF objects. |
| **Hardware Isolation** | Intel MPK (Memory Protection Keys) via ERIM-style intra-process isolation. WRPKRU instructions bracket extension execution. |
| **Dynamic Binary Rewriting** | Inline hooking via Frida-gum for function interposition; zpoline for syscall hooking. |
| **Map Types** | Hash maps, arrays, ring buffers, and more -- implemented in shared userspace memory. |
| **Compatibility** | Works with standard clang/libbpf toolchain. Supports CO-RE (Compile Once, Run Everywhere) via BTF. |
| **GPU Support** | Compiles eBPF to PTX (NVIDIA) or SPIR-V (cross-vendor) and injects into GPU kernels. |

### Performance Characteristics

**Uprobe/Hook Latency:**
- Userspace uprobe: ~100 ns (bpftime) vs ~1000 ns (kernel uprobe) = **10x improvement**
- Userspace memory access: ~4 ns vs ~40 ns in kernel = **10x improvement**
- No dual context switches (user->kernel->user) needed

**Application-Level Benchmarks (from OSDI 2025 paper):**
- **Nginx firewall (eBPF extension)**: 2% throughput loss vs 11-12% for Lua/WebAssembly = **5-6x better than Wasm**
- **Nginx sslsniff monitoring**: 7% overhead (bpftime) vs 28% overhead (kernel eBPF) = **4x better than kernel**
- **Redis durability tuning**: 1.5x more throughput with bpftime extensions
- **Overall**: Up to 6x better performance compared to WebAssembly across six real-world applications

**Micro-benchmarks (LLVM JIT):**
- LLVM JIT/AOT consistently matches near-native performance
- Excels in integer computation (log2), complex math (prime), memory operations (memcpy, strcmp)
- Outperforms ubpf, Wasm runtimes across all tested scenarios

**Hook Overhead Breakdown:**
- Verification overhead: **zero at runtime** (all checks done at load time)
- MPK isolation (WRPKRU): ~11-260 cycles per domain switch on Intel CPUs
- Binary rewriting trampoline: minimal -- replaces instructions at hook point with call to preamble

---

## 2. EIM: Extension Interface Model

### Overview

EIM is a specification framework for defining fine-grained interactions between host applications and extensions (plugins). It is inspired by eBPF's load-time verification and WebAssembly's component model, and is designed to enforce the **principle of least privilege** for application extensions.

- **Specification**: https://eunomia.dev/bpftime/EIM/spec/

### Core Concepts

**Three Capability Types:**

1. **State Capabilities**: Permission to read or write specific variables in the host application
   - Example: `readPid` -- allows reading the process ID variable
   - Can be `read-only` (const) or `read-write`
   - Enforced by modifying function prototypes during verification

2. **Function Capabilities**: Permission to call specific functions in the host application
   - Example: `nginxTime` -- allows calling nginx's time function
   - Controls which helper functions an extension can invoke

3. **Extension Entries**: Specific hook points where extensions can attach
   - Define where in the host the extension's logic runs
   - Can be at function boundaries or arbitrary code locations
   - Support for function interposition (replace/wrap behavior)

**Extension Classes:**
- Group capabilities into roles with different access levels
- Example: For a `processBegin` entry, one class for read-only logging, another for read-write firewall actions
- An administrator defines classes per entry based on required access

**Two-Phase Specification:**

1. **Development-Time Spec**: Produced by the application developer
   - Declares available extension entries, state variables, helper functions
   - Defines possible capability groupings

2. **Deployment-Time Spec**: Created by the system administrator
   - Decides which extensions get which capabilities
   - Allocates resources (CPU instruction limits, memory) to each extension

### How Verification Works

The bpftime verifier enforces EIM specifications by:
1. Checking that each extension's function signature matches its declared extension class
2. Verifying that the extension only calls functions allowed by its class's function capabilities
3. Converting EIM capability constraints into eBPF verifier constraints (e.g., marking arguments as `const` for read-only state capabilities)
4. Enforcing resource limits (instruction count, memory usage) at verification time
5. **Result: Zero runtime overhead** for safety checks -- all enforcement happens before execution

### Isolation Mechanisms

**Intel MPK (Memory Protection Keys):**
- Each extension's memory is tagged with a unique protection key (4-bit domain ID, up to 16 domains)
- WRPKRU instructions are inserted:
  - Before extension execution: enable the extension's memory key
  - After extension returns: reset keys to deny extension access
- Cost: 11-260 cycles per WRPKRU instruction on current Intel CPUs
- Result: Strong intra-process memory isolation with hardware enforcement

**eBPF Verification (Software Safety):**
- Memory bounds checking via abstract interpretation
- Type safety enforcement
- No pointer leaks to externally-visible memory
- Helper function access control per extension class
- Instruction count limits prevent infinite loops

**Combined Model:**
- Verification eliminates most runtime checks (software safety net)
- MPK provides hardware-enforced isolation as defense-in-depth
- Together: fine-grained safety + strong isolation + near-native efficiency

---

## 3. llvmbpf: LLVM-Based BPF JIT/AOT Compiler

### Overview

llvmbpf is a standalone, high-performance eBPF VM library based on LLVM, part of the bpftime project but usable independently.

- **GitHub**: https://github.com/eunomia-bpf/llvmbpf

### Capabilities

| Feature | Details |
|---|---|
| **JIT Compilation** | Compiles eBPF bytecode to native code via LLVM at runtime |
| **AOT Compilation** | Pre-compiles eBPF bytecode to native ELF object files that link like C-compiled objects |
| **LLVM IR Generation** | Can emit LLVM IR for inspection, optimization with `opt -O3` |
| **Multi-Architecture** | Supports multiple CPU architectures via LLVM backend |
| **GPU Targets** | PTX generation for NVIDIA CUDA GPUs, SPIR-V for cross-vendor GPUs |
| **Minimal Dependencies** | No maps, helpers, verifiers, or loaders -- pure VM/compiler library |
| **Map Integration** | Helper function callbacks for map access (bpf_map_lookup_elem, etc.) |

### Usage Model

```cpp
llvmbpf_vm vm;
vm.load_code(code, code_len);
vm.register_external_function(2, "print", (void *)ffi_print_func);
auto func = vm.compile();  // JIT compile
vm.exec(&bpf_mem, sizeof(bpf_mem), res);  // Execute
```

### Performance

- Consistently matches or approaches native C performance in micro-benchmarks
- AOT mode eliminates JIT compilation overhead at runtime
- LLVM optimization passes (O3) produce highly optimized native code
- Can inline map access and helper functions for additional performance

---

## 4. Relevance to NCCL Policy Plane

### How bpftime/EIM Could Run Policy Programs in NCCL's Address Space

**Architecture for NCCL Integration:**

1. **In-Process Extension Model**: bpftime runs extensions in the same address space as the host application. For NCCL, this means policy programs execute inside the NCCL process alongside GPU communication operations -- no IPC, no context switches for policy evaluation.

2. **Hook Points (Extension Entries)**: NCCL API functions (ncclAllReduce, ncclSend, ncclRecv, etc.) can be declared as extension entries in an EIM specification. Policy eBPF programs attach at these points to:
   - Inspect communication parameters (ranks, data sizes, communicator state)
   - Make allow/deny/modify decisions
   - Collect telemetry and statistics

3. **Binary Rewriting Injection**: bpftime can be injected into a running NCCL process via:
   - `LD_PRELOAD` at startup
   - `ptrace`-based injection into already-running processes
   - No recompilation of NCCL required

4. **Shared Memory Maps**: Policy state (counters, rate limits, topology info, ACLs) can be stored in shared-memory eBPF maps, enabling:
   - A control plane process to update policies without restarting NCCL
   - Multiple NCCL processes to share aggregated state
   - Hot-reload of policy programs

5. **GPU Kernel Hooks**: bpftime can also attach to CUDA kernels via PTX injection, enabling monitoring/policy at the GPU operation level (e.g., tracking actual data movement patterns).

### Advantages for Our Use Case

| Advantage | Explanation |
|---|---|
| **Ultra-Low Overhead** | ~100 ns hook latency, near-native JIT execution. NCCL's latency-sensitive collective operations would see minimal impact. |
| **Safety Guarantees** | eBPF verification ensures policy programs cannot crash NCCL, corrupt memory, or loop forever. Critical for production GPU clusters. |
| **Hardware Isolation** | MPK isolates policy memory from NCCL internals -- a buggy policy cannot corrupt NCCL state even if verification has gaps. |
| **Fine-Grained Access Control** | EIM capabilities control exactly what host state/functions policies can access. Different policy types get different privilege levels. |
| **Hot Reload** | Policy programs can be unloaded/reloaded at runtime without restarting NCCL jobs -- essential for long-running training workloads. |
| **Standard Toolchain** | Policies written in C, compiled with clang, using libbpf. Existing eBPF ecosystem and developer expertise apply. |
| **No Kernel Dependency** | Runs entirely in userspace. Works on systems without eBPF-capable kernels, without root. |

### Potential EIM Specification for NCCL

```
Extension Entries:
  - ncclAllReduce_entry: hook at ncclAllReduce function entry
  - ncclSend_entry: hook at ncclSend function entry
  - ncclRecv_entry: hook at ncclRecv function entry

State Capabilities:
  - readCommRank: read communicator rank (read-only)
  - readDataSize: read data size parameter (read-only)
  - readSrcDst: read source/destination rank (read-only)

Function Capabilities:
  - logEvent: call event logging helper
  - getRateLimit: call rate-limit check helper
  - getAllowList: call ACL lookup helper

Extension Classes:
  - MonitorPolicy: readCommRank + readDataSize + logEvent (read-only monitoring)
  - FirewallPolicy: readCommRank + readSrcDst + getAllowList (access control)
  - RateLimitPolicy: readDataSize + getRateLimit (traffic shaping)
```

### Limitations and Concerns

1. **eBPF Instruction Set Restrictions**: eBPF programs cannot use arbitrary C features -- no dynamic memory allocation, limited loop constructs (bounded loops only), 512-byte stack limit. Complex policy logic may need careful structuring.

2. **Map Size Limitations**: Shared memory maps have fixed sizes. Large topology or routing tables may require careful design.

3. **PREVAIL Verifier Maturity**: The userspace PREVAIL verifier is less mature than the Linux kernel verifier. Some edge cases in safety checking may exist. The kernel verifier can be used as fallback when available.

4. **MPK Domain Limit**: Intel MPK supports only 16 protection domains. If many independent extensions run simultaneously, domain multiplexing is needed.

5. **GPU Hook Maturity**: GPU kernel hooking (PTX injection) is experimental and documented as 10x faster than NVbit, but may have stability concerns for production use.

6. **Active Development**: bpftime is "currently under active development and refactoring towards v2" -- APIs may change. However, the OSDI 2025 publication indicates research maturity.

7. **Helper Function Coverage**: Some kernel eBPF helpers and kfuncs are not available in userspace. Custom helpers would need to be implemented for NCCL-specific operations.

8. **ARM/Non-Intel Platforms**: MPK is Intel-specific. On ARM-based systems (e.g., Grace Hopper), alternative isolation mechanisms would be needed.

---

## 5. Existing Integrations with Non-Kernel Workloads

The OSDI 2025 paper and project documentation demonstrate bpftime with several real-world applications:

| Application | Use Case | Results |
|---|---|---|
| **Nginx** | Firewall extension, sslsniff monitoring | 2% overhead (vs 11-12% Lua/Wasm), module for routing/security policies |
| **Redis** | Durability tuning extension | 1.5x throughput improvement |
| **Sidecar-free observability** | Function tracing, malloc monitoring | 10x faster than kernel uprobes |
| **XDP networking** | Userspace packet processing (AF_XDP, DPDK) | Kernel-equivalent functionality in userspace |
| **GPU workloads** | CUDA kernel tracing and instrumentation | 10x faster than NVbit, PTX/SPIR-V support |
| **Solana blockchain** | Smart contract execution (different project, same concept) | Production use of eBPF JIT for safe execution |

---

## 6. Broader Ecosystem: Userspace eBPF for Safe Extensibility

### Related Work and Approaches

- **Wasm (WebAssembly)**: Alternative sandboxed extension model. bpftime achieves 5-6x better performance than Wasm for application extensions due to eBPF's simpler instruction set and LLVM JIT.

- **Lua extensions**: Common in Nginx/Redis. bpftime achieves 10% less overhead than Lua for equivalent Nginx extensions.

- **Kernel eBPF for policy**: Used in Cilium, Falco, and other systems. bpftime brings the same model to userspace with lower overhead.

- **Femto-Containers**: eBPF-based isolation for IoT devices (RIOT OS). Demonstrates eBPF's applicability to resource-constrained environments.

- **RapidPatch**: eBPF-based hotpatching for embedded devices. Shows eBPF as a safe code delivery mechanism.

- **eBPF-PATROL**: Policy enforcement using eBPF probes with userspace policy management. Demonstrates the policy enforcement pattern we would use.

### Key Insight for NCCL

The eBPF extension model (verification + JIT + hardware isolation) is uniquely suited for performance-critical systems because:
1. **Load-time verification** means zero runtime safety overhead
2. **JIT compilation** means near-native execution speed
3. **MPK isolation** means hardware-enforced memory safety with minimal cycle cost
4. **The programming model** is constrained enough to be verifiable but expressive enough for policy logic

This combination is exactly what a NCCL policy plane needs: the ability to run untrusted/dynamic policy code in the hot path of GPU communication with negligible performance impact and strong safety guarantees.

---

## Sources

- [bpftime GitHub Repository](https://github.com/eunomia-bpf/bpftime)
- [OSDI 2025 Paper: "Extending Applications Safely and Efficiently"](https://www.usenix.org/conference/osdi25/presentation/zheng-yusheng)
- [OSDI 2025 Paper PDF (alternate)](https://people.cs.vt.edu/djwillia/papers/osdi25-bpftime.pdf)
- [EIM Specification](https://eunomia.dev/bpftime/EIM/spec/)
- [EIM Overview](https://eunomia.dev/bpftime/EIM/)
- [llvmbpf GitHub Repository](https://github.com/eunomia-bpf/llvmbpf)
- [bpftime Performance Benchmarks](https://eunomia.dev/en/bpftime/documents/performance/)
- [bpf-benchmark Suite](https://github.com/eunomia-bpf/bpf-benchmark)
- [bpftime arXiv Paper](https://arxiv.org/abs/2311.07923)
- [OSDI 2025 Talk Script](https://eunomia.dev/others/miscellaneous/osdi-talk/)
- [Nginx eBPF Module](https://github.com/eunomia-bpf/Nginx-eBPF-module)
- [Userspace eBPF Runtimes Overview](https://eunomia.dev/tutorials/36-userspace-ebpf/)
- [Building High-Performance Userspace eBPF VMs with LLVM](https://eunomia.dev/en/blogs/llvmbpf/)
- [bpftime Documentation](https://eunomia.dev/bpftime/)
- [ERIM: Secure, Efficient In-process Isolation with MPK](https://www.usenix.org/system/files/sec19-vahldiek-oberwagner_0.pdf)
