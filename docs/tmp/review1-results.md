# Review #1: bpftime-migrated NCCL Policy Plugin + Policies

## Scope and method

Reviewed:

- `src/nccl-policy-plugin/plugin.cpp`
- `src/nccl-policy-plugin/CMakeLists.txt`
- `src/nccl-policy-plugin/native_baseline.cpp`
- `src/nccl-policy-plugin/native_baseline.h`
- `src/nccl-policy-plugin/test/test_ebpf_plugin.c`
- `src/ebpf-policies/*.bpf.c`
- `src/ebpf-policies/*.h`

Validation performed:

- source review with line-level inspection
- fresh configure/build in `/tmp/nccl-policy-review-build`
- run of the existing benchmark harness against the prebuilt plugin
- disassembly of the generated `.bpf.o` files

Observed build/runtime status:

- `cmake -S src/nccl-policy-plugin -B /tmp/nccl-policy-review-build -DCMAKE_BUILD_TYPE=Release`: passed
- `cmake --build /tmp/nccl-policy-review-build -j8`: passed
- `src/nccl-policy-plugin/build-bpftime-migration-prebuilt/test_ebpf_plugin src/nccl-policy-plugin/build-bpftime-migration-prebuilt/libnccl-policy.so`: passed

The tree is buildable, but there are correctness gaps that should be fixed before using the adaptive policies for benchmarking.

## Priority-ranked fixes needed

1. Fix the telemetry model in `plugin.cpp`: `last_latency_ns`, `avg_latency_ns`, and `rolling_p99_ns` currently measure plugin execution time, not NCCL collective latency. This makes `adaptive_channels` and `slo_enforcer` optimize the wrong signal.
2. Register bpftime verifier map descriptors before strict verification of map-backed programs. The current loader creates maps and rewrites map FDs, but does not populate verifier map metadata, which likely explains the strict-mode failures for `adaptive_channels` and `slo_enforcer`. This is an inference from the bpftime verifier API/tests.
3. Remove or isolate the state-mutating warmup path for map-backed policies. The current warmup inserts synthetic telemetry before the first real `getCollInfo()` call.
4. Make `PluginContext` state updates thread-safe. The current implementation has unsynchronized read/write access to mutable fields in `getCollInfo()`, which is UB under concurrent use.
5. Fix `slo_enforcer.bpf.c` unsigned underflow/overflow around `aggressiveness_step`, and validate seeded config values in `plugin.cpp`.
6. Make the benchmark harness model NCCL’s real cost-table contract and add correctness assertions. Right now it is mostly a latency microbenchmark, not a policy validation harness.
7. Make the build use a matched Clang/LLVM toolchain and declare libbpf/libelf dependencies explicitly.

## Per-file findings

### `src/nccl-policy-plugin/plugin.cpp`

- High: `getCollInfo()` feeds policy execution latency back into the policy context as if it were collective telemetry. `start_ns` is taken immediately before `bpftime_prog_exec()` at lines 687-708, and `last_latency_ns`/`rolling_p99_ns` are then written back at lines 709-723 and exposed to the next policy invocation at lines 695-700. As a result:
  - `adaptive_channels.bpf.c` reacts to plugin dispatch overhead, not NCCL latency.
  - `slo_enforcer.bpf.c` enforces an SLO on plugin overhead, not collective completion time.
  - any benchmarking conclusions about policy quality are invalid until real telemetry is plumbed in.

- High: strict verifier integration for map-backed policies is incomplete. The loader creates bpftime maps at lines 230-258 and rewrites relocations at lines 290-437, but `verify_program()` at lines 439-479 never calls `bpftime::verifier::set_map_descriptors(...)`. bpftime’s verifier API requires map descriptors for map FDs; without them, map-using programs are typically rejected. This is the most likely reason `adaptive_channels` and `slo_enforcer` fail strict mode today. This is an inference, but it is strongly supported by bpftime’s verifier interface and tests.

- High: `warmup_program()` mutates state for stateful policies before the first real call. Lines 581-607 execute the policy once with a fake 1024-byte AllReduce and record whatever map updates the policy performs. For `adaptive_channels` and `slo_enforcer`, this seeds telemetry with synthetic data and changes first-call behavior.

- High: `PluginContext` state is not synchronized. Mutable fields like `call_count`, `total_latency_ns`, `last_latency_ns`, `rolling_p99_ns`, and `last_channels` are read/written from `pluginGetCollInfoImpl()` at lines 693-723 with no mutex/atomics. If NCCL calls the tuner concurrently from multiple host threads, this is a C++ data race and therefore UB.

