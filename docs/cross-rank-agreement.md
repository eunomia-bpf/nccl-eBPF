# Cross-rank agreement for NCCLbpf policies

Design notes for two mechanisms that let eBPF policies use run-time measurements without making ranks disagree:

- a **cross-rank check**: a load-time information-flow check on policy bytecode, run after the ordinary eBPF verifier;
- an **agreed (distributed) map**: a map whose value is the same on every rank of a communicator at each call, built from per-rank measurements by a verified, deterministic merge.

The load-time checker and an experimental agreed-map example are implemented in `src/nccl-policy-plugin/`. The example supports 1-8 ranks, one rank per process, and uses a verified `dist_merge` BPF program over rank-local latency slots. Ranks on one host use Unix sockets; an explicit rank-zero TCP endpoint enables a multi-host path. The Unix and TCP paths have passed local CPU tests, not a multi-host Spark, GPU, or LLM benchmark. Sections 5 and 6 describe NCCL 2.29.7 paths that still need treatment before general cross-rank safety can be claimed.

**Current safety boundary:** participating distributed-policy ranks compare the BPF object and plugin-runtime hash before their first BPF action. Ordinary policies can still be reloaded independently per process. Host handling of channel bounds and algorithm/protocol availability can differ after identical BPF actions. Some NCCL paths do not advance `getCollInfo` counts uniformly. TCP prepare/ACK/COMMIT cannot make COMMIT delivery atomic: a failure partway through it can leave some ranks with a committed snapshot while others fail. Treat the distributed path as an opt-in prototype, not a production-wide agreement guarantee.

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

## 3. Cross-rank check (prototype)

`rank_agreement_verifier.h` runs after the ordinary verifier for every non-profiler program (`plugin.cpp`, `load_program_from_object`). It is a forward abstract interpretation over the relocated bytecode:

- every register and stack byte carries a label (`shared`, `local`, `context`, `agreed_map`, `agreed_value`, `local_map`, `local_value`, ...);
- context loads are shared for `n_bytes`, `coll_type`, `num_pipe_ops`, `n_ranks`, `n_nodes`, and the tested NVL-domain fields, which pinned NCCL derives from communicator-wide values (`shared_context_access`); `call_count`, `reg_buff`, `current_channels`, and latency fields are local;
- `map_lookup_elem` on the host-published `agreed_map` yields an agreed value; any other lookup, and every other helper, yields a local value;
- a conditional branch on a local value is rejected unless every reachable action is provably zero; both data flow and control flow are covered;
- the action is the return value; a local `r0` at `exit` is rejected ("rank-local action");
- writes to the agreed map are rejected; subprogram calls are rejected as unanalysed.

It is deliberately conservative: local data may be computed, but it cannot affect a nonzero action. Possible relaxations, each needing an argument:

- `current_channels` (the previous decision) is shared by induction if every earlier decision passed the check.
- A per-communicator call count could be shared on the main path only; see Section 5.2.

Open items for the check:

- **Host post-processing.** The host clamps outputs and falls back when a choice is unavailable (paper §4). If availability differs per rank (for example through `NCCL_ALGO`), agreement breaks after the policy returns. Either make these host steps depend only on shared values or include the relevant environment in the init-time profile (Section 4.1).
- **Channel bound.** NCCL does not clamp the tuner's channel count; CollNet and allgatherv use it directly (`enqueue.cc:621-626`, `scheduler/allgatherv_sched.cc:65-80`). The host should enforce `nChannels <= comm->nChannels`. The plugin cannot see that value, so it belongs in the profile.
- **Scope of the check.** Only tuner programs are checked today. Net plugin programs need the per-connection scope from Section 2.

## 4. Agreed (distributed) map (design and experimental transport)

The current implementation fixes three u64 array maps (`local_latency`, `rank_slots`, `agreed_map`), a verified `SEC("dist_merge")` max operation, an exchange every 1024 tuner calls, and activation on the next 1024-call boundary. It requires `NCCL_POLICY_EXPERIMENTAL_DIST_MAP=1`, supports 1-8 ranks with one rank per process, and rejects distributed-policy hot reload. Without `NCCL_POLICY_DIST_COORDINATOR`, same-host ranks use a Unix socket and shared `/tmp`. For multiple hosts, every rank must use the same explicit rank-zero `host-or-IPv4:port` endpoint; rank zero binds only that local interface, and peers connect over TCP. The more general semantics below remain design goals.

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
- **Bound every wait inside `getCollInfo`.** The round-0 version handshake happens before the first BPF action and may wait up to the exchange deadline (30 seconds by default). Later exchanges start asynchronously at calls 1024, 2048, and so on; an activation call waits for its pending result without holding the distributed-state lock. The first-call wait is still a liveness risk if one NCCL thread drives several ranks sequentially; the tested setup uses one rank per process. `getCollInfo` can run on the user or a group thread, and `ncclCommAbort` joins that thread (`group.cc:644-648, 797-822, 873-878`; `init.cc:2713`).
- If the snapshot for the activation call has not arrived: wait with a bound and return an error from `getCollInfo` (fail closed). Never fall back to a local value, because a local fallback is itself a disagreement.
- Before the first merged epoch, the zero-initialized `agreed_map` is read only after round-0 version agreement. This is the tested example's initial value, not a generic map API contract.
- The TCP path exchanges prepare proposals, receives ACKs, then sends COMMIT. A lost connection during COMMIT can reach only a subset of peers. Some ranks may commit while others fail; an atomic cross-rank outcome still needs a stronger protocol or NCCL-level integration.

