#include "bpf_compat.h"
#include "policy_action.h"
#include "policy_context.h"
#include "policy_maps.h"

struct {
  __uint(type, BPF_MAP_TYPE_ARRAY);
  __uint(max_entries, 1);
  __type(key, uint32_t);
  __type(value, uint64_t);
} local_latency SEC(".maps");

struct {
  __uint(type, BPF_MAP_TYPE_ARRAY);
  __uint(max_entries, 8);
  __type(key, uint32_t);
  __type(value, uint64_t);
} rank_slots SEC(".maps");

struct {
  __uint(type, BPF_MAP_TYPE_ARRAY);
  __uint(max_entries, 1);
  __type(key, uint32_t);
  __type(value, uint64_t);
} agreed_map SEC(".maps");

SEC("uprobe")
uint64_t distributed_max_latency_policy(struct nccl_policy_ctx *ctx) {
  uint32_t key = 0;
  uint64_t *max_latency;
  if (!ctx)
    return 0;
  max_latency = bpf_map_lookup_elem(&agreed_map, &key);
  if (!max_latency)
    return 0;
  /* The same agreed value yields the same action on every rank. */
  return nccl_policy_pack_action(0, 0, *max_latency > 1000000 ? 4 : 8,
                                 0, NCCL_POLICY_ACTION_SET_CHANNELS);
}

SEC("dist_merge")
uint64_t distributed_max_latency_merge(struct nccl_dist_merge_ctx *ctx) {
  uint64_t maximum = 0;
  uint32_t zero = 0;
  if (!ctx || ctx->n_ranks == 0 || ctx->n_ranks > 8)
    return 1;
#define MERGE_RANK(INDEX)                        \
  do {                                           \
    if (ctx->n_ranks > INDEX) {                   \
      uint32_t rank = INDEX;                     \
      uint64_t *latency =                         \
          bpf_map_lookup_elem(&rank_slots, &rank);\
      if (!latency)                               \
        return 1;                                 \
      if (*latency > maximum)                     \
        maximum = *latency;                       \
    }                                            \
  } while (0)
  MERGE_RANK(0);
  MERGE_RANK(1);
  MERGE_RANK(2);
  MERGE_RANK(3);
  MERGE_RANK(4);
  MERGE_RANK(5);
  MERGE_RANK(6);
  MERGE_RANK(7);
#undef MERGE_RANK
  bpf_map_update_elem(&agreed_map, &zero, &maximum, BPF_ANY);
  return 0;
}

char LICENSE[] SEC("license") = "GPL";

