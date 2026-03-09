#include "bpf_compat.h"
#include "policy_context.h"

static long (*bpf_unregistered_helper)(void) = (void *)4096;

SEC("uprobe")
uint64_t bad_unregistered_helper_policy(struct nccl_policy_ctx *ctx)
{
  if (!ctx)
    return 0;

  return (uint64_t)bpf_unregistered_helper();
}

char LICENSE[] SEC("license") = "GPL";