### 4.4 Rendezvous

The tuner v5 init receives `commId`, rank and node counts, a logger, NVL-domain info and constants, but no rank id, bootstrap handle or communicator pointer (`tuner_v5.h:52-53`). Exchanging slots therefore needs a channel outside NCCL's plugin contract. The rank id comes from the profiler v6 plugin, whose init receives it; missing or conflicting rank metadata is rejected before the first tuner action. The same-host path uses a Unix socket keyed by communicator hash and round. The TCP path uses `NCCL_POLICY_DIST_COORDINATOR=host-or-IPv4:port` on every rank; rank zero binds the specified local interface, so the address must be reachable by every peer. This is an explicit endpoint, not a discovered NCCL bootstrap address or a standalone service. The current state and rendezvous still use the communicator hash, which can collide for overlapping communicators; a future implementation needs a per-communicator instance key. All local ranks of one communicator share a commHash, and repeated `ncclCommShrink` of the same parent can produce the same hash (`init.cc:2808-2815, 2849`).

### 4.5 Policy versions and hot reload

Ordinary-policy reload remains a per-process pointer swap (`pluginReloadPolicyImpl`); distributed-policy reload is rejected until cross-rank coordination exists. For the initial distributed policy, round 0 exchanges a version derived from the BPF object bytes and the loaded plugin runtime bytes before any BPF action. A mismatch rejects the action. This checks participating ranks that loaded a distributed policy; it does not force an ordinary policy or a rank with no plugin into the same handshake. Paper T3 argues that briefly running different policies is fine because calls are independent; across ranks that is false. A future distributed hot reload would require every rank to load, verify and JIT the new program, report success, and switch at an agreed call. A rank whose verification fails must abort the switch for everyone.

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
- `NCCL_ALGO`, `NCCL_PROTO`: read per process; put relevant availability in the init-time profile and require it to match. The distributed-policy round-0 exchange compares policy/runtime bytes, not the `NCCL_POLICY_BPF_PATH` string; ordinary policies have no cross-rank version exchange.
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
3. Extend the local CPU checks (2 and 8 Unix-socket ranks, 4 TCP loopback ranks) to real multi-host Spark/NCCL runs. Measure epoch latency, activation slack, and hot-path cost; compare policy decisions and data-check results on every rank.

## 8. Prior art

- **MCCS** (SIGCOMM'24): a central manager; daemons exchange the last launched collective number, run to the maximum, then switch. Activation at an agreed collective number, used for occasional reconfiguration.
- **AutoCCL** (NSDI'25; code at github.com/gbxu/autoccl): rank 0 is the leader and decides from its own timings; it broadcasts versioned schedules of (config, number of calls) over TCP on NCCL's bootstrap addresses, using a modified NCCL 2.18.3; every rank counts calls per key {commHash, collType, nBytes} and blocks if the next round has not arrived.
- **Horovod autotune**: rank 0 tunes "to ensure consistency" and broadcasts parameters with MPI at a common loop cycle.
- **Titanium `single`** (POPL'98): a type qualifier for values identical on all processes; branches around barriers and broadcasts may depend only on them.
- **PARCOACH**: taint from rank-derived values on branch conditions that control collectives; does not check collective arguments.
- **MUST**: run-time comparison of collective arguments.
- **GoLiSA** (ECOOP'23): information flow from nondeterministic sources to consensus-relevant sinks only.

The prototype demonstrates agreement semantics as an eBPF map read by the verified tuner example, with the merged value computed by the same verified deterministic program on every rank rather than by a leader, on unmodified NCCL; and a load-time check on eBPF bytecode, scoped by output, with map-level labels (agreed vs. local). General map APIs and full NCCL call-path coverage remain future work.

## 9. Questions to expect

- **Isn't this MCCS or AutoCCL?** They agree by a leader or manager deciding. Here rank 0 transports measurements, but each rank runs the same verified merge over the exchanged slots; the experimental tuner example reads the resulting agreed map.
- **Does a straggler or dead rank block everyone?** The initial version handshake and later activation boundaries can wait for peers, each with a bounded exchange deadline. An epoch starts asynchronously one 1024-call interval before activation. The current one-rank-per-process CPU tests do not prove that this scheduling is safe for every NCCL thread path.
- **Scale and staleness?** The current TCP prototype sends all slots through rank 0 and activates one 1024-call interval after sealing. A hierarchical tree reduction and local-scope fast reactions are design options, not measured or implemented in this example.
- **Is the taint analysis sound?** It follows data and control flow on the bytecode, rejects local values that can affect a nonzero collective action, and treats ordinary map and nondeterministic helper results as local. Maps written by several programs need all of them analysed together, re-run on reload.
- **What about CUDA graphs?** The current call-count epoch scheme does not account for captured replays that skip `getCollInfo`; pinning a captured graph to its original policy is still a design requirement.
