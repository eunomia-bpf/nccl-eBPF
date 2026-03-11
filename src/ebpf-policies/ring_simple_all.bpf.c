/* ring_simple_all — force Ring/Simple at ALL message sizes
 *
 * This is the "oracle-matching" policy: empirically Ring/Simple is optimal
 * on 3-rank socket transport at all sizes (8.7ms flat vs NCCL's Ring/LL
 * which causes 17.4ms at 4KB-128KB).
 *
 * n_channels=0: let NCCL decide (avoids the nChannels=1 degradation bug)
 */
#include "bpf_compat.h"
#include "policy_action.h"
#include "policy_context.h"

SEC("uprobe")
uint64_t ring_simple_all_policy(struct nccl_policy_ctx *ctx)
{
  if (!ctx)
    return 0;

  return nccl_policy_pack_action(
      NCCL_POLICY_ALGO_RING, NCCL_POLICY_PROTO_SIMPLE, 0, 2,
      NCCL_POLICY_ACTION_SET_ALGO | NCCL_POLICY_ACTION_SET_PROTO |
          NCCL_POLICY_ACTION_SET_AGGRESSIVENESS);
}

char LICENSE[] SEC("license") = "GPL";
