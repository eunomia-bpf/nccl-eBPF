#ifndef NCCL_POLICY_BPF_COMPAT_H_
#define NCCL_POLICY_BPF_COMPAT_H_

/* BPF programs cannot use system libc headers; define stdint types manually */
typedef unsigned char      uint8_t;
typedef unsigned short     uint16_t;
typedef unsigned int       uint32_t;
typedef unsigned long long uint64_t;
typedef signed char        int8_t;
typedef short              int16_t;
typedef int                int32_t;
typedef long long          int64_t;

#define SEC(NAME) __attribute__((section(NAME), used))
#define __uint(NAME, VALUE) int (*NAME)[VALUE]
#define __type(NAME, VALUE) VALUE *NAME

#ifndef BPF_MAP_TYPE_HASH
#define BPF_MAP_TYPE_HASH 1
#endif

#ifndef BPF_MAP_TYPE_ARRAY
#define BPF_MAP_TYPE_ARRAY 2
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

#ifndef UINT8_MAX
#define UINT8_MAX  0xffu
#endif
#ifndef UINT16_MAX
#define UINT16_MAX 0xffffu
#endif
#ifndef UINT32_MAX
#define UINT32_MAX 0xffffffffu
#endif
#ifndef UINT64_MAX
#define UINT64_MAX 0xffffffffffffffffull
#endif

static void *(*bpf_map_lookup_elem)(void *map, const void *key) = (void *)1;
static long (*bpf_map_update_elem)(void *map, const void *key,
                                   const void *value,
                                   unsigned long flags) = (void *)2;
static long (*bpf_map_delete_elem)(void *map, const void *key) = (void *)3;

#endif
