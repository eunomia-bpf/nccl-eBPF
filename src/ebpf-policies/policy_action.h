#ifndef NCCL_POLICY_ACTION_H_
#define NCCL_POLICY_ACTION_H_

#ifdef __BPF__
#include "bpf_compat.h"
#else
#include <stdint.h>
#endif

enum nccl_policy_algo_id {
  NCCL_POLICY_ALGO_TREE = 0,
  NCCL_POLICY_ALGO_RING = 1,
  NCCL_POLICY_ALGO_COLLNET_DIRECT = 2,
  NCCL_POLICY_ALGO_COLLNET_CHAIN = 3,
  NCCL_POLICY_ALGO_NVLS = 4,
  NCCL_POLICY_ALGO_NVLS_TREE = 5,
  NCCL_POLICY_ALGO_PAT = 6,
};

enum nccl_policy_proto_id {
  NCCL_POLICY_PROTO_LL = 0,
  NCCL_POLICY_PROTO_LL128 = 1,
  NCCL_POLICY_PROTO_SIMPLE = 2,
};

enum nccl_policy_action_flags {
  NCCL_POLICY_ACTION_SET_ALGO = 1u << 0,
  NCCL_POLICY_ACTION_SET_PROTO = 1u << 1,
  NCCL_POLICY_ACTION_SET_CHANNELS = 1u << 2,
  NCCL_POLICY_ACTION_SET_AGGRESSIVENESS = 1u << 3,
};

static inline uint64_t nccl_policy_pack_action(uint8_t algo, uint8_t proto,
                                               uint8_t n_channels,
                                               uint8_t aggressiveness,
                                               uint8_t flags)
{
  return (uint64_t)algo |
         ((uint64_t)proto << 8) |
         ((uint64_t)n_channels << 16) |
         ((uint64_t)aggressiveness << 24) |
         ((uint64_t)flags << 32);
}

static inline uint8_t nccl_policy_action_algo(uint64_t action)
{
  return (uint8_t)(action & 0xffu);
}

static inline uint8_t nccl_policy_action_proto(uint64_t action)
{
  return (uint8_t)((action >> 8) & 0xffu);
}

static inline uint8_t nccl_policy_action_channels(uint64_t action)
{
  return (uint8_t)((action >> 16) & 0xffu);
}

static inline uint8_t nccl_policy_action_aggressiveness(uint64_t action)
{
  return (uint8_t)((action >> 24) & 0xffu);
}

static inline uint8_t nccl_policy_action_flags_get(uint64_t action)
{
  return (uint8_t)((action >> 32) & 0xffu);
}

#endif
