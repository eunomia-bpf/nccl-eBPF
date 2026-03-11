/* bad_channels.bpf.c -- Deliberately bad policy for degradation demo.
 *
 * Forces n_channels=1 for all collectives. This is verifier-accepted
 * (memory-safe) but produces severe throughput degradation on NVLink.
 */
#include "bpf_compat.h"
#include "policy_action.h"
#include "policy_context.h"

SEC("uprobe")
uint64_t bad_channels_policy(struct nccl_policy_ctx *ctx) {
  if (!ctx)
    return 0;

  return nccl_policy_pack_action(
      0, 0, 1, 0, NCCL_POLICY_ACTION_SET_CHANNELS);
}

char LICENSE[] SEC("license") = "GPL";
