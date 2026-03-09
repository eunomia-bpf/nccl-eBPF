# Paper Readiness Review

Date: 2026-03-09

Scope reviewed:
- `docs/paper-outline.md`
- `docs/tmp/benchmark-results.md`
- `docs/tmp/revise2-results.md`
- `docs/tmp/phase3-safety-results.md`
- `docs/tmp/phase4-results.md`
- `docs/tmp/bpftime-migration-results.md`
- `src/nccl-policy-plugin/plugin.cpp`
- `src/ebpf-policies/*.bpf.c`
- `src/nccl-policy-plugin/test/test_ebpf_plugin.c`
- `src/nccl-policy-plugin/test/test_native_crash.c`
- key runtime logs under `docs/tmp/`

## Executive Summary

The current artifact is **not ready for the paper described in `docs/paper-outline.md`**. The implementation and results support a much narrower claim:

> a **bpftime-backed NCCL tuner plugin** that executes user-space eBPF policies in `getCollInfo()`, performs load-time verifier checks, supports atomic hot-reload through a debug hook, achieves sub-100ns CPU-side policy cost, and has been exercised in one real 2-rank NCCL run on a single-GPU/socket-only setup.

That is a legitimate workshop/poster artifact. It is **not yet** a convincing artifact for the stronger paper story about:
- cross-stack kernel<->userspace shared maps,
- a single mixed plugin implementing tuner + net + env + profiler,
- profiler-to-tuner closed-loop feedback with real NCCL telemetry,
- MPK isolation,
- EIM capability enforcement,
- or a multi-tenant governance evaluation.

The biggest problem is not that the current work is weak. The biggest problem is that the **outline over-claims beyond the artifact**. A skeptical reviewer will notice that quickly.

## A. Core System Claims

### A1. "NCCLPol is a cross-stack eBPF policy plane"

Status: **Unsupported by the current artifact**

What is actually implemented:
- All reviewed policy programs are user-space `SEC("uprobe")` programs, not kernel/XDP/TC/kprobe programs. See `src/ebpf-policies/*.bpf.c`.
- The only execution site is the tuner hot path in `pluginGetCollInfoImpl()`, which calls `bpftime_prog_exec()` inside `getCollInfo()` (`src/nccl-policy-plugin/plugin.cpp:816-893`).
- The plugin creates its own bpftime shared-memory namespace per process using `nccl_policy_bpftime_<uid>_<pid>` and initializes it with `SHM_REMOVE_AND_CREATE` (`src/nccl-policy-plugin/plugin.cpp:137-146`, `175-196`).

Why that matters:
- The outline's kernel-side story (`congestion_map`, `error_map`, `bw_quota_map`, XDP/TC/kprobe writers) appears in `docs/paper-outline.md`, but not in the implementation or requested results.
- The current map namespace is process-local by default, which is the opposite of a demonstrated kernel<->userspace shared-state story.

Assessment:
- Today this is a **userspace NCCL tuning policy plane**, not a demonstrated cross-stack one.

### A2. "Single plugin .so implementing tuner + net + env + profiler"

Status: **Unsupported**

What is actually implemented:
- `plugin.cpp` exports only `ncclTunerPlugin_v5` (`src/nccl-policy-plugin/plugin.cpp:1017-1023`).
- The built binary confirms that: `nm -D libnccl-policy.so` shows `ncclTunerPlugin_v5` and no `ncclNetPlugin_*`, `ncclEnvPlugin_*`, or `ncclProfiler_*`.
- CMake builds the shared object from a single `plugin.cpp` (`src/nccl-policy-plugin/CMakeLists.txt:156-184`), and that file only contains tuner entry points.

Runtime evidence:
- In the successful Phase 4 logs, NCCL reports:
  - `ENV/Plugin: Could not find: libnccl-env.so`
  - `NET/Plugin: Could not find: libnccl-net.so`
  - `PROFILER/Plugin: Could not find: libnccl-profiler.so`
  - `TUNER/Plugin: Using eBPFPolicy (v5)`
  (`docs/tmp/phase4-step2-hostid-socket-mpi-device0-fixed.log:16-29`, `61-80`)

Assessment:
- This is a **tuner plugin only**.
- The mixed-plugin claim in the abstract/design section is currently false.

### A3. "Profiler->tuner closed-loop feedback via eBPF maps"

Status: **Unsupported in real NCCL; only synthetic in the test harness**

