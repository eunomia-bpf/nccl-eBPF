#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif

#include <dlfcn.h>
#include <inttypes.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "native_baseline.h"
#include "nccl_tuner.h"
#include "policy_test_paths.h"

enum { kIterations = 1000000 };

struct policy_case {
  const char *name;
  const char *path;
  const char *verify_mode;
};

struct benchmark_result {
  const char *name;
  const char *path;
  const char *verify_mode;
  uint64_t p50;
  uint64_t p99;
  uint64_t max;
  int last_channels;
  int last_algo;
  int last_proto;
};

static uint64_t monotonic_time_ns(void)
{
  struct timespec ts;
  clock_gettime(CLOCK_MONOTONIC_RAW, &ts);
  return (uint64_t)ts.tv_sec * 1000000000ull + (uint64_t)ts.tv_nsec;
}

static int compare_u64(const void *lhs, const void *rhs)
{
  const uint64_t a = *(const uint64_t *)lhs;
  const uint64_t b = *(const uint64_t *)rhs;
  if (a < b) return -1;
  if (a > b) return 1;
  return 0;
}

static void compute_stats(uint64_t *samples, size_t count,
                          uint64_t *p50, uint64_t *p99, uint64_t *max)
{
  qsort(samples, count, sizeof(*samples), compare_u64);
  *p50 = samples[count / 2];
  *p99 = samples[(count * 99) / 100];
  *max = samples[count - 1];
}

static int64_t delta_u64(uint64_t lhs, uint64_t rhs)
{
  return (int64_t)lhs - (int64_t)rhs;
}

static void no_op_logger(ncclDebugLogLevel level, unsigned long flags,
                         const char *file, int line, const char *fmt, ...)
{
  (void)level;
  (void)flags;
  (void)file;
  (void)line;
  (void)fmt;
}

static void reset_cost_table(float cost_table[NCCL_NUM_ALGORITHMS]
                                             [NCCL_NUM_PROTOCOLS],
                             float *cost_table_ptr[NCCL_NUM_ALGORITHMS])
{
  size_t i;
  size_t j;

  for (i = 0; i < NCCL_NUM_ALGORITHMS; ++i) {
    cost_table_ptr[i] = cost_table[i];
    for (j = 0; j < NCCL_NUM_PROTOCOLS; ++j)
      cost_table[i][j] = 1.0f;
  }
}

static void detect_forced_choice(float cost_table[NCCL_NUM_ALGORITHMS]
                                                 [NCCL_NUM_PROTOCOLS],
                                 int *algo, int *proto)
{
  int i;
  int j;

  *algo = -1;
  *proto = -1;
  for (i = 0; i < NCCL_NUM_ALGORITHMS; ++i) {
    for (j = 0; j < NCCL_NUM_PROTOCOLS; ++j) {
      if (cost_table[i][j] == 0.0f) {
        *algo = i;
        *proto = j;
        return;
      }
    }
  }
}

static int benchmark_native_size_aware(uint64_t *samples, size_t count,
                                       struct benchmark_result *result)
{
  const ncclFunc_t coll_types[] = {
      ncclFuncBroadcast, ncclFuncReduce, ncclFuncAllGather,
      ncclFuncReduceScatter, ncclFuncAllReduce,
  };
  volatile uint64_t sink = 0;
  size_t i;

  for (i = 0; i < count; ++i) {
    struct nccl_policy_ctx ctx = {0};
    uint64_t start_ns;
    uint64_t end_ns;

    ctx.n_bytes = ((size_t)1 << (10 + (i % 11))) + (i & 255u);
    ctx.coll_type = (uint32_t)coll_types[i % (sizeof(coll_types) /
                                              sizeof(coll_types[0]))];
    ctx.num_pipe_ops = 1 + (uint32_t)(i % 4);
    ctx.reg_buff = (uint32_t)(i & 1u);
    ctx.n_ranks = 8;
    ctx.n_nodes = 1;
    ctx.current_channels = 1;

    start_ns = monotonic_time_ns();
    sink ^= nccl_native_size_aware_v2(&ctx);
    end_ns = monotonic_time_ns();
    samples[i] = end_ns - start_ns;
  }

