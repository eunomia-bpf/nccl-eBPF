#include "bpf_compat.h"
#include "policy_action.h"
#include "policy_context.h"

static uint8_t pick_algo(const struct nccl_policy_ctx *ctx) {
  if (ctx->coll_type == NCCL_POLICY_COLL_ALLREDUCE &&
      ctx->n_bytes <= (1ULL << 15))
    return NCCL_POLICY_ALGO_TREE;
  return NCCL_POLICY_ALGO_RING;
}

static uint8_t pick_proto(const struct nccl_policy_ctx *ctx) {
  if (ctx->n_bytes < 4096)
    return NCCL_POLICY_PROTO_SIMPLE;
  if (ctx->n_bytes < (1ULL << 20))
    return NCCL_POLICY_PROTO_LL;
  return NCCL_POLICY_PROTO_SIMPLE;
}

static uint8_t pick_channels(const struct nccl_policy_ctx *ctx) {
  if (ctx->n_bytes < 4096)
    return 2;
  if (ctx->n_bytes < (1ULL << 20))
    return 4;
  if (ctx->coll_type == NCCL_POLICY_COLL_ALLREDUCE && ctx->n_ranks >= 8)
    return 10;
  return 8;
}

SEC("uprobe")
uint64_t size_aware_v2_policy(struct nccl_policy_ctx *ctx) {
  if (!ctx)
    return 0;

  return nccl_policy_pack_action(
      pick_algo(ctx), pick_proto(ctx), pick_channels(ctx), 2,
      NCCL_POLICY_ACTION_SET_ALGO | NCCL_POLICY_ACTION_SET_PROTO |
          NCCL_POLICY_ACTION_SET_CHANNELS |
          NCCL_POLICY_ACTION_SET_AGGRESSIVENESS);
}

char LICENSE[] SEC("license") = "GPL";
