# Cross-rank agreement for NCCLbpf policies

Design notes for two mechanisms that let eBPF policies use run-time measurements without making ranks disagree:

- a **cross-rank check**: a load-time information-flow check on policy bytecode, run after the ordinary eBPF verifier;
- an **agreed (distributed) map**: a map whose value is the same on every rank of a communicator at each call, built from per-rank measurements by a verified, deterministic merge.

The first is partly implemented (`src/nccl-policy-plugin/rank_agreement_verifier.h`, commit 4e74e1a). The second is a design. Sections 5 and 6 list what a review against the NCCL 2.29.7 source found, and the fixes the design needs before it is implemented.

NCCL line references are to NCCL 2.29.7 (`nccl` submodule, `src/`). Plugin references are to `src/nccl-policy-plugin/plugin.cpp` at the time of writing.

## 1. Problem

Every rank of a communicator must pick the same algorithm and protocol, and the same channel count, for the same collective call. Each algorithm fixes who sends what to whom; if rank 0 runs Ring and rank 1 runs Tree, each waits for data that never comes. The channel count decides how each rank splits the buffer across channels (`enqueue.cc:579-705`), so different counts make peers split the same buffer differently.

NCCL itself keeps ranks in step by aligning inputs once, when the communicator is created, and then running a deterministic function per call:

- init AllGather of compute capabilities, min/max (`init.cc:998`);
- init AllGather of per-algorithm graph info: `nChannels`, `bwIntra`, `bwInter` take the minimum, link types and `crossNic` the maximum ("Make sure we align all ranks so that the tuning is consistent across ranks", `init.cc:1260`); NVLS is disabled for the whole communicator if any rank lacks it (`init.cc:1273`);
- the cost model is built once (`ncclTopoTuneModel`, `graph/tuning.cc:231`) and never updated afterwards.

The tuner plugin API (`include/plugin/tuner/tuner_v5.h`) never states this contract. Each rank loads its own copy of the plugin and its tuner decides alone. A tuner that reads anything rank-local (its own measured latency, timers, a map written by its own profiler) can make ranks disagree. Two gaps already exist in NCCL: `NCCL_ALGO`/`NCCL_PROTO` are read per process (`tuning.cc:420-497`) and `regBuff` reflects local buffer registration (`register.cc`), and neither is compared across ranks.

An internal composability run (`docs/tmp/composability-experiment.md`, 2 ranks on one GPU) logged the two ranks choosing 10 and 9 channels on the same call. The run did not hang, but NCCL does not clamp the tuner's value (`enqueue.cc:2085`) unless it exceeds `comm->nChannels`, so the likely outcome is silent step desync or wrong data rather than safety. This needs a rerun (Section 7).

## 2. Agreement scopes and input classes

Not every output needs global agreement. Outputs are scoped by who must agree:

| Output | Must agree on | May depend on |
| --- | --- | --- |
| tuner: algorithm, protocol, channels | all ranks of the communicator | shared inputs only |
| net plugin: transport or device for one connection | both ends of that connection | shared inputs, plus what its connection handshake carries |
| profiler writes; net plugin local pacing, rail choice on this node | no one | anything |

Local tuning on local data stays allowed. Only outputs that NCCL uses in a collective-wide way are restricted.

Inputs fall into two classes:

- **Shared**: `getCollInfo` arguments that NCCL guarantees are identical (collective type, byte count, `numPipeOps`, rank and node counts), values aligned at communicator init, and reads from an agreed map.
- **Rank-local**: measured latency, timers and other helpers, ordinary maps, `regBuff`, per-process environment variables.

## 3. Cross-rank check (implemented)

`rank_agreement_verifier.h` runs after the ordinary verifier for every non-profiler program (`plugin.cpp`, `load_program_from_object`). It is a forward abstract interpretation over the relocated bytecode:

