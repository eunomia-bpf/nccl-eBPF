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
uint64_t lookup_update_policy(struct nccl_policy_ctx *ctx) {
  struct nccl_policy_telemetry_key key = {};
  struct nccl_policy_telemetry_value next = {};
  struct nccl_policy_telemetry_value *prev;

  if (!ctx)
    return 0;

  key.coll_type = ctx->coll_type;
  key.n_nodes = ctx->n_nodes;
  prev = bpf_map_lookup_elem(&telemetry_map, &key);
  if (prev)
    next = *prev;

  next.last_latency_ns = ctx->last_latency_ns;
  next.avg_latency_ns = ctx->avg_latency_ns;
  next.p99_latency_ns = ctx->rolling_p99_ns;
  next.last_n_bytes = ctx->n_bytes;
  if (next.samples != UINT32_MAX)
    next.samples += 1;
  if (next.recommended_channels == 0)
    next.recommended_channels =
        ctx->current_channels ? ctx->current_channels : 1;

  bpf_map_update_elem(&telemetry_map, &key, &next, BPF_ANY);
  return 0;
}

char LICENSE[] SEC("license") = "GPL";