  compute_stats(samples, count, &result->p50, &result->p99, &result->max);
  result->name = "native_size_aware_v2";
  result->path = "builtin";
  result->last_channels = (int)(sink & 0xffu);
  result->last_algo = -1;
  result->last_proto = -1;
  return 0;
}

static int benchmark_policy(const char *plugin_path,
                            const struct policy_case *policy,
                            uint64_t *samples, size_t count,
                            struct benchmark_result *result)
{
  const ncclFunc_t coll_types[] = {
      ncclFuncBroadcast, ncclFuncReduce, ncclFuncAllGather,
      ncclFuncReduceScatter, ncclFuncAllReduce,
  };
  float cost_table[NCCL_NUM_ALGORITHMS][NCCL_NUM_PROTOCOLS];
  float *cost_table_ptr[NCCL_NUM_ALGORITHMS];
  void *handle = NULL;
  void *plugin_context = NULL;
  const ncclTuner_v5_t *plugin = NULL;
  size_t i;
  int n_channels = 1;

  if (setenv("NCCL_POLICY_BPF_PATH", policy->path, 1) != 0) {
    perror("setenv");
    return -1;
  }
  if (setenv("NCCL_POLICY_VERIFY_MODE", policy->verify_mode, 1) != 0) {
    perror("setenv");
    return -1;
  }

  handle = dlopen(plugin_path, RTLD_NOW | RTLD_LOCAL);
  if (!handle) {
    fprintf(stderr, "dlopen failed for %s: %s\n", plugin_path, dlerror());
    return -1;
  }

  plugin = (const ncclTuner_v5_t *)dlsym(handle, NCCL_TUNER_PLUGIN_SYMBOL);
  if (!plugin) {
    fprintf(stderr, "dlsym failed for %s: %s\n", NCCL_TUNER_PLUGIN_SYMBOL,
            dlerror());
    dlclose(handle);
    return -1;
  }

  if (plugin->init(&plugin_context, 0, 8, 1, no_op_logger, NULL, NULL) !=
      ncclSuccess) {
    fprintf(stderr, "plugin init failed for %s\n", policy->name);
    dlclose(handle);
    return -1;
  }

  for (i = 0; i < count; ++i) {
    const size_t n_bytes = ((size_t)1 << (10 + (i % 11))) + (i & 255u);
    const ncclFunc_t coll_type = coll_types[i % (sizeof(coll_types) /
                                                 sizeof(coll_types[0]))];
    const int pipe_ops = 1 + (int)(i % 4);
    const int reg_buff = (int)(i & 1u);
    uint64_t start_ns;
    uint64_t end_ns;

    reset_cost_table(cost_table, cost_table_ptr);
    n_channels = 1;

    start_ns = monotonic_time_ns();
    if (plugin->getCollInfo(plugin_context, coll_type, n_bytes, pipe_ops,
                            cost_table_ptr, NCCL_NUM_ALGORITHMS,
                            NCCL_NUM_PROTOCOLS, reg_buff, &n_channels) !=
        ncclSuccess) {
      fprintf(stderr, "plugin getCollInfo failed for %s at iteration %zu\n",
              policy->name, i);
      plugin->finalize(plugin_context);
      dlclose(handle);
      return -1;
    }
    end_ns = monotonic_time_ns();
    samples[i] = end_ns - start_ns;
  }

  detect_forced_choice(cost_table, &result->last_algo, &result->last_proto);
  result->last_channels = n_channels;
  result->name = policy->name;
  result->path = policy->path;
  result->verify_mode = policy->verify_mode;
  compute_stats(samples, count, &result->p50, &result->p99, &result->max);

  plugin->finalize(plugin_context);
  dlclose(handle);
  return 0;
}

