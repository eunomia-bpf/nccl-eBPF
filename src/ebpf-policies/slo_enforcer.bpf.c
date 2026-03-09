#include "bpf_compat.h"
#include "policy_action.h"
#include "policy_context.h"
#include "policy_maps.h"

struct {
  __uint(type, BPF_MAP_TYPE_ARRAY);
  __uint(max_entries, NCCL_POLICY_COLL_ALLREDUCE + 1);
  __type(key, struct nccl_policy_config_key);
  __type(value, struct nccl_policy_config_value);
} config_map SEC(".maps");

struct {
  __uint(type, BPF_MAP_TYPE_HASH);
  __uint(max_entries, 64);
  __type(key, struct nccl_policy_telemetry_key);
  __type(value, struct nccl_policy_telemetry_value);
} telemetry_map SEC(".maps");

static uint32_t clamp_channels(uint32_t value, uint32_t min_channels,
                               uint32_t max_channels) {
  if (value < min_channels)
    return min_channels;
  if (value > max_channels)
    return max_channels;
  return value;
}

static uint32_t saturating_increase(uint32_t value, uint32_t step,
                                    uint32_t max_channels) {
  if (value >= max_channels)
    return max_channels;
  if (step >= max_channels - value)
    return max_channels;
  return value + step;
}

static uint32_t saturating_decrease(uint32_t value, uint32_t step,
                                    uint32_t min_channels) {
  if (value <= min_channels)
    return min_channels;
  if (step >= value - min_channels)
    return min_channels;
  return value - step;
}

static uint64_t update_average(uint64_t current_avg, uint64_t sample,
                               uint32_t count) {
  if (count == 0)
    return sample;
  return (current_avg * 7 + sample) / 8;
}

SEC("uprobe")
uint64_t slo_enforcer_policy(struct nccl_policy_ctx *ctx) {
  struct nccl_policy_config_key cfg_key = {};
  struct nccl_policy_telemetry_key t_key = {};
  struct nccl_policy_config_value default_cfg = {
      .target_p99_ns = 150,
      .min_channels = 1,
      .max_channels = 8,
      .aggressiveness_step = 1,
  };
  struct nccl_policy_config_value cfg_value = default_cfg;
  struct nccl_policy_config_value *cfg;
  struct nccl_policy_telemetry_value current = {};
  struct nccl_policy_telemetry_value *prev;
  uint64_t observed_p99;
  uint32_t channels;
  uint8_t algo;
  uint8_t proto;
  uint8_t aggressiveness;

  if (!ctx)
    return 0;

  cfg_key.coll_type = ctx->coll_type;
  cfg = bpf_map_lookup_elem(&config_map, &cfg_key);
  if (cfg)
    cfg_value = *cfg;

  t_key.coll_type = ctx->coll_type;
  t_key.n_nodes = ctx->n_nodes;
  prev = bpf_map_lookup_elem(&telemetry_map, &t_key);
  if (prev)
    current = *prev;

  observed_p99 =
      ctx->rolling_p99_ns ? ctx->rolling_p99_ns : current.p99_latency_ns;
  channels =
      ctx->current_channels ? ctx->current_channels : cfg_value.min_channels;
  channels =
      clamp_channels(channels, cfg_value.min_channels, cfg_value.max_channels);

  if (observed_p99 > cfg_value.target_p99_ns) {
    channels = saturating_increase(channels, cfg_value.aggressiveness_step,
                                   cfg_value.max_channels);
    algo = ctx->n_bytes < (1ULL << 16) ? NCCL_POLICY_ALGO_TREE
                                       : NCCL_POLICY_ALGO_RING;
    proto = ctx->n_bytes < (1ULL << 20) ? NCCL_POLICY_PROTO_LL
                                        : NCCL_POLICY_PROTO_SIMPLE;
    aggressiveness = 3;
  } else {
    channels = saturating_decrease(channels, cfg_value.aggressiveness_step,
                                   cfg_value.min_channels);
    algo = ctx->n_bytes < (1ULL << 14) ? NCCL_POLICY_ALGO_TREE
                                       : NCCL_POLICY_ALGO_RING;
    proto = NCCL_POLICY_PROTO_SIMPLE;
    aggressiveness = 1;
  }

  current.last_latency_ns = ctx->last_latency_ns;
  current.avg_latency_ns = update_average(
      current.avg_latency_ns, ctx->last_latency_ns, current.samples);
  if (ctx->last_latency_ns > current.p99_latency_ns)
    current.p99_latency_ns = ctx->last_latency_ns;
  current.last_n_bytes = ctx->n_bytes;
  if (current.samples != UINT32_MAX)
    current.samples += 1;
  current.recommended_channels = channels;
  bpf_map_update_elem(&telemetry_map, &t_key, &current, BPF_ANY);

  return nccl_policy_pack_action(algo, proto, (uint8_t)channels, aggressiveness,
                                 NCCL_POLICY_ACTION_SET_ALGO |
                                     NCCL_POLICY_ACTION_SET_PROTO |
                                     NCCL_POLICY_ACTION_SET_CHANNELS |
                                     NCCL_POLICY_ACTION_SET_AGGRESSIVENESS);
}

char LICENSE[] SEC("license") = "GPL";
