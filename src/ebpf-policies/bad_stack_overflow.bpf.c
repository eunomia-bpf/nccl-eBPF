#include "bpf_compat.h"
#include "policy_context.h"

SEC("uprobe")
uint64_t bad_stack_overflow_policy(struct nccl_policy_ctx *ctx)
{
  if (!ctx)
    return 0;

  asm volatile("r1 = *(u64 *)(r10 - 520)\n" ::: "r1");
  return ctx->n_bytes;
}

char LICENSE[] SEC("license") = "GPL";
