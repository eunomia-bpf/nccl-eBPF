#include "bpf_compat.h"
#include "policy_action.h"
#include "policy_context.h"
#include "policy_maps.h"

struct {
  __uint(type, BPF_MAP_TYPE_HASH);
  __uint(max_entries, 64);
  __type(key, struct nccl_policy_telemetry_key);
  __type(value, struct nccl_policy_telemetry_value);
} telemetry_map SEC(".maps");

static uint32_t base_channels(const struct nccl_policy_ctx *ctx) {
  if (ctx->n_bytes < (1ULL << 14))
    return 2;
  if (ctx->n_bytes < (1ULL << 20))
    return 4;
  return 8;
}

static uint32_t clamp_channels(uint32_t channels) {
  if (channels < 1)
    return 1;
  if (channels > 12)
    return 12;
  return channels;
}

SEC("uprobe")
uint64_t adaptive_channels_policy(struct nccl_policy_ctx *ctx) {
  struct nccl_policy_telemetry_key key = {};
  struct nccl_policy_telemetry_value next = {};
  struct nccl_policy_telemetry_value *prev;
  uint32_t channels;

  if (!ctx)
    return 0;

  key.coll_type = ctx->coll_type;
  key.n_nodes = ctx->n_nodes;

  prev = bpf_map_lookup_elem(&telemetry_map, &key);
  if (!prev) {
    channels = base_channels(ctx);
    next.recommended_channels = channels;
    bpf_map_update_elem(&telemetry_map, &key, &next, BPF_ANY);
    return nccl_policy_pack_action(0, 0, (uint8_t)channels, 0,
                                   NCCL_POLICY_ACTION_SET_CHANNELS);
  }

  next = *prev;
  channels = clamp_channels(prev->recommended_channels);
  if (prev->samples > prev->applied_samples) {
    if (prev->last_latency_ns > prev->avg_latency_ns + 32) {
      channels = clamp_channels(channels - 1);
    } else if (ctx->n_bytes >= (1ULL << 20) &&
               prev->last_latency_ns <= prev->avg_latency_ns) {
      channels = clamp_channels(channels + 1);
    }
    next.applied_samples = prev->samples;
  }

  next.recommended_channels = channels;
  bpf_map_update_elem(&telemetry_map, &key, &next, BPF_ANY);

  return nccl_policy_pack_action(0, 0, (uint8_t)channels, 0,
                                 NCCL_POLICY_ACTION_SET_CHANNELS);
}

char LICENSE[] SEC("license") = "GPL";