static int test_verifier_rejection(const char *plugin_path)
{
  void *handle = NULL;
  const ncclTuner_v5_t *plugin = NULL;
  void *plugin_context = NULL;
  int rc = 0;

  if (setenv("NCCL_POLICY_BPF_PATH", NCCL_POLICY_TEST_BAD_BPF_PATH, 1) != 0) {
    perror("setenv");
    return -1;
  }
  if (setenv("NCCL_POLICY_VERIFY_MODE", "strict", 1) != 0) {
    perror("setenv");
    return -1;
  }

  handle = dlopen(plugin_path, RTLD_NOW | RTLD_LOCAL);
  if (!handle) {
    fprintf(stderr, "dlopen failed for %s: %s\n", plugin_path, dlerror());
    return -1;
  }

  plugin = (const ncclTuner_v5_t *)dlsym(handle, NCCL_TUNER_PLUGIN_SYMBOL);
  if (!plugin) {
    fprintf(stderr, "dlsym failed for %s: %s\n", NCCL_TUNER_PLUGIN_SYMBOL,
            dlerror());
    dlclose(handle);
    return -1;
  }

  rc = plugin->init(&plugin_context, 0, 8, 1, no_op_logger, NULL, NULL);
  if (rc == ncclSuccess) {
    fprintf(stderr, "verifier unexpectedly accepted bad program\n");
    plugin->finalize(plugin_context);
    dlclose(handle);
    return -1;
  }

  dlclose(handle);
  return 0;
}

int main(int argc, char **argv)
{
  const char *plugin_path = argc > 1 ? argv[1] : "./libnccl-policy.so";
  const struct policy_case policies[] = {
      {"noop", NCCL_POLICY_TEST_NOOP_BPF_PATH, "strict"},
      {"size_aware", NCCL_POLICY_TEST_SIZE_AWARE_BPF_PATH, "strict"},
      {"size_aware_v2", NCCL_POLICY_TEST_SIZE_AWARE_V2_BPF_PATH, "strict"},
      {"adaptive_channels", NCCL_POLICY_TEST_ADAPTIVE_CHANNELS_BPF_PATH,
       "warning"},
      {"slo_enforcer", NCCL_POLICY_TEST_SLO_ENFORCER_BPF_PATH, "warning"},
  };
  struct benchmark_result native_result = {0};
  struct benchmark_result policy_result = {0};
  uint64_t *samples = (uint64_t *)calloc(kIterations, sizeof(*samples));
  size_t i;

  if (!samples) {
    fprintf(stderr, "failed to allocate benchmark samples\n");
    return 1;
  }

  if (benchmark_native_size_aware(samples, kIterations, &native_result) != 0) {
    free(samples);
    return 1;
  }

  printf("native baseline ns: p50=%" PRIu64 " p99=%" PRIu64 " max=%" PRIu64
         "\n",
         native_result.p50, native_result.p99, native_result.max);

  for (i = 0; i < sizeof(policies) / sizeof(policies[0]); ++i) {
    if (benchmark_policy(plugin_path, &policies[i], samples, kIterations,
                         &policy_result) != 0) {
      free(samples);
      return 1;
    }

    printf("%s (%s, verify=%s) ns: p50=%" PRIu64 " p99=%" PRIu64 " max=%" PRIu64
           " delta_p50=%" PRId64 " delta_p99=%" PRId64 " channels=%d"
           " algo=%d proto=%d\n",
           policy_result.name, policy_result.path, policy_result.verify_mode,
           policy_result.p50, policy_result.p99, policy_result.max,
           delta_u64(policy_result.p50, native_result.p50),
           delta_u64(policy_result.p99, native_result.p99),
           policy_result.last_channels, policy_result.last_algo,
           policy_result.last_proto);
  }

  if (test_verifier_rejection(plugin_path) != 0) {
    free(samples);
    return 1;
  }
  printf("verifier rejection: PASS (%s)\n", NCCL_POLICY_TEST_BAD_BPF_PATH);

  free(samples);
  return 0;
}
