#include "bpf_compat.h"
#include "policy_context.h"
#include "policy_maps.h"

struct {
  __uint(type, BPF_MAP_TYPE_HASH);
  __uint(max_entries, 64);
  __type(key, struct nccl_policy_telemetry_key);
  __type(value, struct nccl_policy_telemetry_value);
} telemetry_map SEC(".maps");

SEC("uprobe")
uint64_t lookup_only_policy(struct nccl_policy_ctx *ctx) {
  struct nccl_policy_telemetry_key key = {};

  if (!ctx)
    return 0;

  key.coll_type = ctx->coll_type;
  key.n_nodes = ctx->n_nodes;
  (void)bpf_map_lookup_elem(&telemetry_map, &key);
  return 0;
}

char LICENSE[] SEC("license") = "GPL";
