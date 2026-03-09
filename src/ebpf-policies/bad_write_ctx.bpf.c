#include "bpf_compat.h"
#include "policy_context.h"

SEC("uprobe")
uint64_t bad_write_ctx_policy(struct nccl_policy_ctx *ctx)
{
  volatile uint64_t *slots = (volatile uint64_t *)ctx;

  if (!ctx)
    return 0;

  slots[32] = 32;
  return 0;
}

char LICENSE[] SEC("license") = "GPL";