- every register and stack byte carries a label (`shared`, `local`, `context`, `agreed_map`, `agreed_value`, `local_map`, `local_value`, ...);
- context loads are shared only for `n_bytes`, `coll_type`, `num_pipe_ops`, `n_ranks`, `n_nodes` (`shared_context_access`); `call_count`, `reg_buff`, `current_channels`, latency fields and the NVL-domain fields are local;
- `map_lookup_elem` on the host-published `agreed_map` yields an agreed value; any other lookup, and every other helper, yields a local value;
- a conditional branch on a local value is rejected ("rank-local control flow"), so both data flow and control flow are covered, including an early `return` that leaves NCCL's default in place;
- the action is the return value; a local `r0` at `exit` is rejected ("rank-local action");
- writes to the agreed map are rejected; subprogram calls are rejected as unanalysed.

It is deliberately conservative: a local value may be computed but may not select a path or reach the action. Possible relaxations, each needing an argument:

- NVL-domain fields come from `nvlDomainInfo`, which NCCL computes per communicator; they could be shared if NCCL's computation is shown to be identical on all ranks.
- `current_channels` (the previous decision) is shared by induction if every earlier decision passed the check.
- A per-communicator call count could be shared on the main path only; see Section 5.2.

Open items for the check:

- **Host post-processing.** The host clamps outputs and falls back when a choice is unavailable (paper §4). If availability differs per rank (for example through `NCCL_ALGO`), agreement breaks after the policy returns. Either make these host steps depend only on shared values or include the relevant environment in the init-time profile (Section 4.1).
- **Channel bound.** NCCL does not clamp the tuner's channel count; CollNet and allgatherv use it directly (`enqueue.cc:621-626`, `scheduler/allgatherv_sched.cc:65-80`). The host should enforce `nChannels <= comm->nChannels`. The plugin cannot see that value, so it belongs in the profile.
- **Scope of the check.** Only tuner programs are checked today. Net plugin programs need the per-connection scope from Section 2.

## 4. Agreed (distributed) map (design)

### 4.1 Layers

1. **Static differences** (GPU type, NICs, topology, relevant environment): exchanged once at communicator init into a communicator profile that is identical on every rank.
2. **Slow changes** (degraded links, stragglers, shared networks): an agreed map whose value changes at agreed points.
3. **Fast local reactions**: local-scope outputs only, reading local telemetry directly.

### 4.2 Semantics

- **Write side.** The profiler writes this rank's slot. A lookup from a profiler program returns its own slot.
- **Read side.** A lookup from a tuner program returns the agreed snapshot, identical on every rank at each call. The value and the presence of an entry both come from the snapshot.
- **Merge.** A verified eBPF program attached to the map (for example `SEC("agreed_merge")`) runs on every rank over the same vector of slots. It may not call time, random or rank-id helpers. eBPF has no floating point, division by zero is defined, slots are indexed by rank so there is no iteration-order issue, and x86 and Arm JITs agree by the ISA spec. Agreement follows from determinism: same verified merge, same slots, same value. The bpftime runtime version belongs in the agreed program hash to cover JIT bugs.
- **Associative merges** (max, min, sum, median of a fixed-size window) reduce hierarchically: node shared memory first, then a tree across nodes. A general merge needs all slots on every rank, which is N² traffic per epoch in total (about 6.4 GB at 10k ranks with 64 B slots); make it opt-in.
- **One agreed map per communicator.** Slots and epochs belong to one communicator; the map is keyed by communicator, not shared across communicators.

### 4.3 Activation

- An epoch covers K calls. Seal epoch e at call (e+1)·K and activate it at call (e+2)·K, so the exchange has one epoch of slack and stays off the critical path.
- **Never block indefinitely inside `getCollInfo`.** It runs on the user thread (blocking communicators) or on a group thread (non-blocking communicators), and `ncclCommAbort` joins that thread (`group.cc:644-648, 797-822, 873-878`; `init.cc:2713`). One thread may drive several GPUs and prepare each communicator in turn, so waiting for a peer that the same thread serves self-deadlocks. The plugin also holds `telemetry_mu` while running a program (`plugin.cpp` near the `bpftime_prog_exec` call), which the profiler write-back needs.
- If the snapshot for the activation call has not arrived: wait with a bound and without holding locks, then return an error from `getCollInfo` (fail closed). Never fall back to a local value, because a local fallback is itself a disagreement.
- Before the first merged epoch the snapshot is the init-time default, identical on every rank; pick one convention (empty or a version-0 default) and use it everywhere.

