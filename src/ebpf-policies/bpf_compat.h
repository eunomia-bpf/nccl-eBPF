#ifndef NCCL_POLICY_BPF_COMPAT_H_
#define NCCL_POLICY_BPF_COMPAT_H_

#include <stdint.h>

#define SEC(NAME) __attribute__((section(NAME), used))
#define __uint(NAME, VALUE) int (*NAME)[VALUE]
#define __type(NAME, VALUE) VALUE *NAME

#ifndef BPF_MAP_TYPE_HASH
#define BPF_MAP_TYPE_HASH 1
#endif

#ifndef BPF_ANY
#define BPF_ANY 0
#endif

#ifndef BPF_NOEXIST
#define BPF_NOEXIST 1
#endif

#ifndef BPF_EXIST
#define BPF_EXIST 2
#endif

static void *(*bpf_map_lookup_elem)(void *map, const void *key) = (void *)1;
static long (*bpf_map_update_elem)(void *map, const void *key,
                                   const void *value,
                                   unsigned long flags) = (void *)2;
static long (*bpf_map_delete_elem)(void *map, const void *key) = (void *)3;

#endif
