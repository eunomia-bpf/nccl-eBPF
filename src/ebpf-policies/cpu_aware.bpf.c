/* cpu_aware.bpf.c -- CPU-contention-aware algorithm selection policy
 *
 * Reads the kernel CPU contention state from a shared BPF array map
 * (populated by the nccl_cpu_observer kernel eBPF program on sched_switch
 * tracepoints) and selects the NCCL algorithm accordingly.
 *
 * Decision rationale (B300 8-GPU NVLink measurements):
 *
 *   Contention type      Ring throughput    NVLS throughput    Best choice
 *   -----------------------------------------------------------------------
 *   None                 319.9 GB/s         212.9 GB/s         Ring
 *   CPU saturation       213.3 GB/s (-33%)  231.2 GB/s (ok)    NVLS
 *   cpuset limited       370.5 GB/s (ok)     46.2 GB/s (-78%)  Ring
 *
 * For medium-large messages (>=4 MB) where the difference is significant:
 *   - CONTENTION_SATURATION  -> NVLS/Simple  (hardware multicast, CPU-free)
 *   - CONTENTION_CPUSET_LIMITED -> Ring/Simple (fewer CPU thread deps)
 *   - CONTENTION_NONE        -> Ring/Simple  (highest baseline throughput)
 *
 * For small messages (<=32 KB), Ring/LL is used regardless of contention
 * for minimal latency.
 *
 * The state_map is an array with a single entry (index 0) of type
 * cpu_contention_state.  At runtime the NCCL policy plugin replaces this
 * map with the kernel-pinned map via:
 *   NCCL_POLICY_KERNEL_MAPS=state_map:/sys/fs/bpf/nccl_cpu_state
 */
#include "bpf_compat.h"
#include "policy_action.h"
#include "policy_context.h"

/* ------------------------------------------------------------------ */
/* Inline the cpu_contention_state struct and constants to avoid       */
/* fragile cross-directory includes from BPF object compilation.      */
/* Keep in sync with src/kernel_observer/nccl_cpu_observer.h.         */
/* ------------------------------------------------------------------ */

#define CONTENTION_NONE           0u
#define CONTENTION_SATURATION     1u
#define CONTENTION_CPUSET_LIMITED 2u
#define CPU_STATE_MAP_IDX         0u

struct cpu_contention_state {
    uint64_t timestamp_ns;
    uint32_t sched_switch_rate;
    uint32_t allowed_cpus;
    uint8_t  contention_type;
    uint8_t  _pad[7];
};

/* ------------------------------------------------------------------ */
/* Shared map: replaced at load time with the kernel-pinned state map */
/* ------------------------------------------------------------------ */

struct {
    __uint(type, BPF_MAP_TYPE_ARRAY);
    __uint(max_entries, 1);
    __type(key, uint32_t);
    __type(value, struct cpu_contention_state);
} state_map SEC(".maps");

/* ------------------------------------------------------------------ */
/* Size thresholds                                                     */
/* ------------------------------------------------------------------ */

#define SMALL_MSG_THRESH  (32ULL * 1024)        /* 32 KB */
#define LARGE_MSG_THRESH  (4ULL * 1024 * 1024)  /*  4 MB */

/* ------------------------------------------------------------------ */
/* Policy entry point                                                  */
/* ------------------------------------------------------------------ */

SEC("uprobe")
uint64_t cpu_aware_policy(struct nccl_policy_ctx *ctx)
{
    if (!ctx)
        return 0;

    /* Read CPU contention state from kernel-observer shared map */
    uint32_t key = CPU_STATE_MAP_IDX;
    struct cpu_contention_state *state = bpf_map_lookup_elem(&state_map, &key);

    uint8_t contention = CONTENTION_NONE;
    if (state)
        contention = state->contention_type;

    uint64_t n_bytes = ctx->n_bytes;

    /* Small messages: always Ring/LL for lowest latency, regardless of
     * contention (contention effects are negligible at small sizes). */
    if (n_bytes <= SMALL_MSG_THRESH)
        return nccl_policy_pack_action(
            NCCL_POLICY_ALGO_RING, NCCL_POLICY_PROTO_LL, 0, 2,
            NCCL_POLICY_ACTION_SET_ALGO | NCCL_POLICY_ACTION_SET_PROTO |
                NCCL_POLICY_ACTION_SET_AGGRESSIVENESS);

    /* CPU saturation detected: switch to NVLS (hardware multicast path,
     * independent of CPU scheduling). Only for >=4 MB where the Ring
     * degradation (-33%) makes the switch worthwhile. */
    if (contention == CONTENTION_SATURATION && n_bytes >= LARGE_MSG_THRESH)
        return nccl_policy_pack_action(
            NCCL_POLICY_ALGO_NVLS, NCCL_POLICY_PROTO_SIMPLE, 0, 2,
            NCCL_POLICY_ACTION_SET_ALGO | NCCL_POLICY_ACTION_SET_PROTO |
                NCCL_POLICY_ACTION_SET_AGGRESSIVENESS);

    /* cpuset limited: force Ring (immune to cpuset restrictions,
     * while NVLS collapses -78% under cpuset). */
    if (contention == CONTENTION_CPUSET_LIMITED && n_bytes >= LARGE_MSG_THRESH)
        return nccl_policy_pack_action(
            NCCL_POLICY_ALGO_RING, NCCL_POLICY_PROTO_SIMPLE, 0, 2,
            NCCL_POLICY_ACTION_SET_ALGO | NCCL_POLICY_ACTION_SET_PROTO |
                NCCL_POLICY_ACTION_SET_AGGRESSIVENESS);

    /* No contention or medium messages (32KB < n_bytes < 4MB):
     * Ring/Simple is the proven best on NVLink baseline. */
    return nccl_policy_pack_action(
        NCCL_POLICY_ALGO_RING, NCCL_POLICY_PROTO_SIMPLE, 0, 2,
        NCCL_POLICY_ACTION_SET_ALGO | NCCL_POLICY_ACTION_SET_PROTO |
            NCCL_POLICY_ACTION_SET_AGGRESSIVENESS);
}

char LICENSE[] SEC("license") = "GPL";
