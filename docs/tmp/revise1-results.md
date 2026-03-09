# Revise #1 Results

Date: March 9, 2026

## Summary

Implemented the Review #1 fixes across the plugin, policies, CMake, and test
harness.

High-priority fixes completed:

- Registered verifier map descriptors before strict verification in
  `plugin.cpp`, which unblocked strict-mode verification for all map-backed
  policies.
- Removed fake latency feedback from `policy_ctx`; the plugin now leaves
  latency telemetry fields at zero and documents them as placeholders until
  NCCL profiler adapter integration exists.
- Skipped synthetic warmup for map-backed policies so the first real
  `getCollInfo()` call starts from empty map state.
- Added mutex protection for mutable `PluginContext` fields used from
  `getCollInfo()` / `finalize()`.
- Fixed `slo_enforcer` channel arithmetic to avoid unsigned underflow/overflow
  before clamping, and validated seeded channel config from environment
  variables against the action ABI.
- Reworked the harness so it now:
  - asserts correctness for `size_aware` and `size_aware_v2`
  - checks positive strict-mode acceptance for good map-backed policies
  - seeds and verifies `adaptive_channels` map state directly
  - models NCCL’s ignore semantics in the cost table instead of marking every
    algo/proto pair as supported

Medium-priority fixes also completed:

- Hardened ELF relocation parsing against zero `sh_entsize` and null symtab
  data.
- Matched the BPF Clang executable to the discovered `llvm-config` major
  version in CMake.
- Added shared `coll_type` constants in `policy_context.h` and replaced the
  previous magic `4`.

## Build And Validation

Build:

```bash
cmake -S src/nccl-policy-plugin \
      -B src/nccl-policy-plugin/build-bpftime-migration-prebuilt \
      -DCMAKE_BUILD_TYPE=Release
cmake --build src/nccl-policy-plugin/build-bpftime-migration-prebuilt -j8
```

Harness run:

```bash
SPDLOG_LEVEL=off \
  src/nccl-policy-plugin/build-bpftime-migration-prebuilt/test_ebpf_plugin
```

Validation status:

- Strict verifier acceptance: PASS for `noop`, `size_aware`, `size_aware_v2`,
  `adaptive_channels`, and `slo_enforcer`
- Size-aware correctness assertions: PASS
- Adaptive map-state / clean-first-call check: PASS
- Negative strict verifier test (`bad_lookup`): PASS

Note: the build still emits existing bpftime/libbpf ODR warnings from mixed BPF
UAPI headers. They were non-fatal and did not block the plugin or harness.

## 1M-Call Benchmark

All timings are from the harness output above, in nanoseconds.

| Policy | Verify | P50 | P99 | Max |
| --- | --- | ---: | ---: | ---: |
| native_size_aware_v2 | builtin | 10 | 16 | 29181 |
| noop | strict | 42 | 52 | 147988 |
| size_aware | strict | 42 | 52 | 3470 |
| size_aware_v2 | strict | 42 | 53 | 11149 |
| adaptive_channels | strict | 66 | 75 | 38220 |
| slo_enforcer | strict | 78 | 102 | 22601 |

Harness excerpts:

- `verifier acceptance: PASS (5 strict policies)`
- `size_aware correctness: PASS`
- `adaptive_channels map state: PASS`
- `verifier rejection: PASS (.../bad_lookup.bpf.o)`