What the code says:
- `policy_context.h` explicitly marks telemetry fields as placeholders "until NCCL profiler adapter integration provides real collective telemetry" (`src/ebpf-policies/policy_context.h:16-19`).
- In `pluginGetCollInfoImpl()`, the policy context gets telemetry only from `ctx->synthetic_telemetry`; otherwise the latency fields are zero (`src/nccl-policy-plugin/plugin.cpp:846-857`).
- Synthetic telemetry is set only through the exported debug function `ncclPolicyPluginDebugSetSyntheticTelemetry()` (`src/nccl-policy-plugin/plugin.cpp:971-989`, `1053-1055`).

What the tests actually do:
- `test_adaptive_policy_curve()` seeds `telemetry_map` manually and injects fake latency through the debug hook (`src/nccl-policy-plugin/test/test_ebpf_plugin.c:1183-1289`).
- `test_adaptive_channels_map_state()` also manually seeds `telemetry_map` before calling `getCollInfo()` (`src/nccl-policy-plugin/test/test_ebpf_plugin.c:731-810`).

Runtime evidence:
- The real NCCL logs show `PROFILER/Plugin: Could not find: libnccl-profiler.so` in the Phase 4 run (`docs/tmp/phase4-step2-hostid-socket-mpi-device0-fixed.log:61-72`).

Assessment:
- The repository has **map-backed adaptive policies**, but not a real profiler adapter and not a real NCCL telemetry loop.
- The adaptive behavior result is real as a synthetic harness result, not as a demonstrated closed loop in NCCL.

### A4. "bpftime runtime integration"

Status: **Partially supported**

What is real:
- The plugin is no longer just raw `llvmbpf_vm`.
- It links bpftime runtime/verifier libraries (`src/nccl-policy-plugin/CMakeLists.txt:40-80`, `171-181`).
- It initializes bpftime shared memory and helper groups (`src/nccl-policy-plugin/plugin.cpp:175-196`, `238-259`).
- It creates maps with `bpftime_maps_create`, runs a verifier path, and executes policies through `bpftime::bpftime_prog` (`src/nccl-policy-plugin/plugin.cpp:322-362`, `562-608`, `678-701`).

What is still manual:
- The plugin manually opens the ELF, extracts instructions, performs map relocation, constructs verifier metadata, and instantiates `bpftime_prog` from raw instructions (`src/nccl-policy-plugin/plugin.cpp:365-560`, `650-701`).

Assessment:
- The right description is: **"integrated with bpftime runtime primitives and `bpftime_prog`, using a custom loader/relocation path."**
- The wrong description is: **"full bpftime runtime integration with the broader cross-stack runtime model."**

## B. Safety Claims

### B5. "PREVAIL verifier at load time"

Status: **Supported, with wording caveats**

What is real:
- The plugin runs verifier logic before JIT-loading file-backed policies (`src/nccl-policy-plugin/plugin.cpp:650-676`).
- `verify_program()` explicitly sets map/helper metadata and calls `bpftime::verifier::verify_ebpf_program()` (`src/nccl-policy-plugin/plugin.cpp:562-608`).
- The Phase 3 artifact contains a real accept/reject matrix for 14 programs, including negative cases such as null-deref-after-map-lookup, OOB, unregistered helper, stack overflow, infinite loop, invalid context write, and divide-by-zero (`docs/tmp/phase3-safety-results.md:12-35`).
- `test_ebpf_plugin.c` drives that matrix in strict mode (`src/nccl-policy-plugin/test/test_ebpf_plugin.c:684-729`, `1294-1323`).

Important caveat:
- The artifact demonstrates a **real load-time verifier path**.
- It does **not** make the "PREVAIL" branding explicit in the plugin source itself; it shows a bpftime verifier path. If the paper wants to say PREVAIL specifically, it should cite the actual verifier dependency and cleanly explain that stack.

Artifact consistency issue:
- `docs/tmp/bpftime-migration-results.md` still says `adaptive_channels` and `slo_enforcer` only loaded under `warning` mode (`docs/tmp/bpftime-migration-results.md:81-96`, `141-148`).
- Newer artifacts say both are accepted in `strict` mode (`docs/tmp/benchmark-results.md:19-32`, `docs/tmp/phase3-safety-results.md:16-29`).

Assessment:
- The verifier claim is real.
- The artifact package needs cleanup so reviewers do not see contradictory verifier stories.

### B6. "MPK hardware isolation"

Status: **Unsupported**

What is missing:
- No MPK/pkey/WRPKRU/protection-key code appears in `plugin.cpp`.
- No MPK measurement or fault-containment test appears in the requested results.
- The safety artifact demonstrates **verifier rejection**, not MPK containment (`docs/tmp/phase3-safety-results.md:37-53`).

