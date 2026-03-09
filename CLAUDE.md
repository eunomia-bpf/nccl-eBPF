# CLAUDE.md

This file provides guidance to Claude Code when working with code in this repository.

## Project Overview

Research project exploring **eBPF-based policy plane for NCCL** (NVIDIA Collective Communication Library). The core thesis: transform NCCL from a black-box communication library into a **governable datapath** using verifiable, isolated, composable eBPF policies executed via bpftime/EIM.

**Key difference from "yet another tuner"**: We target SLO enforcement, multi-tenant fairness, fault resilience, and safe extensibility — not just "pick the fastest algorithm."

### Research Components

1. **Policy Hooks**: Intercept NCCL decision points (collective selection, network path, resource allocation, fault handling) via NCCL's plugin system (tuner/net/env/profiler plugins)
2. **Policy Execution**: Run eBPF policy programs inside NCCL's address space via bpftime (LLVM JIT, verification, MPK isolation)
3. **Policy Semantics**: EIM-based capability model defining what each policy can access/modify
4. **Telemetry Loop**: NCCL inspector/profiler → eBPF maps → policy decisions → NCCL actions (closed-loop governance)

### Related Projects (Same Team)

- **bpftime**: Userspace eBPF runtime (OSDI 2025) — https://github.com/eunomia-bpf/bpftime
- **llvmbpf**: LLVM-based BPF JIT/AOT compiler — https://github.com/eunomia-bpf/llvmbpf
- **EIM**: Extension Interface Model specification — https://eunomia.dev/bpftime/EIM/spec/

## Build & Run

### Prerequisites
- clang/llvm (BPF programs compiled with `clang -target bpf`)
- cmake, pkg-config, libelf-dev, zlib1g-dev, libzstd-dev

### Build
```bash
git submodule update --init --recursive
make build
```

## Key Directories

- `src/` — eBPF programs and userspace loader (currently bootstrap template, to be replaced)
- `docs/research/` — Research notes and analysis
- `docs/tmp/` — Codex-generated working documents (ephemeral)
- `vmlinux/` — Kernel headers for BPF CO-RE
- `libbpf/`, `bpftool/` — Vendored dependencies

## Using Codex CLI as Subagent

OpenAI Codex CLI is available on this machine. Use it as a subagent for research, code exploration, and implementation tasks.

### Division of Labor (IMPORTANT)
- **Codex handles**: Deep research (NCCL internals, plugin APIs, related work), code exploration (reading NCCL source, bpftime source), implementation drafts, experiment scripts, data analysis
- **Claude Code handles**: Scheduling/dispatching codex tasks, document synthesis, TODO/memory updates, architectural decisions, reviewing codex output
- **Claude Code must NEVER**: Do deep multi-file research directly when codex can do it faster — always delegate

### Workflow Rules
- **Codex output goes to `docs/tmp/`** — codex writes research/analysis/design docs into `docs/tmp/`
- **Claude maintains non-tmp docs** — Claude directly edits `CLAUDE.md`, `docs/research/`, and other permanent documents
- **Codex runs in background** — use `run_in_background: true` for all codex tasks; Claude dispatches and moves on
- **Review cycle** — when codex produces a new document, dispatch another codex to review/expand it
- **Never ask for confirmation** — just keep going, do all work, iterate multiple rounds autonomously

### Usage
```bash
# Non-interactive execution
codex exec --dangerously-bypass-approvals-and-sandbox "your prompt here"

# With a specific working directory
codex exec --dangerously-bypass-approvals-and-sandbox -C /path/to/dir "your prompt here"
```

## Research Directions (Priority Order)

1. **Multi-tenant QoS/Fairness** — Policy-driven SLO enforcement for shared GPU clusters
2. **Safe Extensibility** — bpftime/EIM as policy sandbox for NCCL plugins (verifiable, MPK-isolated)
3. **Fault Resilience** — Policy-driven elastic recovery (revoke/shrink/grow)
4. **Compute-Communication Interference** — Policy controlling SM/CopyEngine/zero-SM paths
5. **Programmable Network Device Policy** — eBPF policies for NIC selection, vNIC, device offload
6. **Cross-vendor Portability** — Same policy IR on NCCL + RCCL (AMD)

## Paper Target

Systems venues: OSDI, NSDI, EuroSys, ATC, ASPLOS. Core contribution: first system to bring verifiable, isolated policy execution (eBPF + EIM) into GPU collective communication runtimes.
