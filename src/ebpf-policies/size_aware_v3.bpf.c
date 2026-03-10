/* size_aware_v3 — corrected size-aware AllReduce tuning policy
 *
 * Hardware: RTX 5090, socket transport, 2 ranks
 * Derived from systematic (algo, proto) sweep 2026-03-09
 * (docs/tmp/p2-default-vs-optimal-sweep.md)
 *
 * Key corrections over size_aware_v2:
 *   1. Small messages (≤32KB): TREE+SIMPLE (was TREE+LL for 4KB-32KB range)
 *      Sweep shows Tree/Simple beats Ring/LL by ~2.4% (~100 µs) at these sizes.
 *   2. All messages >32KB: RING+SIMPLE (was RING+LL up to 1MB in v2)
 *      LL collapses catastrophically at 512KB (2×) and degrades to 44× at 128MB.
 *      NCCL default switches to Simple at 128KB; this policy applies Simple everywhere
 *      above 32KB. Measured difference between LL and Simple below 256KB is <0.1%.
 *   3. Channel count: capped at 4 for socket transport (NCCL initializes max 4 channels).
 */
#include "bpf_compat.h"
#include "policy_action.h"
#include "policy_context.h"

/* Thresholds derived from 2026-03-09 sweep (p2-default-vs-optimal-sweep.md):
 *   ≤32KB:    Tree/Simple — ~2.4% faster than NCCL default (Ring/LL)
 *   >32KB:    Ring/Simple — at parity or better vs NCCL default; guards LL collapse
 */
#define SMALL_MSG_THRESHOLD (32ULL * 1024)   /* 32 KB */

static uint8_t pick_algo(const struct nccl_policy_ctx *ctx) {
  /* Tree is slightly faster for AllReduce at small sizes (different NCCL kernel path).
   * Ring is optimal for larger messages. */
  if (ctx->coll_type == NCCL_POLICY_COLL_ALLREDUCE &&
      ctx->n_bytes <= SMALL_MSG_THRESHOLD)
    return NCCL_POLICY_ALGO_TREE;
  return NCCL_POLICY_ALGO_RING;
}

static uint8_t pick_proto(const struct nccl_policy_ctx *ctx) {
  /* Always use Simple protocol.
   * LL is safe only below ~256KB but provides no measurable latency benefit
   * over Simple in this socket-transport configuration. LL collapses at ≥512KB.
   * Using Simple everywhere eliminates the LL collapse risk with zero performance cost. */
  return NCCL_POLICY_PROTO_SIMPLE;
}

static uint8_t pick_channels(const struct nccl_policy_ctx *ctx) {
  /* Socket transport with 2 ranks initializes exactly 4 channels.
   * Use 2 channels for tiny messages (≤4KB) — matches v2 behavior.
   * Use 4 channels for everything else (socket transport max). */
  if (ctx->n_bytes <= 4096)
    return 2;
  return 4;
}

SEC("uprobe")
uint64_t size_aware_v3_policy(struct nccl_policy_ctx *ctx) {
  if (!ctx)
    return 0;

  return nccl_policy_pack_action(
      pick_algo(ctx), pick_proto(ctx), pick_channels(ctx), 2,
      NCCL_POLICY_ACTION_SET_ALGO | NCCL_POLICY_ACTION_SET_PROTO |
          NCCL_POLICY_ACTION_SET_CHANNELS |
          NCCL_POLICY_ACTION_SET_AGGRESSIVENESS);
}

char LICENSE[] SEC("license") = "GPL";
