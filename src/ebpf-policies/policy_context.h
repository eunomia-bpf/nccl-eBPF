#ifndef NCCL_POLICY_CONTEXT_H_
#define NCCL_POLICY_CONTEXT_H_

#ifdef __BPF__
#include "bpf_compat.h"
#else
#include <stdint.h>
#endif

enum nccl_policy_coll_type {
  NCCL_POLICY_COLL_BROADCAST = 0,
  NCCL_POLICY_COLL_REDUCE = 1,
  NCCL_POLICY_COLL_ALLGATHER = 2,
  NCCL_POLICY_COLL_REDUCESCATTER = 3,
  NCCL_POLICY_COLL_ALLREDUCE = 4,
};

struct nccl_policy_ctx {
  uint64_t n_bytes;
  /* Populated from profiler-fed telemetry_map snapshots when available. */
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
