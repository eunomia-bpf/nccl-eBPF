#include "policy_context.h"

#define SEC(NAME) __attribute__((section(NAME), used))

SEC("policy")
int noop_policy(struct nccl_policy_ctx *ctx)
{
  (void)ctx;
  return 0;
}

char LICENSE[] SEC("license") = "GPL";
