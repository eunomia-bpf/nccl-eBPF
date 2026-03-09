#define _GNU_SOURCE

#include <dlfcn.h>
#include <inttypes.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "tuner.h"

enum { kIterations = 1000000 };

static uint64_t monotonic_time_ns(void) {
  struct timespec ts;
  clock_gettime(CLOCK_MONOTONIC_RAW, &ts);
  return (uint64_t)ts.tv_sec * 1000000000ull + (uint64_t)ts.tv_nsec;
}

static int compare_u64(const void *lhs, const void *rhs) {
  const uint64_t a = *(const uint64_t *)lhs;
  const uint64_t b = *(const uint64_t *)rhs;
  if (a < b) return -1;
  if (a > b) return 1;
  return 0;
}

static void compute_stats(uint64_t *samples, size_t count,
                          uint64_t *p50, uint64_t *p99, uint64_t *max) {
  qsort(samples, count, sizeof(*samples), compare_u64);
  *p50 = samples[count / 2];
  *p99 = samples[(count * 99) / 100];
  *max = samples[count - 1];
}

static void benchmark_empty_call(volatile uint64_t *sink, size_t n_bytes,
                                 int coll_type, int pipe_ops, int reg_buff) {
  asm volatile("" : : "r"(sink), "r"(n_bytes), "r"(coll_type), "r"(pipe_ops),
               "r"(reg_buff) : "memory");
}

static void no_op_logger(ncclDebugLogLevel level, unsigned long flags,
                         const char *file, int line, const char *fmt, ...) {
  (void)level;
  (void)flags;
  (void)file;
  (void)line;
  (void)fmt;
}

int main(int argc, char **argv) {
  const char *plugin_path = argc > 1 ? argv[1] : "./libnccl-policy.so";
  const char *policy_path = argc > 2 ? argv[2] : NCCL_POLICY_TEST_BPF_PATH;
  const ncclFunc_t coll_types[] = {
      ncclFuncBroadcast, ncclFuncReduce, ncclFuncAllGather,
      ncclFuncReduceScatter, ncclFuncAllReduce,
  };

  uint64_t *plugin_latencies = calloc(kIterations, sizeof(*plugin_latencies));
  uint64_t *baseline_latencies =
      calloc(kIterations, sizeof(*baseline_latencies));
  float cost_table[NCCL_NUM_ALGORITHMS][NCCL_NUM_PROTOCOLS];
  float *cost_table_ptr[NCCL_NUM_ALGORITHMS];
  volatile uint64_t baseline_sink = 0;
  void *handle;
  const ncclTuner_v5_t *plugin;
  void *plugin_context = NULL;
  int n_channels = 1;
  size_t i;

  if (!plugin_latencies || !baseline_latencies) {
    fprintf(stderr, "failed to allocate latency buffers\n");
    return 1;
  }

  for (i = 0; i < NCCL_NUM_ALGORITHMS; ++i) {
    cost_table_ptr[i] = cost_table[i];
    for (int j = 0; j < NCCL_NUM_PROTOCOLS; ++j) {
      cost_table[i][j] = 1.0f;
    }
  }

  if (setenv("NCCL_POLICY_BPF_PATH", policy_path, 1) != 0) {
    perror("setenv");
    return 1;
  }

  handle = dlopen(plugin_path, RTLD_NOW | RTLD_LOCAL);
  if (!handle) {
    fprintf(stderr, "dlopen failed for %s: %s\n", plugin_path, dlerror());
    return 1;
  }

  plugin = (const ncclTuner_v5_t *)dlsym(handle, NCCL_TUNER_PLUGIN_SYMBOL);
  if (!plugin) {
    fprintf(stderr, "dlsym failed for %s: %s\n", NCCL_TUNER_PLUGIN_SYMBOL,
            dlerror());
    dlclose(handle);
    return 1;
  }

  if (plugin->init(&plugin_context, 0, 8, 1, no_op_logger, NULL, NULL) !=
      ncclSuccess) {
    fprintf(stderr, "plugin init failed\n");
    dlclose(handle);
    return 1;
  }

  for (i = 0; i < kIterations; ++i) {
    const size_t n_bytes =
        (size_t)1 << (10 + (i % 11));
    const ncclFunc_t coll_type = coll_types[i % (sizeof(coll_types) /
                                                 sizeof(coll_types[0]))];
    const int pipe_ops = 1 + (int)(i % 4);
    const int reg_buff = (int)(i & 1u);
    uint64_t start_ns;
    uint64_t end_ns;

    start_ns = monotonic_time_ns();
    if (plugin->getCollInfo(plugin_context, coll_type, n_bytes + (i & 255u),
                            pipe_ops, cost_table_ptr, NCCL_NUM_ALGORITHMS,
                            NCCL_NUM_PROTOCOLS, reg_buff, &n_channels) !=
        ncclSuccess) {
      fprintf(stderr, "plugin getCollInfo failed at iteration %zu\n", i);
      plugin->finalize(plugin_context);
      dlclose(handle);
      return 1;
    }
    end_ns = monotonic_time_ns();
    plugin_latencies[i] = end_ns - start_ns;

    start_ns = monotonic_time_ns();
    benchmark_empty_call(&baseline_sink, n_bytes, coll_type, pipe_ops,
                         reg_buff);
    end_ns = monotonic_time_ns();
    baseline_latencies[i] = end_ns - start_ns;
  }

  plugin->finalize(plugin_context);
  dlclose(handle);

  {
    uint64_t plugin_p50, plugin_p99, plugin_max;
    uint64_t baseline_p50, baseline_p99, baseline_max;

    compute_stats(plugin_latencies, kIterations, &plugin_p50, &plugin_p99,
                  &plugin_max);
    compute_stats(baseline_latencies, kIterations, &baseline_p50,
                  &baseline_p99, &baseline_max);

    printf("policy path: %s\n", policy_path);
    printf("plugin latency ns: p50=%" PRIu64 " p99=%" PRIu64
           " max=%" PRIu64 "\n",
           plugin_p50, plugin_p99, plugin_max);
    printf("baseline latency ns: p50=%" PRIu64 " p99=%" PRIu64
           " max=%" PRIu64 "\n",
           baseline_p50, baseline_p99, baseline_max);
    printf("delta p50=%" PRIu64 " delta p99=%" PRIu64 " delta max=%" PRIu64
           "\n",
           plugin_p50 - baseline_p50, plugin_p99 - baseline_p99,
           plugin_max - baseline_max);
  }

  free(plugin_latencies);
  free(baseline_latencies);
  return 0;
}
