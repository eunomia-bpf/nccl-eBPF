#include "native_baseline.h"

#include "../ebpf-policies/policy_action.h"

namespace {

uint8_t pick_algo(const nccl_policy_ctx *ctx)
{
  if (ctx->coll_type == 4 && ctx->n_bytes <= (1ULL << 15))
    return NCCL_POLICY_ALGO_TREE;
  return NCCL_POLICY_ALGO_RING;
}

uint8_t pick_proto(const nccl_policy_ctx *ctx)
{
  if (ctx->n_bytes < 4096)
    return NCCL_POLICY_PROTO_SIMPLE;
  if (ctx->n_bytes < (1ULL << 20))
    return NCCL_POLICY_PROTO_LL;
  return NCCL_POLICY_PROTO_SIMPLE;
}

uint8_t pick_channels(const nccl_policy_ctx *ctx)
{
  if (ctx->n_bytes < 4096)
    return 2;
  if (ctx->n_bytes < (1ULL << 20))
    return 4;
  if (ctx->coll_type == 4 && ctx->n_ranks >= 8)
    return 10;
  return 8;
}

}  // namespace

extern "C" uint64_t nccl_native_size_aware_v2(const struct nccl_policy_ctx *ctx)
{
  if (!ctx)
    return 0;

  return nccl_policy_pack_action(
      pick_algo(ctx), pick_proto(ctx), pick_channels(ctx), 2,
      NCCL_POLICY_ACTION_SET_ALGO | NCCL_POLICY_ACTION_SET_PROTO |
          NCCL_POLICY_ACTION_SET_CHANNELS |
          NCCL_POLICY_ACTION_SET_AGGRESSIVENESS);
}
