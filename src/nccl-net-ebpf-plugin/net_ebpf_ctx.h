#ifndef NCCL_NET_EBPF_CTX_H_
#define NCCL_NET_EBPF_CTX_H_

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

enum nccl_net_ebpf_hook_type {
  NCCL_NET_EBPF_HOOK_INIT = 0,
  NCCL_NET_EBPF_HOOK_LISTEN = 1,
  NCCL_NET_EBPF_HOOK_CONNECT = 2,
  NCCL_NET_EBPF_HOOK_ACCEPT = 3,
  NCCL_NET_EBPF_HOOK_ISEND = 4,
  NCCL_NET_EBPF_HOOK_IRECV = 5,
  NCCL_NET_EBPF_HOOK_FINALIZE = 6,
  NCCL_NET_EBPF_HOOK_COUNT = 7,
};

struct nccl_net_ebpf_ctx {
  uint32_t hook;
  int32_t dev;
  int32_t tag;
  uint32_t flags;
  uint64_t size;
  uint64_t comm_id;
  uint64_t timestamp_ns;
};

struct nccl_net_ebpf_stat {
  uint64_t calls;
  uint64_t bytes;
  uint64_t last_comm_id;
  int64_t last_tag;
};

#define NCCL_NET_EBPF_STATS_MAP_NAME "stats_map"

#ifdef __cplusplus
}  // extern "C"
#endif

#endif
