/* Size-aware AllReduce policy for small groups contained in one NVL domain. */
#include "bpf_compat.h"
#include "policy_action.h"
#include "policy_context.h"

static inline uint64_t force(uint32_t algo, uint32_t proto) {
  return nccl_policy_pack_action(
      algo, proto, 0, 0,
      NCCL_POLICY_ACTION_SET_ALGO | NCCL_POLICY_ACTION_SET_PROTO);
}

SEC("uprobe")
uint64_t nvl72_size_aware_policy(struct nccl_policy_ctx *ctx) {
  uint64_t domain_rank_bounds;

  if (!ctx || ctx->reserved < NCCL_POLICY_CTX_ABI_V2_SIZE)
    return 0;

  if (ctx->n_nvl_domains != 1 ||
      ctx->coll_type != NCCL_POLICY_COLL_ALLREDUCE)
    return 0;

  /* Load both adjacent bounds as one scalar. bpftime's uprobe verifier models
   * offset 76 as the socket-filter data pointer, even though this userspace
   * policy context contains an ordinary uint32_t there. A single aligned load
   * preserves the ABI while avoiding that inapplicable kernel-context type. */
  __builtin_memcpy(&domain_rank_bounds, &ctx->min_ranks_per_nvl_domain,
                   sizeof(domain_rank_bounds));
  if ((uint32_t)domain_rank_bounds != (uint32_t)(domain_rank_bounds >> 32))
    return 0;

  /* Domain sizes must be uniform and exact. In particular, do not apply the
   * four-rank settings to ranks 1-3 or the eight-rank settings to ranks 5-7. */
  if (ctx->n_ranks == 4 && (uint32_t)domain_rank_bounds == 4) {
    if (ctx->n_bytes >= (4ULL << 20) && ctx->n_bytes <= (32ULL << 20))
      return force(NCCL_POLICY_ALGO_RING, NCCL_POLICY_PROTO_LL128);
    return 0;
  }

  if (ctx->n_ranks == 8 && (uint32_t)domain_rank_bounds == 8) {
    if (ctx->n_bytes >= (4ULL << 20) && ctx->n_bytes <= (32ULL << 20))
      return force(NCCL_POLICY_ALGO_RING, NCCL_POLICY_PROTO_LL128);
    if (ctx->n_bytes >= (64ULL << 20) && ctx->n_bytes <= (192ULL << 20))
      return force(NCCL_POLICY_ALGO_RING, NCCL_POLICY_PROTO_SIMPLE);
  }

  return 0;
}

char LICENSE[] SEC("license") = "GPL";
