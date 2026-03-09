#include "bpf_compat.h"
#include "policy_context.h"

SEC("uprobe")
uint64_t bad_div_zero_policy(struct nccl_policy_ctx *ctx)
{
  if (!ctx)
    return 0;

  return ctx->n_bytes / ctx->reg_buff;
}

char LICENSE[] SEC("license") = "GPL";
