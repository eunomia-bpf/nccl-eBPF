/* nvlink_ring_mid.bpf.c -- NVLink-aware policy for B300 8-GPU
 *
 * Observation: NCCL 2.29 defaults to NVLS on B300 NVLink for all sizes,
 * but Ring is 5-27% faster in the 4M-128M range (measured on 8x B300 SXM6).
 * This policy forces Ring/Simple for that range and lets NCCL use NVLS
 * (its default) for everything else.
 */
#include "bpf_compat.h"
#include "policy_action.h"
#include "policy_context.h"

SEC("uprobe")
uint64_t nvlink_ring_mid_policy(struct nccl_policy_ctx *ctx) {
  if (!ctx)
    return 0;

  /* Force Ring/Simple for 2M-192M messages where Ring outperforms NVLS. */
  if (ctx->n_bytes >= (2ULL << 20) && ctx->n_bytes <= (192ULL << 20)) {
    return nccl_policy_pack_action(
        NCCL_POLICY_ALGO_RING, NCCL_POLICY_PROTO_SIMPLE, 0, 0,
        NCCL_POLICY_ACTION_SET_ALGO | NCCL_POLICY_ACTION_SET_PROTO);
  }

  /* All other sizes: no override, NCCL uses default (NVLS). */
  return 0;
}

char LICENSE[] SEC("license") = "GPL";
