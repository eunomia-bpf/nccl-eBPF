#include "bpf_compat.h"
#include "policy_context.h"

SEC("uprobe")
uint64_t bad_infinite_loop_policy(struct nccl_policy_ctx *ctx)
{
  volatile uint64_t guard;

  if (!ctx)
    return 0;

  guard = ctx->call_count;
loop:
  if (guard <= ctx->n_bytes)
    goto loop;

  return guard;
}

char LICENSE[] SEC("license") = "GPL";
