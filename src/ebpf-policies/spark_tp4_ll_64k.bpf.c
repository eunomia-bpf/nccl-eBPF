/* TP4 Spark experiment: Test LL for latency-sensitive small AllReduce.
 * Other collective shapes retain NCCL's own choice. */
#include "bpf_compat.h"
#include "policy_action.h"
#include "policy_context.h"

SEC("uprobe")
uint64_t spark_tp4_ll_64k_policy(struct nccl_policy_ctx *ctx) {
  if (!ctx || ctx->n_ranks != 4 || ctx->n_nodes != 4 ||
      ctx->coll_type != NCCL_POLICY_COLL_ALLREDUCE ||
      ctx->n_bytes == 0 || ctx->n_bytes > (64ULL << 10))
    return 0;

  return nccl_policy_pack_action(
      NCCL_POLICY_ALGO_RING, NCCL_POLICY_PROTO_LL, 0, 0,
      NCCL_POLICY_ACTION_SET_ALGO | NCCL_POLICY_ACTION_SET_PROTO);
}

char LICENSE[] SEC("license") = "GPL";