Assessment:
- MPK is currently a paper-outline claim, not an artifact-backed claim.

### B7. "Atomic hot-reload"

Status: **Supported for the core mechanism; narrower than the outline wording**

What is real:
- The plugin loads a new immutable policy state off path and swaps it with `std::atomic_exchange_explicit()` (`src/nccl-policy-plugin/plugin.cpp:930-968`).
- The hot-reload test runs 400,000 active `getCollInfo()` calls while reloading from `noop` to `size_aware_v2` and then validates a single clean decision boundary plus preservation on bad replacement (`src/nccl-policy-plugin/test/test_ebpf_plugin.c:964-1180`).
- The results show sub-microsecond swap time and zero call loss (`docs/tmp/revise2-results.md:45-74`, `docs/tmp/phase3-safety-results.md:55-72`).

What is not there:
- No `inotify` watcher or automatic file watching appears in the actual plugin.
- The demonstration is CPU-only at the plugin API level, not a real NCCL live-reload run.

Assessment:
- The core "atomic swap of a running policy" claim is good.
- The stronger "production mixed-plugin hot reload with inotify" framing is not backed.

### B8. "EIM capability model"

Status: **Unsupported**

What the code actually does:
- It registers helper groups globally (`src/nccl-policy-plugin/plugin.cpp:238-259`).
- It does not define per-policy capability declarations, capability manifests, or per-policy enforcement.
- `set_non_kernel_helpers({})` is not an EIM policy model (`src/nccl-policy-plugin/plugin.cpp:578-580`).

Assessment:
- There is no artifact evidence for EIM beyond the paper positioning language.

## C. Evaluation Claims

### C9. "per-collective overhead < 200ns"

Status: **Supported for CPU-side `getCollInfo()` policy cost**

Evidence:
- `benchmark-results.md` reports:
  - `noop`: P50 43ns / P99 51ns
  - `size_aware_v2`: P50 44ns / P99 53ns
  - `adaptive_channels`: P50 66ns / P99 76ns
  - `slo_enforcer`: P50 81ns / P99 93ns
  (`docs/tmp/benchmark-results.md:19-27`)
- `revise2-results.md` shows the same order of magnitude (`docs/tmp/revise2-results.md:18-43`).

Important caveat:
- This supports a **CPU hook cost** claim, not a broader end-to-end collective cost claim.
- That is still a legitimate and relevant metric because `getCollInfo()` is CPU-side.

Assessment:
- The "< 200ns" claim is one of the strongest parts of the artifact.

### C10. "`getCollInfo()` exercised in real NCCL"

Status: **Supported for invocation; partially supported for actual decision application**

What is clearly supported:
- The Phase 4 run forms a real `nranks=2` communicator (`docs/tmp/phase4-results.md:83-92`).
- The successful log shows non-zero plugin calls:
  - `finalize calls=495` on each rank (`docs/tmp/phase4-step2-hostid-socket-mpi-device0-fixed.log:151-155`)
- The first five calls show non-trivial policy outputs, including a change from `channels=2` to `channels=4` as message size grows (`docs/tmp/phase4-step2-hostid-socket-mpi-device0-fixed.log:109-138`).

What is still weaker than the outline implies:
- The setup is very artificial: two MPI ranks on one physical GPU, socket transport forced, `NCCL_TESTS_DEVICE=0`, and per-rank `NCCL_HOSTID` overrides (`docs/tmp/phase4-results.md:5-12`, `46-77`).
- The plugin does not hard-disable all unchosen algo/proto pairs; it zeroes the preferred cost-table entry if valid (`src/nccl-policy-plugin/plugin.cpp:772-777`).
- The logs prove that the plugin ran and returned actions. They do **not** fully prove that NCCL's final chosen algo/proto/channel exactly matched those requested actions on every call.

Answer to the specific sub-questions:
- Was `getCollInfo()` exercised in real NCCL? **Yes.**
- How many calls? **495 per rank** in the successful Phase 4 run.
- Real decisions? **Real policy actions were emitted; end-to-end proof of final NCCL adoption is still incomplete.**

### C11. "Overhead breakdown"

Status: **Partially supported**

What is good:
- `revise2-results.md` gives a clean enough staircase:
  - native -> noop: +41ns
  - noop -> size_aware_v2: +1ns
  - size_aware_v2 -> lookup_only: +11ns
  - lookup_only -> lookup_update: +11ns
  - lookup_update -> adaptive_channels: +1ns
  - adaptive_channels -> slo_enforcer: +5ns
  (`docs/tmp/revise2-results.md:28-43`)
