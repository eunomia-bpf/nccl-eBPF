#include "bpf_compat.h"
#include "policy_action.h"
#include "policy_context.h"
#include "policy_maps.h"

struct {
  __uint(type, BPF_MAP_TYPE_HASH);
  __uint(max_entries, 16);
  __type(key, struct nccl_policy_config_key);
  __type(value, struct nccl_policy_config_value);
} config_map SEC(".maps");

struct {
  __uint(type, BPF_MAP_TYPE_HASH);
  __uint(max_entries, 64);
  __type(key, struct nccl_policy_telemetry_key);
  __type(value, struct nccl_policy_telemetry_value);
} telemetry_map SEC(".maps");

static uint32_t clamp_to_config(uint32_t value,
                                const struct nccl_policy_config_value *cfg)
{
  if (value < cfg->min_channels)
    return cfg->min_channels;
  if (value > cfg->max_channels)
    return cfg->max_channels;
  return value;
}

SEC("uprobe")
uint64_t slo_enforcer_policy(struct nccl_policy_ctx *ctx)
{
  struct nccl_policy_config_key cfg_key = {};
  struct nccl_policy_telemetry_key t_key = {};
  struct nccl_policy_config_value default_cfg = {
      .target_p99_ns = 150,
      .min_channels = 1,
      .max_channels = 8,
      .aggressiveness_step = 1,
  };
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
  if (!cfg)
    cfg = &default_cfg;

  t_key.coll_type = ctx->coll_type;
  t_key.n_nodes = ctx->n_nodes;
  prev = bpf_map_lookup_elem(&telemetry_map, &t_key);
  if (prev)
    current = *prev;

  observed_p99 = ctx->rolling_p99_ns ? ctx->rolling_p99_ns : current.p99_latency_ns;
  channels = ctx->current_channels ? ctx->current_channels : cfg->min_channels;
  channels = clamp_to_config(channels, cfg);

  if (observed_p99 > cfg->target_p99_ns) {
    channels = clamp_to_config(channels + cfg->aggressiveness_step, cfg);
    algo = ctx->n_bytes < (1ULL << 16) ? NCCL_POLICY_ALGO_TREE
                                       : NCCL_POLICY_ALGO_RING;
    proto = ctx->n_bytes < (1ULL << 20) ? NCCL_POLICY_PROTO_LL
                                        : NCCL_POLICY_PROTO_SIMPLE;
    aggressiveness = 3;
  } else {
    channels = clamp_to_config(channels - cfg->aggressiveness_step, cfg);
    algo = ctx->n_bytes < (1ULL << 14) ? NCCL_POLICY_ALGO_TREE
                                       : NCCL_POLICY_ALGO_RING;
    proto = NCCL_POLICY_PROTO_SIMPLE;
    aggressiveness = 1;
  }

  current.last_latency_ns = ctx->last_latency_ns;
  current.avg_latency_ns =
      current.samples == 0
          ? ctx->last_latency_ns
          : (current.avg_latency_ns * (uint64_t)current.samples +
             ctx->last_latency_ns) /
                (uint64_t)(current.samples + 1);
  current.p99_latency_ns = observed_p99 > ctx->last_latency_ns
                               ? observed_p99
                               : ctx->last_latency_ns;
  current.last_n_bytes = ctx->n_bytes;
  current.samples += 1;
  current.recommended_channels = channels;
  bpf_map_update_elem(&telemetry_map, &t_key, &current, BPF_ANY);

  return nccl_policy_pack_action(
      algo, proto, (uint8_t)channels, aggressiveness,
      NCCL_POLICY_ACTION_SET_ALGO | NCCL_POLICY_ACTION_SET_PROTO |
          NCCL_POLICY_ACTION_SET_CHANNELS |
          NCCL_POLICY_ACTION_SET_AGGRESSIVENESS);
}

char LICENSE[] SEC("license") = "GPL";
