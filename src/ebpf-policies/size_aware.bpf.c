#include "policy_context.h"

#define SEC(NAME) __attribute__((section(NAME), used))

SEC("policy")
int size_aware_policy(struct nccl_policy_ctx *ctx)
{
  if (!ctx)
    return 0;

  if (ctx->n_bytes < 4096)
    return 1;
  if (ctx->n_bytes < (1ULL << 20))
    return 2;
  return 3;
}

char LICENSE[] SEC("license") = "GPL";
