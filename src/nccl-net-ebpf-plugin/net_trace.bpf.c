#include "../ebpf-policies/bpf_compat.h"
#include "net_ebpf_ctx.h"

struct {
  __uint(type, BPF_MAP_TYPE_ARRAY);
  __uint(max_entries, NCCL_NET_EBPF_HOOK_COUNT);
  __type(key, uint32_t);
  __type(value, struct nccl_net_ebpf_stat);
} stats_map SEC(".maps");

SEC("uprobe")
uint64_t net_trace_policy(struct nccl_net_ebpf_ctx *ctx) {
  uint32_t key;
  struct nccl_net_ebpf_stat *stat;

  if (!ctx)
    return 0;

  key = ctx->hook;
  if (key >= NCCL_NET_EBPF_HOOK_COUNT)
    return 0;

  stat = bpf_map_lookup_elem(&stats_map, &key);
  if (!stat)
    return 0;

  stat->calls += 1;
  stat->bytes += ctx->size;
  stat->last_comm_id = ctx->comm_id;
  stat->last_tag = ctx->tag;
  return 0;
}

char LICENSE[] SEC("license") = "GPL";
