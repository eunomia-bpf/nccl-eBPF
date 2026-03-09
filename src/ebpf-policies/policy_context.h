#ifndef NCCL_POLICY_CONTEXT_H_
#define NCCL_POLICY_CONTEXT_H_

#include <stdint.h>

struct nccl_policy_ctx {
  uint64_t n_bytes;
  uint64_t last_latency_ns;
  uint64_t avg_latency_ns;
  uint64_t rolling_p99_ns;
  uint64_t call_count;
  uint32_t coll_type;
  uint32_t num_pipe_ops;
  uint32_t reg_buff;
  uint32_t n_ranks;
  uint32_t n_nodes;
  uint32_t current_channels;
  uint32_t reserved;
};

#endif