- Medium: `acquire_bpftime_runtime()` always does `SHM_REMOVE_AND_CREATE` on first use at lines 136-149. If `BPFTIME_GLOBAL_SHM_NAME` is intentionally set to share an existing runtime, this code will still recreate the shared memory segment and can break that arrangement.

- Medium: ELF relocation parsing is not hardened against malformed inputs. In `relocate_program_maps()`:
  - `symtab_shdr.sh_entsize` is used as a divisor at line 360 without checking for zero.
  - `shdr.sh_entsize` is used as a divisor at line 393 without checking for zero.
  - `elf_getdata(symtab_scn, nullptr)` at line 359 is not checked for null before symbol iteration.
  A malformed or hostile BPF object can crash the plugin instead of being rejected cleanly.

- Medium: hot-path logging materially perturbs the benchmark signal. The plugin emits `stderr` logs during init (lines 603-606), every first five calls and every 100000th call (lines 725-731), and finalize (lines 742-749). That is acceptable for bring-up, but it should not be enabled in benchmark mode by default.

- Low: `ensure_bpftime_shm_name()` will not replace an already-present but empty `BPFTIME_GLOBAL_SHM_NAME`, because it calls `setenv(..., 0)` at line 104. If the variable exists but is empty, the function still leaves it empty.

- Low: `log_plugin_message()` reports the line number of the logging wrapper itself (`__LINE__` at line 133), not the original call site, so NCCL-integrated logs lose source precision.

### `src/nccl-policy-plugin/CMakeLists.txt`

- Medium: the build pins `llvm-config-15` at line 12 but uses generic `clang` at line 11. On this host, `clang --version` reports `18.1.3`, while `llvm-config-15 --version` reports `15.0.7`. That mismatch is avoidable and can become a real problem once the BPF code or JIT path depends on version-specific LLVM behavior.

- Medium: the target links against vendored/prebuilt `libbpf.a` and uses `<bpf/libbpf.h>` and `<gelf.h>` in `plugin.cpp`, but the build does not declare libbpf/libelf header discovery explicitly. The build passed here because the host already has those development headers installed, not because the CMake target fully specifies its dependencies.

- Medium: the build hard-codes bpftime internal archive paths at lines 30-47. That works for the current tree layout, but it is brittle against bpftime build-directory changes and makes the project harder to reproduce outside this workspace.

- Low: the harness is made to compile as C++ at lines 148-161 even though it is named `.c`. That is not wrong, but it hides whether the test still compiles in a pure C mode.

### `src/nccl-policy-plugin/native_baseline.cpp`

- Low: the baseline logic itself is coherent and matches `size_aware_v2` closely, but it hard-codes `coll_type == 4` at lines 9 and 29 instead of using a shared symbolic constant. That creates an undocumented ABI dependency between the policy context producer and the policy logic.

- Low: there is no guard ensuring this baseline stays in sync with `size_aware_v2.bpf.c`. The harness times both, but it does not assert equivalence over a representative input set.

### `src/nccl-policy-plugin/native_baseline.h`

- No ABI bug found. The `extern "C"` wrapper is correct for the current mixed C/C++ use.

- Low: this header is minimal, but it inherits the same undocumented `coll_type` numbering contract through `policy_context.h`.

### `src/nccl-policy-plugin/test/test_ebpf_plugin.c`

- High: `reset_cost_table()` at lines 77-89 does not model NCCL’s `getCollInfo()` contract. It fills every entry with `1.0f`, whereas NCCL uses `NCCL_ALGO_PROTO_IGNORE` for unsupported algo/proto pairs. This means the harness can report a forced choice as “working” even when real NCCL would ignore that pair.

- High: the harness never checks policy correctness across the input space. `benchmark_policy()` loops for 1,000,000 iterations at lines 197-223, but only captures the last iteration’s decision at lines 224-229. A policy that is wrong for most inputs still passes as long as the calls return `ncclSuccess`.

- High: the native baseline is not compared against the plugin outputs, only timed. `benchmark_native_size_aware()` is called at lines 299-302, but there is no equivalence check against `size_aware_v2`.

- Medium: the default plugin path at line 280 is `./libnccl-policy.so`, so the harness only works from the right working directory unless the caller passes `argv[1]`.

- Medium: the verifier test only checks that `bad_lookup.bpf.o` is rejected in strict mode (lines 236-275, 326-330). It does not check the positive case that good map-backed policies should also pass strict mode once verifier integration is correct.

