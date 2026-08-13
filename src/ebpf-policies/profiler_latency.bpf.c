#include "bpf_compat.h"
#include "policy_context.h"
#include "policy_maps.h"

struct {
  __uint(type, BPF_MAP_TYPE_HASH);
  __uint(max_entries, 64);
  __type(key, struct nccl_policy_telemetry_key);
  __type(value, struct nccl_policy_telemetry_value);
} telemetry_map SEC(".maps");

SEC("profiler")
uint64_t record_latency(struct nccl_profiler_ctx *ctx) {
  struct nccl_policy_telemetry_key key = {};
  struct nccl_policy_telemetry_value next = {};
  struct nccl_policy_telemetry_value *prev;

  if (!ctx || ctx->latency_ns == 0)
    return 0;

  key.coll_type = ctx->coll_type;
  key.n_nodes = ctx->n_nodes;
  prev = bpf_map_lookup_elem(&telemetry_map, &key);
  if (prev)
    next = *prev;

  next.last_latency_ns = ctx->latency_ns;
  next.avg_latency_ns =
      prev ? (next.avg_latency_ns == 0
                  ? ctx->latency_ns
                  : (next.avg_latency_ns * 7 + ctx->latency_ns) / 8)
           : ctx->latency_ns;
  if (next.p99_latency_ns == 0 || ctx->latency_ns > next.p99_latency_ns)
    next.p99_latency_ns = ctx->latency_ns;
  else
    next.p99_latency_ns =
        (next.p99_latency_ns * 99 + ctx->latency_ns) / 100;
  next.last_n_bytes = ctx->n_bytes;
  next.observed_channels = ctx->channel_count;
  if (next.samples != UINT32_MAX)
    next.samples += 1;

  bpf_map_update_elem(&telemetry_map, &key, &next, BPF_ANY);
  return 0;
}

char LICENSE[] SEC("license") = "GPL";
