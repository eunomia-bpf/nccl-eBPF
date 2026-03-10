/* size_aware_v5 — safe size-aware tuning policy (JIT-compatible, no TREE)
 *
 * Fixes v4 crash: TREE algo causes null-ptr deref in pluginGetCollInfo when
 * NCCL doesn't support TREE for the current topology (2-rank, intra-node,
 * socket transport). coll_cost_table[TREE_ALGO] is NULL in this config.
 *
 * Strategy: use only RING algo (always valid), select proto by message size:
 *   <=32KB:   RING/LL     — LL has lower latency at small sizes
 *   >32KB:    RING/SIMPLE — avoids LL collapse at >=512KB
 *
 * n_channels=0: let NCCL decide (fixed last_channels bug in plugin.cpp)
 */
#include "bpf_compat.h"
#include "policy_action.h"
#include "policy_context.h"

SEC("uprobe")
uint64_t size_aware_v5_policy(struct nccl_policy_ctx *ctx)
{
  if (!ctx)
    return 0;

  uint64_t n_bytes = ctx->n_bytes;

  /* Small messages: LL protocol reduces latency */
  if (n_bytes <= 32 * 1024)
    return nccl_policy_pack_action(
        NCCL_POLICY_ALGO_RING, NCCL_POLICY_PROTO_LL, 0, 2,
        NCCL_POLICY_ACTION_SET_ALGO | NCCL_POLICY_ACTION_SET_PROTO |
            NCCL_POLICY_ACTION_SET_AGGRESSIVENESS);

  /* Large messages: Simple avoids LL bandwidth collapse */
  return nccl_policy_pack_action(
      NCCL_POLICY_ALGO_RING, NCCL_POLICY_PROTO_SIMPLE, 0, 2,
      NCCL_POLICY_ACTION_SET_ALGO | NCCL_POLICY_ACTION_SET_PROTO |
          NCCL_POLICY_ACTION_SET_AGGRESSIVENESS);
}

char LICENSE[] SEC("license") = "GPL";