- Medium: the harness does not exercise any of the known risk areas from Phase 1.5:
  - no multi-threaded access (`-t 2`)
  - no multi-communicator lifetime overlap
  - no real NCCL collective validation
  - no map-state assertions for `adaptive_channels` / `slo_enforcer`

- Low: `benchmark_native_size_aware()` stores `sink ^= action` and then records `result->last_channels = (int)(sink & 0xffu)` at line 144, but the low byte is the packed algorithm field, not channels, and XOR does not represent the last result anyway. This does not affect the printed benchmark output today, but the recorded field is meaningless.

### `src/ebpf-policies/noop.bpf.c`

- No correctness issue found. This is a valid minimal stateless policy and should pass strict verification.

### `src/ebpf-policies/size_aware.bpf.c`

- No verifier issue found in the generated BPF. This should pass strict verification.

- Low: it is stateless and straightforward, but it only covers TREE/RING plus three protocols and does not model the full NCCL v5 algorithm space.

### `src/ebpf-policies/size_aware_v2.bpf.c`

- No verifier issue found in the generated BPF. This should pass strict verification.

- Low: `pick_algo()` and `pick_channels()` use the magic literal `4` for AllReduce at lines 7 and 27. This should be replaced by a shared symbolic constant to make the ABI obvious and keep the native baseline and eBPF policy aligned.

### `src/ebpf-policies/adaptive_channels.bpf.c`

- Medium: the policy currently fails strict mode in the plugin, but the generated BPF looks structurally valid. The most likely blocker is the plugin-side verifier setup, not this source file.

- Medium: `prev->recommended_channels` is trusted at line 56 without normalization. If the map contains `0` or an out-of-range value, the policy can emit an invalid or unintended channel count.

- Low: the decision thresholds (`+32 ns`, `1 MiB`) at lines 57-63 are hard-coded and undocumented. That is manageable for experimentation, but not yet a stable policy interface.

### `src/ebpf-policies/slo_enforcer.bpf.c`

- High: channel arithmetic can underflow/overflow around `aggressiveness_step`.
  - increase path: `channels + cfg->aggressiveness_step` at line 69 can overflow before clamping.
  - decrease path: `channels - cfg->aggressiveness_step` at line 76 can underflow to a very large `u32`, which `clamp_to_config()` then collapses to `max_channels`.
  This means a badly seeded config can make the “decrease channels” branch increase to max instead.

- Medium: this policy currently fails strict mode in the plugin, but the generated BPF looks structurally valid. As above, the loader/verifier setup is the more likely root cause.

- Medium: the policy stores `uint32_t recommended_channels` in telemetry at line 95 but returns `(uint8_t)channels` at line 99. If config values above 255 are ever seeded, map state and emitted actions diverge silently.

### `src/ebpf-policies/bad_lookup.bpf.c`

- Expected failure: line 24 dereferences the result of `bpf_map_lookup_elem()` without a null check. This is a correct negative test and should fail strict verification.

- Low: because this is intentionally invalid, it should remain clearly isolated as a test artifact and not be treated as a deployable policy.

### `src/ebpf-policies/policy_context.h`

- Medium: the header claims to expose latency telemetry (`last_latency_ns`, `avg_latency_ns`, `rolling_p99_ns`), but the current producer in `plugin.cpp` fills those fields with tuner execution overhead rather than collective telemetry. The header is fine; the producer contract is not.

- Low: `coll_type` is an untyped `uint32_t` with no shared enum/constants. That is why both the native and eBPF policies rely on magic literal `4`.

### `src/ebpf-policies/policy_action.h`

- Low: the packing format is internally consistent, but only TREE/RING symbolic constants are defined even though NCCL v5 exposes more algorithm IDs. This is a missing-feature gap, not an immediate bug.

### `src/ebpf-policies/policy_maps.h`

- Medium: map values use 32-bit channel counts (`recommended_channels`, `max_channels`) while the action ABI returns only 8 bits. That mismatch is harmless under the current defaults, but it is a latent configuration bug.

### `src/ebpf-policies/bpf_compat.h`

- Medium: map declarations rely on BTF-style `__uint/__type` metadata only. That is fine for the current loader path, but it creates an implicit dependency on BTF-aware object parsing and makes the policy objects less portable to simpler loaders.

## Specific code changes suggested

### 1. Register map descriptors before verification

File: `src/nccl-policy-plugin/plugin.cpp`