- The scaffolding policies (`lookup_only`, `lookup_update`) make the story explainable.

What is missing:
- This is not a direct timing decomposition of "bpftime dispatch vs JIT vs map lookup vs MPK".
- There is no MPK stage in the actual implementation or results.

Assessment:
- Good enough for a workshop staircase figure.
- Not strong enough to support the more ambitious decomposition language in the outline.

### C12. "Adaptive policy behavior"

Status: **Partially supported; synthetic only**

What is demonstrated:
- The harness shows the adaptive policy increasing channels during a low-latency synthetic phase and stepping down to 1 in a high-latency synthetic phase (`docs/tmp/revise2-results.md:75-198`).

What is not demonstrated:
- No real NCCL profiler telemetry is feeding this loop.
- The test relies on seeded `telemetry_map` state and the synthetic telemetry debug hook (`src/nccl-policy-plugin/test/test_ebpf_plugin.c:1183-1289`).

Assessment:
- This is a reasonable synthetic adaptation demo.
- It is not a real closed-loop NCCL adaptation result.

### C13. "Multi-tenant competition"

Status: **Unsupported**

Evidence:
- The requested result docs contain no actual multi-tenant competition data, no CDF, and no 2-process contention figure.
- The outline proposes this experiment, but the artifact package has not delivered it yet.

Assessment:
- The multi-tenant/SLO story is still mostly conceptual.

### C14. "Native crash vs eBPF rejection"

Status: **Supported**

Evidence:
- `test_native_crash.c` forks a child, loads a deliberately bad native tuner plugin, and observes `SIGSEGV`; it then checks that the equivalent bad eBPF object is rejected at plugin init (`src/nccl-policy-plugin/test/test_native_crash.c:129-276`).
- The Phase 3 artifact records exactly that result (`docs/tmp/phase3-safety-results.md:37-53`).

Important caveat:
- This is a verifier-vs-native-crash comparison.
- It is **not** an MPK containment demo.

Assessment:
- This is a solid and paper-worthy safety result.

## D. Gap Analysis

Below is the shortest honest path for each unsupported or only partially supported claim.

| Claim / gap | How critical for acceptance? | Minimum work to close honestly | 1-2 days? |
| --- | --- | --- | --- |
| Cross-stack kernel<->userspace policy plane | **Very high** if kept as main novelty | Either cut the kernel/shared-map story entirely, or implement one real kernel-side writer plus a stable shared-map namespace that the tuner reads in a live run | **Cut claim: yes**. **Real demo: no, not comfortably** |
| Mixed plugin with tuner + net + env + profiler | **High** because the abstract/design centers on it | Either narrow the paper to "tuner plugin only", or implement at least tuner + profiler in one `.so` and show NCCL loads both from that library | **Narrow claim: yes**. **Convincing mixed plugin: probably no** |
| Real profiler->tuner closed loop | **High** because it is a claimed architectural contribution | Implement a minimal profiler adapter that writes one real latency metric into a map consumed by the tuner policy in a real NCCL run | **Borderline for a tiny demo; not comfortably for a clean paper result** |
| bpftime "full runtime" wording | **Medium** | Rephrase to "bpftime runtime primitives + custom loader/relocation path" | **Yes** |
| PREVAIL naming + stale verifier docs | **Medium** | Clean the docs so the verifier story is consistent, and explicitly cite the actual verifier dependency stack | **Yes** |
| MPK isolation | **Medium** if claimed, low if removed | Either remove it from the paper, or implement/measure a true protection-key isolation path and containment demo | **Remove claim: yes**. **Real implementation: no** |
| EIM capability model | **Medium** if claimed | Either remove it, or add policy capability declarations plus enforcement beyond generic helper registration | **Remove claim: yes**. **Real implementation: no** |
| Real NCCL proof that requested algo/proto/channel was actually applied | **Medium** | Add explicit instrumentation showing the chosen NCCL algo/proto/channel after `getCollInfo()`, or log/force the table manipulation more aggressively in a debug build | **Yes** |
| Adaptive behavior with real telemetry | **Medium** | Same closure as the profiler gap: real profiler metric into `telemetry_map`, then run `adaptive_channels` in NCCL and plot the change | **Borderline** |
| Multi-tenant competition data | **Medium** for the SLO case-study story | Add even a weak 2-process, 1-GPU contention experiment and label it clearly as preliminary | **Yes for a weak figure; no for a convincing systems evaluation** |
| Hot reload as production feature (`inotify`, real NCCL demo) | **Low to medium** | Keep the current CPU-side hot-reload result and delete the `inotify` language, or add a simple file-watch wrapper if you want the implementation to match the text | **Yes** |

