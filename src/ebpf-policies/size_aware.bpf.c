#include "bpf_compat.h"
#include "policy_action.h"
#include "policy_context.h"

SEC("uprobe")
uint64_t size_aware_policy(struct nccl_policy_ctx *ctx)
{
  if (!ctx)
    return 0;

  if (ctx->n_bytes < 4096)
    return nccl_policy_pack_action(
        NCCL_POLICY_ALGO_TREE, NCCL_POLICY_PROTO_SIMPLE, 2, 1,
        NCCL_POLICY_ACTION_SET_ALGO | NCCL_POLICY_ACTION_SET_PROTO |
            NCCL_POLICY_ACTION_SET_CHANNELS |
            NCCL_POLICY_ACTION_SET_AGGRESSIVENESS);
  if (ctx->n_bytes < (1ULL << 20))
    return nccl_policy_pack_action(
        NCCL_POLICY_ALGO_RING, NCCL_POLICY_PROTO_LL, 4, 2,
        NCCL_POLICY_ACTION_SET_ALGO | NCCL_POLICY_ACTION_SET_PROTO |
            NCCL_POLICY_ACTION_SET_CHANNELS |
            NCCL_POLICY_ACTION_SET_AGGRESSIVENESS);
  return nccl_policy_pack_action(
      NCCL_POLICY_ALGO_RING, NCCL_POLICY_PROTO_SIMPLE, 8, 3,
      NCCL_POLICY_ACTION_SET_ALGO | NCCL_POLICY_ACTION_SET_PROTO |
          NCCL_POLICY_ACTION_SET_CHANNELS |
          NCCL_POLICY_ACTION_SET_AGGRESSIVENESS);
}

char LICENSE[] SEC("license") = "GPL";