### 4.4 Rendezvous

The tuner v5 init receives `commId`, rank and node counts, a logger, NVL-domain info and constants, but no rank id, bootstrap handle or communicator pointer (`tuner_v5.h:52-53`). Exchanging slots therefore needs a channel outside NCCL's plugin contract: a key-value store or a transport addressed by an environment variable plus the communicator hash. The rank id comes from the profiler plugin, whose init receives it. Key per-rank state by (commHash, rank): all local ranks of one communicator share a commHash, and repeated `ncclCommShrink` of the same parent can produce the same hash (`init.cc:2808-2815, 2849`).

### 4.5 Policy versions and hot reload

Today reload is a per-process pointer swap (`pluginReloadPolicyImpl`). Paper T3 argues that briefly running different policies is fine because calls are independent; across ranks that is false. With an agreed map, the policy version becomes an agreed value: every rank loads, verifies and JITs the new program, reports success, and all ranks switch at an agreed call. This is a commit round, not a single map read. A rank whose verification fails aborts the switch for everyone.

### 4.6 Trust

Ranks are trusted, as in NCCL. A buggy slot can skew a `max` merge; prefer robust merges (median) where that matters. A rank that sends different values to different peers (Byzantine) is out of scope.

## 5. Where NCCL breaks the assumptions

### 5.1 What must agree

Algorithm and protocol select the kernel, the collnet/NVLS binning and buffers (`enqueue.cc:437-458, 2126-2130`); a new algorithm also triggers a transport connect inside `ncclGroupEnd` (`enqueue.cc:490-499`, `group.cc:148-178`). Channel count sets the per-channel partition (`enqueue.cc:579-705`). All three must agree. The paper (§3.4) states only the algorithm; the check covers all three.

### 5.2 The call count is not a reliable clock

- Main path: one `getCollInfo` call per aggregated (fn, op, type) bin in deterministic size order (`enqueue.cc:389-436`). Holds.
- Symmetric-kernel path: an extra call (`scheduler/symmetric_sched.cc:115`) depends on rank-local conditions (`intraRanks`, `NCCL_SYM_*` environment) and on the tuner's own protocol answer.
- allgatherv: called once per plan at launch time (`scheduler/allgatherv_sched.cc:54`).
- `ncclGroupSimulateEnd` runs the tuner locally without launching (`group.cc:118-121, 694`).
- Copy-engine collectives skip the tuner (`enqueue.cc:2919-2921`).
- CUDA graph replay reuses the saved plan and never calls `getCollInfo`, while profiler events still fire on every replay (`enqueue.cc:1390-1412`, `profiler.cc:460-465`).

Fixes: count epochs only on main-path, uncaptured calls; pin captured graphs to the policy in force at capture time; fail closed when the symmetric path is enabled; exclude simulate calls from the count.

### 5.3 Inputs to reclassify

- `regBuff`: local (registration and graph capture).
- `NCCL_ALGO`, `NCCL_PROTO`, `NCCL_POLICY_BPF_PATH`: read per process; put them in the init-time profile and require them to match.
- `comm->nChannels`: needed for the channel bound; put it in the profile.

## 6. Scenarios

