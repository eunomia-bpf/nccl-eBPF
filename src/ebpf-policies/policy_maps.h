#ifndef NCCL_POLICY_MAPS_H_
#define NCCL_POLICY_MAPS_H_

#include <stdint.h>

struct nccl_policy_telemetry_key {
  uint32_t coll_type;
  uint32_t n_nodes;
};

struct nccl_policy_telemetry_value {
  uint64_t last_latency_ns;
  uint64_t avg_latency_ns;
  uint64_t p99_latency_ns;
  uint64_t last_n_bytes;
  uint32_t samples;
  uint32_t recommended_channels;
};

struct nccl_policy_config_key {
  uint32_t coll_type;
};

struct nccl_policy_config_value {
  uint64_t target_p99_ns;
  uint32_t min_channels;
  uint32_t max_channels;
  uint32_t aggressiveness_step;
};

#endif
