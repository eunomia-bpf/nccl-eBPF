#ifndef NCCL_POLICY_CONTEXT_H_
#define NCCL_POLICY_CONTEXT_H_

#ifdef __BPF__
#include "bpf_compat.h"
#else
#include <stddef.h>
#include <stdint.h>
#endif

/* Hosts predating NVL topology passed the 72-byte v1 context and left the
 * reserved field zero. New policies must check the size marker before reading
 * fields beyond that prefix. */
#define NCCL_POLICY_CTX_ABI_V1_SIZE 72U
#define NCCL_POLICY_CTX_ABI_V2_SIZE 88U

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
  /* Total context size in bytes; zero when populated by a v1 host. */
  uint32_t reserved;
  /* NVL topology supplied by the NCCL tuner v5 initialization ABI. These
   * fields are zero when the caller does not provide topology information.
   * Keep new fields appended so policies built against the original prefix
   * retain the same offsets. */
  uint32_t n_nvl_domains;
  uint32_t min_ranks_per_nvl_domain;
  uint32_t max_ranks_per_nvl_domain;
  uint32_t reserved2;
};

#if defined(__cplusplus)
static_assert(offsetof(struct nccl_policy_ctx, coll_type) == 40,
              "nccl_policy_ctx v1 coll_type offset changed");
static_assert(offsetof(struct nccl_policy_ctx, reserved) == 64,
              "nccl_policy_ctx v1 reserved offset changed");
static_assert(offsetof(struct nccl_policy_ctx, n_nvl_domains) == 68,
              "nccl_policy_ctx NVL fields must follow the v1 fields");
static_assert(sizeof(struct nccl_policy_ctx) == NCCL_POLICY_CTX_ABI_V2_SIZE,
              "nccl_policy_ctx v2 size changed");
#else
_Static_assert(__builtin_offsetof(struct nccl_policy_ctx, coll_type) == 40,
               "nccl_policy_ctx v1 coll_type offset changed");
_Static_assert(__builtin_offsetof(struct nccl_policy_ctx, reserved) == 64,
               "nccl_policy_ctx v1 reserved offset changed");
_Static_assert(__builtin_offsetof(struct nccl_policy_ctx, n_nvl_domains) == 68,
               "nccl_policy_ctx NVL fields must follow the v1 fields");
_Static_assert(sizeof(struct nccl_policy_ctx) == NCCL_POLICY_CTX_ABI_V2_SIZE,
               "nccl_policy_ctx v2 size changed");
#endif

/* Profiler input ABI. The host populates this after measuring a collective and
 * invokes a separately loaded SEC("profiler") program. */
struct nccl_profiler_ctx {
  uint64_t n_bytes;
  uint64_t latency_ns;
  uint32_t coll_type;
  uint32_t n_nodes;
  uint32_t channel_count;
  uint32_t reserved;
};

#endif
