#ifndef NCCL_POLICY_CONTEXT_H_
#define NCCL_POLICY_CONTEXT_H_

#include <stdint.h>

enum nccl_policy_coll_type {
  NCCL_POLICY_COLL_BROADCAST = 0,
  NCCL_POLICY_COLL_REDUCE = 1,
  NCCL_POLICY_COLL_ALLGATHER = 2,
  NCCL_POLICY_COLL_REDUCESCATTER = 3,
  NCCL_POLICY_COLL_ALLREDUCE = 4,
};

struct nccl_policy_ctx {
  uint64_t n_bytes;
  /* Placeholder until NCCL profiler adapter integration provides real
   * collective telemetry. The plugin currently leaves these at zero instead of
   * feeding back its own dispatch overhead.
   */
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
