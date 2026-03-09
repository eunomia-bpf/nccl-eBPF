#include "bpf_compat.h"
#include "policy_context.h"

SEC("uprobe")
uint64_t bad_oob_access_policy(struct nccl_policy_ctx *ctx)
{
  volatile const uint64_t *slots = (volatile const uint64_t *)ctx;

  if (!ctx)
    return 0;

  return slots[32];
}

char LICENSE[] SEC("license") = "GPL";
