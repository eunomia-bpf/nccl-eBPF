/* nvlink_ring_mid_v2.bpf.c -- Refined NVLink-aware policy for B300 8-GPU
 *
 * v1 finding: Ring/Simple only beats NVLS at 64M-128M. At 4M-32M,
 * Ring/Simple is worse than NVLS because NCCL's auto-protocol for Ring
 * would choose LL128, not Simple.
 *
 * v2 strategy:
 *   4M-32M:   Ring/LL128 (matches what NCCL_ALGO=Ring auto-selects)
 *   64M-192M: Ring/Simple (proven +5-10% over NVLS)
 *   else:     no override (NCCL uses NVLS)
 */
#include "bpf_compat.h"
#include "policy_action.h"
#include "policy_context.h"

SEC("uprobe")
uint64_t nvlink_ring_mid_v2_policy(struct nccl_policy_ctx *ctx) {
  if (!ctx)
    return 0;

  /* 4M-32M: Ring with LL128 protocol */
  if (ctx->n_bytes >= (4ULL << 20) && ctx->n_bytes <= (32ULL << 20)) {
    return nccl_policy_pack_action(
        NCCL_POLICY_ALGO_RING, NCCL_POLICY_PROTO_LL128, 0, 0,
        NCCL_POLICY_ACTION_SET_ALGO | NCCL_POLICY_ACTION_SET_PROTO);
  }

  /* 64M-192M: Ring with Simple protocol */
  if (ctx->n_bytes >= (64ULL << 20) && ctx->n_bytes <= (192ULL << 20)) {
    return nccl_policy_pack_action(
        NCCL_POLICY_ALGO_RING, NCCL_POLICY_PROTO_SIMPLE, 0, 0,
        NCCL_POLICY_ACTION_SET_ALGO | NCCL_POLICY_ACTION_SET_PROTO);
  }

  return 0;
}

char LICENSE[] SEC("license") = "GPL";
