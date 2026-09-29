/* TP4 Spark experiment: compare SIMPLE against LL for measured mid-size
 * AllReduce calls. Other collectives, sizes, and topologies use NCCL default. */
#include "bpf_compat.h"
#include "policy_action.h"
#include "policy_context.h"

SEC("uprobe")
uint64_t spark_tp4_simple_64k_1m_policy(struct nccl_policy_ctx *ctx) {
  if (!ctx || ctx->n_ranks != 4 || ctx->n_nodes != 4 ||
      ctx->coll_type != NCCL_POLICY_COLL_ALLREDUCE ||
      ctx->n_bytes <= (64ULL << 10) || ctx->n_bytes > (1ULL << 20))
    return 0;

  return nccl_policy_pack_action(
      NCCL_POLICY_ALGO_RING, NCCL_POLICY_PROTO_SIMPLE, 0, 0,
      NCCL_POLICY_ACTION_SET_ALGO | NCCL_POLICY_ACTION_SET_PROTO);
}

char LICENSE[] SEC("license") = "GPL";
