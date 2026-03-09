#ifndef NCCL_NATIVE_BASELINE_H_
#define NCCL_NATIVE_BASELINE_H_

#include <stdint.h>

#include "../ebpf-policies/policy_context.h"

#ifdef __cplusplus
extern "C" {
#endif

uint64_t nccl_native_size_aware_v2(const struct nccl_policy_ctx *ctx);

#ifdef __cplusplus
}
#endif

#endif
