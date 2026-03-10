/* size_aware_v4 — corrected size-aware tuning policy (JIT-compatible)
 *
 * Same logic as v3 but avoids JIT crash by:
 *   1. Single function body (no static helper function calls)
 *   2. Only accesses ctx->n_bytes (offset 0) — avoids coll_type field
 *      that triggers bpftime JIT regression
 *   3. Correct channel count: 0 for "use NCCL default" to avoid the
 *      nChannels=1 degradation bug
 *
 * Policy logic (from sweep data p2-default-vs-optimal-sweep.md):
 *   <=32KB:  TREE/SIMPLE, channels=0 (let NCCL decide)
 *   >32KB:   RING/SIMPLE, channels=0 (let NCCL decide)
 */
#include "bpf_compat.h"
#include "policy_action.h"
#include "policy_context.h"

SEC("uprobe")
uint64_t size_aware_v4_policy(struct nccl_policy_ctx *ctx)
{
  if (!ctx)
    return 0;

  uint64_t n_bytes = ctx->n_bytes;

  if (n_bytes <= 32 * 1024)
    return nccl_policy_pack_action(
        NCCL_POLICY_ALGO_TREE, NCCL_POLICY_PROTO_SIMPLE, 0, 2,
        NCCL_POLICY_ACTION_SET_ALGO | NCCL_POLICY_ACTION_SET_PROTO |
            NCCL_POLICY_ACTION_SET_AGGRESSIVENESS);

  return nccl_policy_pack_action(
      NCCL_POLICY_ALGO_RING, NCCL_POLICY_PROTO_SIMPLE, 0, 2,
      NCCL_POLICY_ACTION_SET_ALGO | NCCL_POLICY_ACTION_SET_PROTO |
          NCCL_POLICY_ACTION_SET_AGGRESSIVENESS);
}

char LICENSE[] SEC("license") = "GPL";
