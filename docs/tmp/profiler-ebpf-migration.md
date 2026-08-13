# Profiler eBPF migration

## What changed

NCCLbpf now has a real profiler-side eBPF program at
`src/ebpf-policies/profiler_latency.bpf.c`. Its `SEC("profiler")`
`record_latency()` entry point receives `nccl_profiler_ctx` (collective type,
node count, bytes, measured latency, and channel count), looks up the typed
`telemetry_map`, checks the nullable result before reading it, updates the
latency aggregates, and writes the result back. The observed profiler channel
count occupies the former tail padding in `nccl_policy_telemetry_value`, so the
map value remains 48 bytes.

The profiler event paths in `plugin.cpp` now call a common telemetry writer.
`NCCL_POLICY_PROFILER_MODE=native` (the default) retains the old C++ update.
`NCCL_POLICY_PROFILER_MODE=ebpf` loads the object named by
`NCCL_POLICY_PROFILER_BPF_PATH`, verifies and JIT-compiles it independently of
the tuner, and executes it after each completed kernel-timed or CE collective.
An eBPF execution error is logged and falls back to the native update for that
sample. A missing or invalid object in explicitly selected `ebpf` mode fails
profiler initialization rather than silently claiming that eBPF is active.

## How the programs share one map

`telemetry_map` is now owned by `SharedCommState`, whose instances are joined by
`commHash` through the existing `g_comm_registry`. When either object is loaded,
the loader creates one runtime map named `comm_<commHash>_telemetry_map`, or
reuses the communicator's existing map after checking type, key size, value
size, capacity, and flags. Relocation of both independently loaded objects then
substitutes that same bpftime fd for their `telemetry_map` symbol. Other maps
remain policy-instance-local and retain the existing
`comm_<commHash>_policy_<policy_instance_id>_` namespace.

The shared map has reference-counted lifetime independent of a loaded program.
Profiler writes and map-backed tuner executions serialize through the
communicator telemetry mutex. This prevents a profiler read/modify/write from
losing a simultaneous tuner update while leaving mapless tuner policies on
their previous one-mutex execution path.

## Verification and end-to-end demonstration

The new BPF object compiles with `clang-15 -target bpf -O2 -g`. PREVAIL accepts
it in strict mode; the integration verifier matrix now reports 15/15 expected
verdicts, including:

```text
| profiler_latency | valid | ACCEPTED | ACCEPTED | PASS |
verifier matrix: PASS (15 programs)
```

As a compatibility sweep, all 24 policy sources compile as BPF objects, and all
17 objects other than the seven intentional verifier-negative controls
strict-load, verify/JIT, and execute once through the plugin. This covers every
pre-existing tuner policy as well as the new profiler object; the dedicated
bridge test separately executes the profiler with its profiler context.

`test_ebpf_plugin` runs the same profiler-event sequence once through the
native writer and once through the separately loaded eBPF writer. In the eBPF
case, two kernel-channel timings produce 520 ns and 920 ns collective samples,
the tuner reads those values from the shared map, and `adaptive_channels`
changes 8 -> 9 -> 8 channels. The relevant output is:

```text
PROFILER/Plugin: kernel/ebpf ... latency_ns=520 channels=2 samples=1
PROFILER/Plugin: kernel/ebpf ... latency_ns=920 channels=2 samples=2
profiler telemetry bridge (ebpf): PASS
```

The harness also seeds five samples, reloads
`adaptive_channels -> noop -> adaptive_channels`, checks that the map fd and
values are unchanged, and confirms that the reloaded tuner returns the
preserved seven-channel recommendation:

```text
telemetry hot-reload persistence: PASS (fd=10 samples=5)
```

Manual GPU reproduction uses the normal plugin invocation with these additions
(paths shown relative to the repository):

```bash
NCCL_POLICY_BPF_PATH=src/nccl-policy-plugin/build/ebpf-policies/adaptive_channels.bpf.o \
NCCL_POLICY_PROFILER_MODE=ebpf \
NCCL_POLICY_PROFILER_BPF_PATH=src/nccl-policy-plugin/build/ebpf-policies/profiler_latency.bpf.o \
NCCL_TUNER_PLUGIN=src/nccl-policy-plugin/build/libnccl-policy.so \
NCCL_PROFILER_PLUGIN=src/nccl-policy-plugin/build/libnccl-policy.so \
mpirun -np 2 nccl-tests/build/all_reduce_perf_mpi -b 1M -e 128M -g 1
```

With NCCL profiler logging enabled, `kernel/ebpf` lines show the sample count.
Running the integration harness after the documented CMake build provides the
deterministic no-GPU reproduction:

```bash
src/nccl-policy-plugin/build/test_ebpf_plugin \
  src/nccl-policy-plugin/build/libnccl-policy.so
```

## Measurements that must be rerun

1. **Profiler-to-tuner composability (§5.3 / `sec:eval-composability`)** must be
   rerun in `ebpf` mode. The published 2 -> 12 -> 2 -> 12 curve currently
   validates the native C++ writer, not eBPF-to-eBPF composition. Report the
   profiler object, strict-verifier result, and mode in the experiment record.
2. **Table `overhead`** should be rerun in full on the paper's CPU. The new
   profiler program is not invoked by a tuner-only `getCollInfo()` benchmark,
   and `noop`/`size_aware_v2` retain the mapless hot path. Map-backed policies
   now serialize against the independently executing profiler through a
   communicator-owned mutex instead of only a per-program mutex, so the
   `lookup_only`, `lookup_update`, `adaptive_channels`, and `slo_enforcer` rows
   need fresh numbers. Add a separate native-versus-eBPF profiler-update
   microbenchmark; the current table does not measure profiler callback cost.
3. **Verifier coverage** must be rerun/reported because the matrix is now eight
   accepted and seven rejected programs (15 total), not seven and seven.
4. **Hot-reload timing and initialization cost** should be rerun. Reloading a
   map-backed tuner now reuses rather than creates `telemetry_map`, while eBPF
   profiler mode adds a second one-time verify/JIT load. The atomic publication
   mechanism and zero-call-loss criterion are unchanged.

The message-size throughput figure, multi-communicator differentiation, and net
plugin experiment do not require reruns solely for this patch when the profiler
remains in the default native mode. Any end-to-end result presented as using
the new two-program closed loop should, however, be collected again in `ebpf`
mode.

## Hot-reload map-state finding

Preserving telemetry across reload was clean and low-risk enough to implement
for this shared map. A loaded program no longer owns `telemetry_map`; the
communicator does. Old and new tuner states can therefore overlap safely during
the existing atomic `shared_ptr` exchange while both hold references to the
same map object. A replacement with an incompatible telemetry schema is
rejected before publication. State lasts for the communicator lifetime, not a
process restart, and is reclaimed after the final tuner/profiler reference.

This does not make every map persistent. Policy-private maps, including seeded
configuration and implementation-specific state, still receive fresh
per-instance names on reload. Generalizing persistence to arbitrary logical map
names is feasible through a communicator map registry keyed by name plus schema,
but has higher risk: unrelated policies may reuse names with different
semantics, schema migration needs an explicit policy, stale state may be unsafe
for a new algorithm, and kernel-user map replacement needs separate ownership
rules. Keeping only the cross-program telemetry ABI persistent captures the
closed-loop/hot-reload composition promised by the paper without broadening
those risks.