- **Run-time drift in homogeneous clusters** (degraded links, stragglers, jobs sharing the network): common at scale; layer 2.
- **Mixed training** (GPU generations in one job, or across datacenters): layer 1 profile plus layer 2 updates.
- **Mixed inference** (a model's layers split across GPU types): per-communicator tables from the profile.

Example of what NCCL does for a mixed job, RTX 5090 (compute capability 12.0, x86) plus two DGX Spark (12.1, Arm), not fully connected:

- mixed compute capability disables LL128 for the communicator (`tuning.cc:493`) and constants come from the Blackwell index (`tuning.cc:250`); mixed CPU architecture only logs (`init.cc:1190-1205`);
- `bwInter` is one minimum for all links (`init.cc:1264`);
- the inter-node ring follows node index order and closes the loop (`graph/connect.cc:95`), so with three nodes every pair is a ring edge; the double binary tree for three nodes is two chains that together use every pair (`graph/trees.cc`);
- each rank picks the first working net plugin (`plugin/net.cc:298`), one per communicator, so an RDMA-only direct cable and a TCP-only path cannot be mixed.

A policy can pick algorithm, protocol and channels from a measured table per communicator profile (layer 1), react to slow changes through the agreed map (layer 2), and choose transport per connection in the net plugin (both-ends scope). Ring order, tree shape and routes are fixed at init by node index; no plugin hook reaches them today.

## 7. Experiments to run

1. Rerun the composability experiment with `NCCL_DEBUG=INFO` (prints channel counts, `init.cc:1450`) and nccl-tests `-c 1`, logging each rank's channel count per call. Establish whether the 10 vs 9 mismatch hung, corrupted data, or was clamped.
2. Run the paper's §5.3 adaptive-channels policy through the checker: it should be rejected with an ordinary map and accepted with an agreed map.
3. Measure the agreed-map exchange at 2, 8 and multi-node scale: epoch latency, activation slack needed, and cost on the hot path (one lookup plus one compare).

## 8. Prior art

- **MCCS** (SIGCOMM'24): a central manager; daemons exchange the last launched collective number, run to the maximum, then switch. Activation at an agreed collective number, used for occasional reconfiguration.
- **AutoCCL** (NSDI'25; code at github.com/gbxu/autoccl): rank 0 is the leader and decides from its own timings; it broadcasts versioned schedules of (config, number of calls) over TCP on NCCL's bootstrap addresses, using a modified NCCL 2.18.3; every rank counts calls per key {commHash, collType, nBytes} and blocks if the next round has not arrived.
- **Horovod autotune**: rank 0 tunes "to ensure consistency" and broadcasts parameters with MPI at a common loop cycle.
- **Titanium `single`** (POPL'98): a type qualifier for values identical on all processes; branches around barriers and broadcasts may depend only on them.
- **PARCOACH**: taint from rank-derived values on branch conditions that control collectives; does not check collective arguments.
- **MUST**: run-time comparison of collective arguments.
- **GoLiSA** (ECOOP'23): information flow from nondeterministic sources to consensus-relevant sinks only.

What is new here: agreement semantics exposed as an eBPF map that any verified policy can read, with the merged value computed by the same verified deterministic program on every rank rather than by a leader, on unmodified NCCL; and a load-time check on eBPF bytecode, scoped by output, with map-level labels (agreed vs. local).

## 9. Questions to expect

- **Isn't this MCCS or AutoCCL?** They agree by a leader or manager deciding. Here every rank runs the same verified merge, any policy reads the result as a map, and the check guarantees the policy reads only agreed values.
- **Does a straggler or dead rank block everyone?** Only at epoch boundaries, with one epoch of slack. A dead rank already hangs a collective; the plugin returns an error after a bounded wait instead of blocking.
- **Scale and staleness?** Associative merges reduce as a tree. Staleness is one to two epochs, which suits slow drift; fast local reactions use local-scope outputs.
- **Is the taint analysis sound?** It follows data and control flow on the bytecode, rejects any branch on a local value and any local action, and treats every helper result as local. Maps written by several programs need all of them analysed together, re-run on reload.
- **What about CUDA graphs?** Captured calls keep the policy in force at capture time; epochs advance only on uncaptured calls.