Why:

- This is the most likely root cause of the current strict verifier failures for `adaptive_channels` and `slo_enforcer`.

Illustrative diff:

```diff
diff --git a/src/nccl-policy-plugin/plugin.cpp b/src/nccl-policy-plugin/plugin.cpp
@@
 struct PluginContext {
   std::unique_ptr<bpftime::bpftime_prog> prog;
@@
   std::unordered_map<std::string, int> map_fds;
+  std::map<int, bpftime::verifier::BpftimeMapDescriptor> verifier_maps;
 };
@@
 bool create_bpftime_maps(PluginContext *ctx, struct bpf_object *obj)
 {
   struct bpf_map *map = nullptr;
@@
     fd = bpftime_maps_create(-1, bpf_map__name(map), attr);
@@
     ctx->map_fds.emplace(bpf_map__name(map), fd);
+    ctx->verifier_maps.emplace(
+        fd, bpftime::verifier::BpftimeMapDescriptor{
+                .original_fd = fd,
+                .type = static_cast<uint32_t>(attr.type),
+                .key_size = attr.key_size,
+                .value_size = attr.value_size,
+                .max_entries = attr.max_ents,
+                .inner_map_fd = 0,
+            });
   }
@@
 bool verify_program(PluginContext *ctx, const ProgramSpec &spec)
 {
@@
+  bpftime::verifier::set_map_descriptors(ctx->verifier_maps);
   bpftime::verifier::set_available_helpers(helper_ids);
   bpftime::verifier::set_non_kernel_helpers({});
 ```

### 2. Stop seeding fake telemetry during warmup

File: `src/nccl-policy-plugin/plugin.cpp`

Why:

- warmup currently changes stateful policy behavior before the first real `getCollInfo()` call
- until there is a side-effect-free warmup path, stateful policies should not be warmed this way

Illustrative diff:

```diff
diff --git a/src/nccl-policy-plugin/plugin.cpp b/src/nccl-policy-plugin/plugin.cpp
@@
-  if (!loaded || !ctx->prog || !warmup_program(ctx)) {
+  const bool safe_to_warmup = ctx->map_fds.empty();
+  if (!loaded || !ctx->prog || (safe_to_warmup && !warmup_program(ctx))) {
     delete ctx;
     release_bpftime_runtime();
     return ncclInternalError;
   }
 ```

If warmup is required for all policies, the better fix is a separate “dry-run” execution path that does not touch telemetry maps.

### 3. Stop using plugin-overhead timing as policy telemetry

File: `src/nccl-policy-plugin/plugin.cpp`

Why:

- current adaptive/SLO policy behavior is semantically wrong

Minimum safe change:

- zero these fields until real collective telemetry exists:
  - `policy_ctx.last_latency_ns`
  - `policy_ctx.avg_latency_ns`
  - `policy_ctx.rolling_p99_ns`
- remove the feedback loop based on `bpftime_prog_exec()` latency

Illustrative diff:

```diff
diff --git a/src/nccl-policy-plugin/plugin.cpp b/src/nccl-policy-plugin/plugin.cpp
@@
-  const uint64_t start_ns = monotonic_time_ns();
   int err = 0;
@@
-  policy_ctx.last_latency_ns = ctx->last_latency_ns;
-  policy_ctx.avg_latency_ns =
-      ctx->call_count == 0 ? 0 : ctx->total_latency_ns / ctx->call_count;
-  policy_ctx.rolling_p99_ns = ctx->rolling_p99_ns;
+  policy_ctx.last_latency_ns = 0;
+  policy_ctx.avg_latency_ns = 0;
+  policy_ctx.rolling_p99_ns = 0;
@@
   err = ctx->prog->bpftime_prog_exec(&policy_ctx, sizeof(policy_ctx), &action);
-  ctx->last_latency_ns = monotonic_time_ns() - start_ns;
-  ctx->rolling_p99_ns =
-      update_p99_estimate(ctx->rolling_p99_ns, ctx->last_latency_ns);
@@
-  ctx->total_latency_ns += ctx->last_latency_ns;
   ctx->call_count++;
 ```

This does not add the missing real telemetry, but it stops feeding misleading data into stateful policies.

### 4. Add saturating arithmetic for `slo_enforcer`

File: `src/ebpf-policies/slo_enforcer.bpf.c`

Why:

- current unsigned wraparound can reverse policy intent

Illustrative diff:

```diff
diff --git a/src/ebpf-policies/slo_enforcer.bpf.c b/src/ebpf-policies/slo_enforcer.bpf.c
@@
+static uint32_t saturating_add_u32(uint32_t a, uint32_t b)
+{
+  uint32_t out = a + b;
+  if (out < a)
+    return 0xffffffffu;
+  return out;
+}
+
+static uint32_t saturating_sub_u32(uint32_t a, uint32_t b)
+{
+  if (a < b)
+    return 0;
+  return a - b;
+}
@@
-    channels = clamp_to_config(channels + cfg->aggressiveness_step, cfg);
+    channels = clamp_to_config(
+        saturating_add_u32(channels, cfg->aggressiveness_step), cfg);
@@
-    channels = clamp_to_config(channels - cfg->aggressiveness_step, cfg);
+    channels = clamp_to_config(
+        saturating_sub_u32(channels, cfg->aggressiveness_step), cfg);
 ```

### 5. Make the harness model NCCL’s real cost-table semantics

File: `src/nccl-policy-plugin/test/test_ebpf_plugin.c`

Why:

- the current harness can accept decisions that real NCCL would ignore

Illustrative diff:

```diff
diff --git a/src/nccl-policy-plugin/test/test_ebpf_plugin.c b/src/nccl-policy-plugin/test/test_ebpf_plugin.c
@@
 static void reset_cost_table(float cost_table[NCCL_NUM_ALGORITHMS]
                                              [NCCL_NUM_PROTOCOLS],
                              float *cost_table_ptr[NCCL_NUM_ALGORITHMS])
 {
@@
-      cost_table[i][j] = 1.0f;
+      cost_table[i][j] = NCCL_ALGO_PROTO_IGNORE;
     }
   }
+
+  cost_table[NCCL_ALGO_TREE][NCCL_PROTO_SIMPLE] = 1.0f;
+  cost_table[NCCL_ALGO_RING][NCCL_PROTO_LL] = 1.0f;
+  cost_table[NCCL_ALGO_RING][NCCL_PROTO_SIMPLE] = 1.0f;
 }
 ```

This should be followed by targeted assertions for representative inputs instead of only printing the last iteration’s result.

### 6. Use a matched Clang/LLVM version

File: `src/nccl-policy-plugin/CMakeLists.txt`

Why:

- current build mixes `clang` 18 with LLVM 15 libraries

Illustrative diff:

```diff
diff --git a/src/nccl-policy-plugin/CMakeLists.txt b/src/nccl-policy-plugin/CMakeLists.txt
@@
-find_program(CLANG_EXECUTABLE NAMES clang REQUIRED)
-find_program(LLVM_CONFIG_15_EXECUTABLE NAMES llvm-config-15 REQUIRED)
+find_program(LLVM_CONFIG_EXECUTABLE NAMES llvm-config-15 llvm-config REQUIRED)
+execute_process(
+    COMMAND ${LLVM_CONFIG_EXECUTABLE} --version
+    OUTPUT_VARIABLE LLVM_VERSION
+    OUTPUT_STRIP_TRAILING_WHITESPACE)
+string(REGEX MATCH "^[0-9]+" LLVM_MAJOR "${LLVM_VERSION}")
+find_program(CLANG_EXECUTABLE NAMES clang-${LLVM_MAJOR} clang REQUIRED)
@@
-    COMMAND ${LLVM_CONFIG_15_EXECUTABLE} --libdir
+    COMMAND ${LLVM_CONFIG_EXECUTABLE} --libdir
     OUTPUT_VARIABLE LLVM_15_LIBDIR
 ```

## Assessment

### Ready for benchmarking?

Not yet, if the benchmark claim is anything stronger than “how much overhead does bpftime add to `getCollInfo()` for these toy policies?”

Current status by goal:

- Microbenchmarking stateless dispatch overhead (`noop`, `size_aware`, `size_aware_v2`): mostly yes.
- Benchmarking stateful/adaptive policy behavior (`adaptive_channels`, `slo_enforcer`): no.
- Claiming strict-verifier readiness for map-backed policies: no.
- Claiming realistic NCCL tuner v5 behavior from the harness alone: no.

### Minimum bar before proceeding

- fix verifier map-descriptor wiring
- stop feeding fake latency telemetry
- remove state-mutating warmup for stateful policies
- harden `slo_enforcer` arithmetic and config validation
- improve the harness so it checks correctness, not just latency

Until those are fixed, the code is buildable and useful for bring-up, but not ready for trustworthy benchmarking of the migrated adaptive policies.
