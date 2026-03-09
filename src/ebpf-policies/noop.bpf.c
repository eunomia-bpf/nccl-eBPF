#include "bpf_compat.h"
#include "policy_context.h"

SEC("uprobe")
uint64_t noop_policy(struct nccl_policy_ctx *ctx)
{
  (void)ctx;
  return 0;
}

char LICENSE[] SEC("license") = "GPL";