## E. Honest Assessment

### Which venue is this ready for?

If submitted with the **current outline as written**:
- **Not ready** for a top systems conference.
- **Not ready** for a workshop paper that expects the abstract/design claims to be literally true.

If the paper is **reframed to match the artifact** within the next 1-2 days:
- **Reasonable for a focused workshop short paper**.
- **Strongest fit right now is workshop/poster/demo**, not a full conference paper.

My blunt take:
- **Poster/demo:** ready now.
- **Workshop short paper:** yes, but only after aggressive claim reduction and artifact cleanup.
- **Top conference:** no.

### What will a skeptical reviewer object to first?

1. **The artifact does not match the headline claims.**
   - No mixed plugin.
   - No profiler adapter.
   - No net/env adapter.
   - No cross-stack kernel path.
   - No MPK.
   - No EIM.

2. **The strongest dynamic behavior results are synthetic.**
   - Adaptive behavior uses seeded maps and debug telemetry injection.
   - The real NCCL run uses `size_aware_v2`, not the map-backed adaptive loop.

3. **The successful NCCL integration is real but highly constrained.**
   - Two MPI ranks on one physical GPU.
   - Forced socket transport.
   - `NCCL_HOSTID` hack to bypass duplicate-GPU detection.
   - Good artifact engineering, but not persuasive as a systems evaluation by itself.

4. **The "safe extensibility" story is verifier-centric, not isolation-centric.**
   - Verifier rejection is real.
   - MPK/EIM are not.

5. **The artifact package is internally inconsistent.**
   - `bpftime-migration-results.md` still contains an older warning-mode-only verifier story for `adaptive_channels` / `slo_enforcer`.
   - Newer docs say strict mode works.

6. **The hot path currently serializes policy execution with a mutex.**
   - `pluginGetCollInfoImpl()` takes `policy_state->exec_mu` around every `bpftime_prog_exec()` (`src/nccl-policy-plugin/plugin.cpp:859-865`).
   - None of the requested artifacts evaluate multi-threaded scaling under that lock.

### What is the strongest paper you can honestly write with the current artifacts?

Something like:

> "A bpftime-backed eBPF NCCL tuner plugin with load-time verification, atomic hot-reload, sub-100ns hot-path overhead, and initial real-NCCL integration."

That paper would emphasize:
- safe-ish plugin extensibility through verifier-gated eBPF,
- a clean NCCL tuner ABI adaptation,
- sub-100ns `getCollInfo()` policy cost,
- negative-program rejection,
- native crash vs eBPF rejection,
- atomic policy replacement under active calls,
- and a real 2-rank NCCL exercise that uncovered a real integration bug.

That paper should **not** claim:
- cross-stack kernel/userspace policy sharing,
- mixed plugin support,
- profiler-to-tuner closed loop in real NCCL,
- MPK,
- EIM,
- or multi-tenant governance results.

### What 2-3 improvements would most strengthen the submission?

1. **Implement one real profiler->tuner feedback path.**
   - This is the highest-value systems closure.
   - Even one simple profiler metric written into `telemetry_map` and consumed by `adaptive_channels` in a real NCCL run would dramatically improve the paper.

2. **Either implement or delete the mixed-plugin/cross-stack story.**
   - The fastest honest path is to delete it.
   - The stronger but harder path is to add at least one more real adapter and show same-`.so` map sharing.

3. **Add one more convincing real NCCL experiment.**
   - Best case: a real 2-GPU or 2-node run.
   - Minimum acceptable fallback: explicit proof that NCCL actually adopted the requested algo/proto/channel plus a preliminary contention experiment labeled as limited.

## Recommended Submission Strategy

If the deadline is close, I would **not** spend the next 1-2 days trying to brute-force all missing subsystems. I would do this instead:

1. Rewrite the paper around the artifact that exists.
2. Remove or sharply downgrade claims about cross-stack execution, mixed plugins, MPK, and EIM.
3. Keep the strongest evidence:
   - verifier matrix,
   - native crash vs rejection,
   - sub-100ns CPU overhead,
   - hot-reload result,
   - and the real 2-rank NCCL `getCollInfo()` evidence.
4. If there is time for one engineering push, make it the **real profiler->tuner loop**, not MPK or EIM.

That path gives you a coherent workshop paper. The current outline, as written, does not.
