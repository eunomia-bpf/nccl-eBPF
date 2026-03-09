#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif

#include <atomic>
#include <dlfcn.h>
#include <errno.h>
#include <inttypes.h>
#include <limits>
#include <linux/bpf.h>
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <thread>
#include <time.h>
#include <vector>

#include "native_baseline.h"
#include "nccl_tuner.h"
#include "policy_action.h"
#include "policy_context.h"
#include "policy_maps.h"
#include "policy_test_paths.h"

enum { kIterations = 1000000 };
enum { kWarmupIterations = 10000 };
enum { kAdaptivePhaseIterations = 500000 };
enum { kAdaptiveSampleStride = 10000 };
enum { kHotReloadIterations = 400000 };
enum { kHotReloadTriggerIteration = 200000 };

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

struct decision_result {
  int channels;
  int algo;
  int proto;
};

struct expected_case {
  ncclFunc_t coll_type;
  size_t n_bytes;
  int num_pipe_ops;
  int reg_buff;
  int expected_algo;
  int expected_proto;
  int expected_channels;
};

typedef int (*plugin_debug_get_map_fd_fn)(void *context, const char *map_name);
struct reload_debug_stats {
  uint64_t load_ns;
  uint64_t swap_ns;
  uint64_t total_ns;
};
typedef int (*plugin_debug_reload_policy_fn)(void *context,
                                             const char *policy_path,
                                             struct reload_debug_stats *stats);
struct synthetic_telemetry_config {
  uint64_t last_latency_ns;
  uint64_t avg_latency_ns;
  uint64_t rolling_p99_ns;
  uint32_t enabled;
  uint32_t reserved;
};
typedef int (*plugin_debug_set_synthetic_telemetry_fn)(
    void *context, const struct synthetic_telemetry_config *config);
typedef long (*bpftime_map_update_elem_fn)(int fd, const void *key,
                                           const void *value, uint64_t flags);
typedef const void *(*bpftime_map_lookup_elem_fn)(int fd, const void *key);

struct plugin_session {
  void *handle;
  const ncclTuner_v5_t *plugin;
  void *plugin_context;
  plugin_debug_get_map_fd_fn debug_get_map_fd;
  plugin_debug_reload_policy_fn debug_reload_policy;
  plugin_debug_set_synthetic_telemetry_fn debug_set_synthetic_telemetry;
  bpftime_map_update_elem_fn map_update_elem;
  bpftime_map_lookup_elem_fn map_lookup_elem;
};

struct hot_reload_result {
  uint64_t reload_load_ns;
  uint64_t reload_swap_ns;
  uint64_t reload_total_ns;
  uint64_t pre_reload_p50_ns;
  uint64_t pre_reload_p99_ns;
  uint64_t max_call_latency_ns;
  uint64_t slow_call_threshold_ns;
  size_t slow_call_count;
  size_t completed_calls;
  size_t failed_calls;
  size_t reload_trigger_call;
  size_t first_changed_call;
  int post_reload_channels;
  int post_reload_algo;
  int post_reload_proto;
};

struct adaptive_curve_result {
  enum { kMaxSamples = 100 };
  size_t sample_count;
  uint64_t calls[kMaxSamples];
  uint32_t channels[kMaxSamples];
  uint32_t map_channels[kMaxSamples];
  uint32_t boundary_before_channels;
  uint32_t boundary_after_channels;
};

static uint64_t monotonic_time_ns(void) {
  struct timespec ts;
  clock_gettime(CLOCK_MONOTONIC_RAW, &ts);
  return (uint64_t)ts.tv_sec * 1000000000ull + (uint64_t)ts.tv_nsec;
}

static int compare_u64(const void *lhs, const void *rhs) {
  const uint64_t a = *(const uint64_t *)lhs;
  const uint64_t b = *(const uint64_t *)rhs;
  if (a < b)
    return -1;
  if (a > b)
    return 1;
  return 0;
}

static void compute_stats(uint64_t *samples, size_t count, uint64_t *p50,
                          uint64_t *p99, uint64_t *max) {
  qsort(samples, count, sizeof(*samples), compare_u64);
  *p50 = samples[count / 2];
  *p99 = samples[(count * 99) / 100];
  *max = samples[count - 1];
}

static int64_t delta_u64(uint64_t lhs, uint64_t rhs) {
  return (int64_t)lhs - (int64_t)rhs;
}

static int failf(const char *fmt, ...) {
  va_list args;

  va_start(args, fmt);
  vfprintf(stderr, fmt, args);
  va_end(args);
  fputc('\n', stderr);
  return -1;
}

static void no_op_logger(ncclDebugLogLevel level, unsigned long flags,
                         const char *file, int line, const char *fmt, ...) {
  (void)level;
  (void)flags;
  (void)file;
  (void)line;
  (void)fmt;
}

static void
reset_cost_table(float cost_table[NCCL_NUM_ALGORITHMS][NCCL_NUM_PROTOCOLS],
                 float *cost_table_ptr[NCCL_NUM_ALGORITHMS]) {
  size_t i;
  size_t j;
  const int supported_algos[] = {NCCL_ALGO_TREE, NCCL_ALGO_RING};
  const int supported_protos[] = {
      NCCL_PROTO_LL,
      NCCL_PROTO_LL128,
      NCCL_PROTO_SIMPLE,
  };

  for (i = 0; i < NCCL_NUM_ALGORITHMS; ++i) {
    cost_table_ptr[i] = cost_table[i];
    for (j = 0; j < NCCL_NUM_PROTOCOLS; ++j)
      cost_table[i][j] = NCCL_ALGO_PROTO_IGNORE;
  }

  for (i = 0; i < sizeof(supported_algos) / sizeof(supported_algos[0]); ++i) {
    for (j = 0; j < sizeof(supported_protos) / sizeof(supported_protos[0]);
         ++j) {
      cost_table[supported_algos[i]][supported_protos[j]] = 1.0f;
    }
  }
}

static void
detect_forced_choice(float cost_table[NCCL_NUM_ALGORITHMS][NCCL_NUM_PROTOCOLS],
                     int *algo, int *proto) {
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

static void decode_action(uint64_t action, struct decision_result *decision) {
  decision->channels = (int)nccl_policy_action_channels(action);
  decision->algo = (int)nccl_policy_action_algo(action);
  decision->proto = (int)nccl_policy_action_proto(action);
}

static int open_plugin_session(struct plugin_session *session,
                               const char *plugin_path,
                               const struct policy_case *policy, size_t n_ranks,
                               size_t n_nodes) {
  memset(session, 0, sizeof(*session));

  if (setenv("NCCL_POLICY_BPF_PATH", policy->path, 1) != 0)
    return failf("setenv failed for NCCL_POLICY_BPF_PATH: %s", strerror(errno));
  if (setenv("NCCL_POLICY_VERIFY_MODE", policy->verify_mode, 1) != 0)
    return failf("setenv failed for NCCL_POLICY_VERIFY_MODE: %s",
                 strerror(errno));

  session->handle = dlopen(plugin_path, RTLD_NOW | RTLD_LOCAL);
  if (!session->handle)
    return failf("dlopen failed for %s: %s", plugin_path, dlerror());

  session->plugin =
      (const ncclTuner_v5_t *)dlsym(session->handle, NCCL_TUNER_PLUGIN_SYMBOL);
  if (!session->plugin) {
    dlclose(session->handle);
    session->handle = NULL;
    return failf("dlsym failed for %s: %s", NCCL_TUNER_PLUGIN_SYMBOL,
                 dlerror());
  }

  if (session->plugin->init(&session->plugin_context, 0, n_ranks, n_nodes,
                            no_op_logger, NULL, NULL) != ncclSuccess) {
    dlclose(session->handle);
    memset(session, 0, sizeof(*session));
    return failf("plugin init failed for %s (verify=%s)", policy->name,
                 policy->verify_mode);
  }

  session->debug_get_map_fd = (plugin_debug_get_map_fd_fn)dlsym(
      session->handle, "ncclPolicyPluginDebugGetMapFd");
  session->debug_reload_policy = (plugin_debug_reload_policy_fn)dlsym(
      session->handle, "ncclPolicyPluginDebugReloadPolicy");
  session->debug_set_synthetic_telemetry =
      (plugin_debug_set_synthetic_telemetry_fn)dlsym(
          session->handle, "ncclPolicyPluginDebugSetSyntheticTelemetry");
  session->map_update_elem = (bpftime_map_update_elem_fn)dlsym(
      session->handle, "bpftime_map_update_elem");
  session->map_lookup_elem = (bpftime_map_lookup_elem_fn)dlsym(
      session->handle, "bpftime_map_lookup_elem");
  return 0;
}

static void close_plugin_session(struct plugin_session *session) {
  if (session->plugin && session->plugin_context)
    session->plugin->finalize(session->plugin_context);
  if (session->handle)
    dlclose(session->handle);
  memset(session, 0, sizeof(*session));
}

static int run_policy_once(struct plugin_session *session, ncclFunc_t coll_type,
                           size_t n_bytes, int num_pipe_ops, int reg_buff,
                           int initial_channels,
                           struct decision_result *decision) {
  float cost_table[NCCL_NUM_ALGORITHMS][NCCL_NUM_PROTOCOLS];
  float *cost_table_ptr[NCCL_NUM_ALGORITHMS];
  int n_channels = initial_channels;

  reset_cost_table(cost_table, cost_table_ptr);
  if (session->plugin->getCollInfo(session->plugin_context, coll_type, n_bytes,
                                   num_pipe_ops, cost_table_ptr,
                                   NCCL_NUM_ALGORITHMS, NCCL_NUM_PROTOCOLS,
                                   reg_buff, &n_channels) != ncclSuccess) {
    return failf("plugin getCollInfo failed for bytes=%zu coll=%d", n_bytes,
                 (int)coll_type);
  }

  decision->channels = n_channels;
  detect_forced_choice(cost_table, &decision->algo, &decision->proto);
  return 0;
}

static int seed_telemetry_map(struct plugin_session *session,
                              int telemetry_map_fd,
                              const struct nccl_policy_telemetry_key *key,
                              const struct nccl_policy_telemetry_value *value) {
  if (!session->map_update_elem)
    return failf("bpftime_map_update_elem is unavailable");
  if (session->map_update_elem(telemetry_map_fd, key, value, BPF_ANY) != 0)
    return failf("failed to seed telemetry_map");
  return 0;
}

static const struct nccl_policy_telemetry_value *
lookup_telemetry_map(struct plugin_session *session, int telemetry_map_fd,
                     const struct nccl_policy_telemetry_key *key) {
  if (!session->map_lookup_elem)
    return NULL;
  return (const struct nccl_policy_telemetry_value *)session->map_lookup_elem(
      telemetry_map_fd, key);
}

static int set_synthetic_telemetry(struct plugin_session *session,
                                   uint64_t last_latency_ns,
                                   uint64_t avg_latency_ns,
                                   uint64_t rolling_p99_ns, uint32_t enabled) {
  const struct synthetic_telemetry_config config = {
      .last_latency_ns = last_latency_ns,
      .avg_latency_ns = avg_latency_ns,
      .rolling_p99_ns = rolling_p99_ns,
      .enabled = enabled,
      .reserved = 0,
  };

  if (!session->debug_set_synthetic_telemetry)
    return failf("synthetic telemetry debug hook is unavailable");
  if (session->debug_set_synthetic_telemetry(session->plugin_context,
                                             enabled ? &config : NULL) != 0) {
    return failf("failed to configure synthetic telemetry");
  }
  return 0;
}

static int expect_choice(const char *policy_name, const char *label,
                         const struct decision_result *decision,
                         int expected_algo, int expected_proto,
                         int expected_channels) {
  if (decision->algo != expected_algo || decision->proto != expected_proto ||
      decision->channels != expected_channels) {
    return failf(
        "%s %s mismatch: got algo=%d proto=%d channels=%d expected algo=%d "
        "proto=%d channels=%d",
        policy_name, label, decision->algo, decision->proto, decision->channels,
        expected_algo, expected_proto, expected_channels);
  }
  return 0;
}

static int run_expected_cases(struct plugin_session *session,
                              const char *policy_name,
                              const struct expected_case *cases,
                              size_t case_count) {
  size_t i;

  for (i = 0; i < case_count; ++i) {
    char label[128];
    struct decision_result decision = {-1, -1, -1};

    snprintf(label, sizeof(label), "case[%zu] bytes=%zu coll=%d", i,
             cases[i].n_bytes, (int)cases[i].coll_type);
    if (run_policy_once(session, cases[i].coll_type, cases[i].n_bytes,
                        cases[i].num_pipe_ops, cases[i].reg_buff, 1,
                        &decision) != 0) {
      return -1;
    }
    if (expect_choice(policy_name, label, &decision, cases[i].expected_algo,
                      cases[i].expected_proto,
                      cases[i].expected_channels) != 0) {
      return -1;
    }
  }

  return 0;
}

static int test_size_aware_policies(const char *plugin_path) {
  const struct policy_case size_aware_policy = {
      "size_aware", NCCL_POLICY_TEST_SIZE_AWARE_BPF_PATH, "strict"};
  const struct policy_case size_aware_v2_policy = {
      "size_aware_v2", NCCL_POLICY_TEST_SIZE_AWARE_V2_BPF_PATH, "strict"};
  const struct expected_case size_aware_cases[] = {
      {ncclFuncAllReduce, 1024, 1, 0, NCCL_ALGO_TREE, NCCL_PROTO_SIMPLE, 2},
      {ncclFuncAllReduce, 32768, 1, 0, NCCL_ALGO_RING, NCCL_PROTO_LL, 4},
      {ncclFuncAllReduce, 1u << 20, 1, 0, NCCL_ALGO_RING, NCCL_PROTO_SIMPLE, 8},
  };
  const struct expected_case size_aware_v2_cases[] = {
      {ncclFuncAllReduce, 1024, 1, 0, NCCL_ALGO_TREE, NCCL_PROTO_SIMPLE, 2},
      {ncclFuncAllReduce, 32768, 1, 0, NCCL_ALGO_TREE, NCCL_PROTO_LL, 4},
      {ncclFuncAllReduce, 1u << 20, 1, 0, NCCL_ALGO_RING, NCCL_PROTO_SIMPLE,
       10},
      {ncclFuncBroadcast, 32768, 1, 0, NCCL_ALGO_RING, NCCL_PROTO_LL, 4},
  };
  const ncclFunc_t coll_types[] = {
      ncclFuncBroadcast,     ncclFuncReduce,    ncclFuncAllGather,
      ncclFuncReduceScatter, ncclFuncAllReduce,
  };
  const size_t sizes[] = {1024, 32768, 1u << 20};
  struct plugin_session session;
  size_t i;
  size_t j;

  if (open_plugin_session(&session, plugin_path, &size_aware_policy, 8, 1) !=
      0) {
    return -1;
  }
  if (run_expected_cases(&session, size_aware_policy.name, size_aware_cases,
                         sizeof(size_aware_cases) /
                             sizeof(size_aware_cases[0])) != 0) {
    close_plugin_session(&session);
    return -1;
  }
  close_plugin_session(&session);

  if (open_plugin_session(&session, plugin_path, &size_aware_v2_policy, 8, 1) !=
      0) {
    return -1;
  }
  if (run_expected_cases(
          &session, size_aware_v2_policy.name, size_aware_v2_cases,
          sizeof(size_aware_v2_cases) / sizeof(size_aware_v2_cases[0])) != 0) {
    close_plugin_session(&session);
    return -1;
  }

  for (i = 0; i < sizeof(coll_types) / sizeof(coll_types[0]); ++i) {
    for (j = 0; j < sizeof(sizes) / sizeof(sizes[0]); ++j) {
      struct nccl_policy_ctx ctx = {0};
      struct decision_result plugin_decision = {-1, -1, -1};
      struct decision_result native_decision = {-1, -1, -1};
      char label[128];

      ctx.n_bytes = sizes[j];
      ctx.coll_type = (uint32_t)coll_types[i];
      ctx.num_pipe_ops = 1;
      ctx.n_ranks = 8;
      ctx.n_nodes = 1;
      ctx.current_channels = 1;
      decode_action(nccl_native_size_aware_v2(&ctx), &native_decision);

      if (run_policy_once(&session, coll_types[i], sizes[j], 1, 0, 1,
                          &plugin_decision) != 0) {
        close_plugin_session(&session);
        return -1;
      }

      snprintf(label, sizeof(label), "native_equivalence coll=%d bytes=%zu",
               (int)coll_types[i], sizes[j]);
      if (expect_choice(size_aware_v2_policy.name, label, &plugin_decision,
                        native_decision.algo, native_decision.proto,
                        native_decision.channels) != 0) {
        close_plugin_session(&session);
        return -1;
      }
    }
  }

  close_plugin_session(&session);
  printf("size_aware correctness: PASS\n");
  return 0;
}

static int test_verifier_acceptance(const char *plugin_path,
                                    const struct policy_case *policies,
                                    size_t policy_count) {
  size_t i;

  for (i = 0; i < policy_count; ++i) {
    struct plugin_session session;

    if (open_plugin_session(&session, plugin_path, &policies[i], 8, 1) != 0)
      return -1;
    close_plugin_session(&session);
  }

  printf("verifier acceptance: PASS (%zu strict policies)\n", policy_count);
  return 0;
}

static int test_adaptive_channels_map_state(const char *plugin_path) {
  const struct policy_case policy = {
      "adaptive_channels", NCCL_POLICY_TEST_ADAPTIVE_CHANNELS_BPF_PATH,
      "strict"};
  struct plugin_session session;
  struct nccl_policy_telemetry_key key = {
      .coll_type = NCCL_POLICY_COLL_ALLREDUCE,
      .n_nodes = 1,
  };
  struct nccl_policy_telemetry_value seeded = {
      .last_latency_ns = 280,
      .avg_latency_ns = 240,
      .p99_latency_ns = 320,
      .last_n_bytes = 4096,
      .samples = 5,
      .recommended_channels = 7,
  };
  const struct nccl_policy_telemetry_value *observed = NULL;
  struct decision_result decision = {-1, -1, -1};
  int telemetry_map_fd = -1;

  if (open_plugin_session(&session, plugin_path, &policy, 8, 1) != 0)
    return -1;

  if (!session.debug_get_map_fd || !session.map_update_elem ||
      !session.map_lookup_elem) {
    close_plugin_session(&session);
    return failf("adaptive_channels map test requires debug map helpers");
  }

  telemetry_map_fd =
      session.debug_get_map_fd(session.plugin_context, "telemetry_map");
  if (telemetry_map_fd < 0) {
    close_plugin_session(&session);
    return failf("unable to find telemetry_map in adaptive_channels session");
  }
  if (session.map_lookup_elem(telemetry_map_fd, &key) != NULL) {
    close_plugin_session(&session);
    return failf("adaptive_channels telemetry_map was pre-populated before the "
                 "first real getCollInfo call");
  }

  if (session.map_update_elem(telemetry_map_fd, &key, &seeded, BPF_ANY) != 0) {
    close_plugin_session(&session);
    return failf("failed to seed adaptive_channels telemetry map");
  }

  if (run_policy_once(&session, ncclFuncAllReduce, 65536, 1, 0, 1, &decision) !=
      0) {
    close_plugin_session(&session);
    return -1;
  }
  if (decision.channels != 7) {
    close_plugin_session(&session);
    return failf(
        "adaptive_channels did not read seeded telemetry: got channels=%d "
        "expected 7",
        decision.channels);
  }

  observed =
      (const struct nccl_policy_telemetry_value *)session.map_lookup_elem(
          telemetry_map_fd, &key);
  if (!observed) {
    close_plugin_session(&session);
    return failf("adaptive_channels did not leave telemetry state behind");
  }
  if (observed->samples != seeded.samples + 1 ||
      observed->recommended_channels != seeded.recommended_channels ||
      observed->last_n_bytes != 65536) {
    close_plugin_session(&session);
    return failf("adaptive_channels telemetry state mismatch: samples=%u "
                 "recommended_channels=%u last_n_bytes=%" PRIu64,
                 observed->samples, observed->recommended_channels,
                 observed->last_n_bytes);
  }

  close_plugin_session(&session);
  printf("adaptive_channels map state: PASS\n");
  return 0;
}

static int test_verifier_rejection(const char *plugin_path) {
  const struct policy_case bad_policy = {
      "bad_lookup", NCCL_POLICY_TEST_BAD_BPF_PATH, "strict"};
  void *handle = NULL;
  const ncclTuner_v5_t *plugin = NULL;
  void *plugin_context = NULL;
  int rc = 0;

  if (setenv("NCCL_POLICY_BPF_PATH", bad_policy.path, 1) != 0)
    return failf("setenv failed for NCCL_POLICY_BPF_PATH: %s", strerror(errno));
  if (setenv("NCCL_POLICY_VERIFY_MODE", bad_policy.verify_mode, 1) != 0)
    return failf("setenv failed for NCCL_POLICY_VERIFY_MODE: %s",
                 strerror(errno));

  handle = dlopen(plugin_path, RTLD_NOW | RTLD_LOCAL);
  if (!handle)
    return failf("dlopen failed for %s: %s", plugin_path, dlerror());

  plugin = (const ncclTuner_v5_t *)dlsym(handle, NCCL_TUNER_PLUGIN_SYMBOL);
  if (!plugin) {
    dlclose(handle);
    return failf("dlsym failed for %s: %s", NCCL_TUNER_PLUGIN_SYMBOL,
                 dlerror());
  }

  rc = plugin->init(&plugin_context, 0, 8, 1, no_op_logger, NULL, NULL);
  if (rc == ncclSuccess) {
    plugin->finalize(plugin_context);
    dlclose(handle);
    return failf("verifier unexpectedly accepted bad_lookup in strict mode");
  }

  dlclose(handle);
  printf("verifier rejection: PASS (%s)\n", bad_policy.path);
  return 0;
}

static void warmup_native_size_aware(size_t count) {
  const ncclFunc_t coll_types[] = {
      ncclFuncBroadcast,     ncclFuncReduce,    ncclFuncAllGather,
      ncclFuncReduceScatter, ncclFuncAllReduce,
  };
  volatile uint64_t sink = 0;
  size_t i;

  for (i = 0; i < count; ++i) {
    struct nccl_policy_ctx ctx = {0};

    ctx.n_bytes = ((size_t)1 << (10 + (i % 11))) + (i & 255u);
    ctx.coll_type =
        (uint32_t)coll_types[i % (sizeof(coll_types) / sizeof(coll_types[0]))];
    ctx.num_pipe_ops = 1 + (uint32_t)(i % 4);
    ctx.reg_buff = (uint32_t)(i & 1u);
    ctx.n_ranks = 8;
    ctx.n_nodes = 1;
    ctx.current_channels = 1;
    sink ^= nccl_native_size_aware_v2(&ctx);
  }

  (void)sink;
}

static int warmup_policy_session(struct plugin_session *session, size_t count) {
  const ncclFunc_t coll_types[] = {
      ncclFuncBroadcast,     ncclFuncReduce,    ncclFuncAllGather,
      ncclFuncReduceScatter, ncclFuncAllReduce,
  };
  size_t i;

  for (i = 0; i < count; ++i) {
    struct decision_result decision = {-1, -1, -1};
    if (run_policy_once(
            session,
            coll_types[i % (sizeof(coll_types) / sizeof(coll_types[0]))],
            ((size_t)1 << (10 + (i % 11))) + (i & 255u), 1 + (int)(i % 4),
            (int)(i & 1u), 1, &decision) != 0) {
      return -1;
    }
  }

  return 0;
}

static int benchmark_native_size_aware(uint64_t *samples, size_t count,
                                       struct benchmark_result *result) {
  const ncclFunc_t coll_types[] = {
      ncclFuncBroadcast,     ncclFuncReduce,    ncclFuncAllGather,
      ncclFuncReduceScatter, ncclFuncAllReduce,
  };
  volatile uint64_t sink = 0;
  uint64_t last_action = 0;
  struct decision_result last_decision = {-1, -1, -1};
  size_t i;

  warmup_native_size_aware(kWarmupIterations);

  for (i = 0; i < count; ++i) {
    struct nccl_policy_ctx ctx = {0};
    uint64_t start_ns;
    uint64_t end_ns;

    ctx.n_bytes = ((size_t)1 << (10 + (i % 11))) + (i & 255u);
    ctx.coll_type =
        (uint32_t)coll_types[i % (sizeof(coll_types) / sizeof(coll_types[0]))];
    ctx.num_pipe_ops = 1 + (uint32_t)(i % 4);
    ctx.reg_buff = (uint32_t)(i & 1u);
    ctx.n_ranks = 8;
    ctx.n_nodes = 1;
    ctx.current_channels = 1;

    start_ns = monotonic_time_ns();
    last_action = nccl_native_size_aware_v2(&ctx);
    sink ^= last_action;
    end_ns = monotonic_time_ns();
    samples[i] = end_ns - start_ns;
  }

  (void)sink;
  decode_action(last_action, &last_decision);
  compute_stats(samples, count, &result->p50, &result->p99, &result->max);
  result->name = "native";
  result->path = "builtin";
  result->verify_mode = "builtin";
  result->last_channels = last_decision.channels;
  result->last_algo = last_decision.algo;
  result->last_proto = last_decision.proto;
  return 0;
}

static int benchmark_policy(const char *plugin_path,
                            const struct policy_case *policy, uint64_t *samples,
                            size_t count, struct benchmark_result *result) {
  const ncclFunc_t coll_types[] = {
      ncclFuncBroadcast,     ncclFuncReduce,    ncclFuncAllGather,
      ncclFuncReduceScatter, ncclFuncAllReduce,
  };
  float cost_table[NCCL_NUM_ALGORITHMS][NCCL_NUM_PROTOCOLS];
  float *cost_table_ptr[NCCL_NUM_ALGORITHMS];
  struct plugin_session session;
  struct decision_result last_decision = {-1, -1, -1};
  size_t i;

  if (open_plugin_session(&session, plugin_path, policy, 8, 1) != 0)
    return -1;
  if (warmup_policy_session(&session, kWarmupIterations) != 0) {
    close_plugin_session(&session);
    return -1;
  }

  for (i = 0; i < count; ++i) {
    const size_t n_bytes = ((size_t)1 << (10 + (i % 11))) + (i & 255u);
    const ncclFunc_t coll_type =
        coll_types[i % (sizeof(coll_types) / sizeof(coll_types[0]))];
    const int pipe_ops = 1 + (int)(i % 4);
    const int reg_buff = (int)(i & 1u);
    int n_channels = 1;
    uint64_t start_ns;
    uint64_t end_ns;

    reset_cost_table(cost_table, cost_table_ptr);

    start_ns = monotonic_time_ns();
    if (session.plugin->getCollInfo(session.plugin_context, coll_type, n_bytes,
                                    pipe_ops, cost_table_ptr,
                                    NCCL_NUM_ALGORITHMS, NCCL_NUM_PROTOCOLS,
                                    reg_buff, &n_channels) != ncclSuccess) {
      close_plugin_session(&session);
      return failf("plugin getCollInfo failed for %s at iteration %zu",
                   policy->name, i);
    }
    end_ns = monotonic_time_ns();
    samples[i] = end_ns - start_ns;

    last_decision.channels = n_channels;
    detect_forced_choice(cost_table, &last_decision.algo, &last_decision.proto);
  }

  compute_stats(samples, count, &result->p50, &result->p99, &result->max);
  result->name = policy->name;
  result->path = policy->path;
  result->verify_mode = policy->verify_mode;
  result->last_channels = last_decision.channels;
  result->last_algo = last_decision.algo;
  result->last_proto = last_decision.proto;
  close_plugin_session(&session);
  return 0;
}

static int test_hot_reload_latency(const char *plugin_path,
                                   struct hot_reload_result *result) {
  const struct policy_case initial_policy = {
      "noop", NCCL_POLICY_TEST_NOOP_BPF_PATH, "strict"};
  const size_t sentinel = std::numeric_limits<size_t>::max();
  struct plugin_session session;
  std::vector<uint64_t> call_latencies(kHotReloadIterations, 0);
  std::atomic<size_t> calls_started(0);
  std::atomic<size_t> failed_calls(0);
  std::atomic<int> reload_rc(-1);
  struct reload_debug_stats reload_stats = {0};
  std::atomic<size_t> first_changed_call(sentinel);
  std::atomic<int> post_reload_channels(-1);
  std::atomic<int> post_reload_algo(-1);
  std::atomic<int> post_reload_proto(-1);

  memset(result, 0, sizeof(*result));
  result->first_changed_call = sentinel;

  if (open_plugin_session(&session, plugin_path, &initial_policy, 8, 1) != 0)
    return -1;
  if (!session.debug_reload_policy) {
    close_plugin_session(&session);
    return failf("hot reload test requires reload debug hook");
  }
  if (warmup_policy_session(&session, kWarmupIterations) != 0) {
    close_plugin_session(&session);
    return -1;
  }

  std::thread caller([&]() {
    size_t i;

    for (i = 0; i < kHotReloadIterations; ++i) {
      float cost_table[NCCL_NUM_ALGORITHMS][NCCL_NUM_PROTOCOLS];
      float *cost_table_ptr[NCCL_NUM_ALGORITHMS];
      struct decision_result decision = {-1, -1, -1};
      int n_channels = 1;
      uint64_t start_ns;
      uint64_t end_ns;

      calls_started.store(i + 1, std::memory_order_release);
      reset_cost_table(cost_table, cost_table_ptr);
      start_ns = monotonic_time_ns();
      if (session.plugin->getCollInfo(session.plugin_context, ncclFuncAllReduce,
                                      1u << 20, 1, cost_table_ptr,
                                      NCCL_NUM_ALGORITHMS, NCCL_NUM_PROTOCOLS,
                                      0, &n_channels) != ncclSuccess) {
        failed_calls.fetch_add(1, std::memory_order_relaxed);
        continue;
      }
      end_ns = monotonic_time_ns();
      call_latencies[i] = end_ns - start_ns;
      decision.channels = n_channels;
      detect_forced_choice(cost_table, &decision.algo, &decision.proto);

      if (reload_rc.load(std::memory_order_acquire) == 0 &&
          decision.channels == 10 && decision.algo == NCCL_ALGO_RING &&
          decision.proto == NCCL_PROTO_SIMPLE) {
        size_t expected = sentinel;
        (void)first_changed_call.compare_exchange_strong(
            expected, i, std::memory_order_acq_rel, std::memory_order_acquire);
        post_reload_channels.store(decision.channels,
                                   std::memory_order_release);
        post_reload_algo.store(decision.algo, std::memory_order_release);
        post_reload_proto.store(decision.proto, std::memory_order_release);
      }
    }
  });

  std::thread reloader([&]() {
    while (calls_started.load(std::memory_order_acquire) <
           kHotReloadTriggerIteration) {
      std::this_thread::yield();
    }
    result->reload_trigger_call = calls_started.load(std::memory_order_acquire);

    reload_rc.store(session.debug_reload_policy(
                        session.plugin_context,
                        NCCL_POLICY_TEST_SIZE_AWARE_V2_BPF_PATH, &reload_stats),
                    std::memory_order_release);
  });

  caller.join();
  reloader.join();

  if (reload_rc.load(std::memory_order_acquire) != 0) {
    close_plugin_session(&session);
    return failf("hot reload failed");
  }

  {
    std::vector<uint64_t> pre_reload(call_latencies.begin(),
                                     call_latencies.begin() +
                                         result->reload_trigger_call);
    uint64_t max_dummy = 0;
    compute_stats(pre_reload.data(), pre_reload.size(),
                  &result->pre_reload_p50_ns, &result->pre_reload_p99_ns,
                  &max_dummy);
  }

  result->reload_load_ns = reload_stats.load_ns;
  result->reload_swap_ns = reload_stats.swap_ns;
  result->reload_total_ns = reload_stats.total_ns;
  result->slow_call_threshold_ns =
      std::max<uint64_t>(10000, result->pre_reload_p99_ns * 10);
  result->completed_calls = kHotReloadIterations;
  result->failed_calls = failed_calls.load(std::memory_order_acquire);
  result->first_changed_call =
      first_changed_call.load(std::memory_order_acquire);
  result->post_reload_channels =
      post_reload_channels.load(std::memory_order_acquire);
  result->post_reload_algo = post_reload_algo.load(std::memory_order_acquire);
  result->post_reload_proto = post_reload_proto.load(std::memory_order_acquire);

  for (size_t i = 0; i < call_latencies.size(); ++i) {
    if (call_latencies[i] > result->max_call_latency_ns)
      result->max_call_latency_ns = call_latencies[i];
    if (call_latencies[i] > result->slow_call_threshold_ns)
      result->slow_call_count++;
  }

  close_plugin_session(&session);
  if (result->failed_calls != 0 ||
      result->first_changed_call == std::numeric_limits<size_t>::max() ||
      result->post_reload_channels != 10 ||
      result->post_reload_algo != NCCL_ALGO_RING ||
      result->post_reload_proto != NCCL_PROTO_SIMPLE) {
    return failf("hot reload correctness check failed");
  }

  result->first_changed_call += 1;
  printf(
      "hot reload us: load=%.3f swap=%.3f total=%.3f"
      " pre_p50_ns=%" PRIu64 " pre_p99_ns=%" PRIu64 " max_call_ns=%" PRIu64
      " slow_calls=%zu threshold_ns=%" PRIu64
      " completed_calls=%zu failed_calls=%zu zero_call_loss=%s"
      " trigger_call=%zu first_changed_call=%zu channels=%d algo=%d proto=%d\n",
      result->reload_load_ns / 1000.0, result->reload_swap_ns / 1000.0,
      result->reload_total_ns / 1000.0, result->pre_reload_p50_ns,
      result->pre_reload_p99_ns, result->max_call_latency_ns,
      result->slow_call_count, result->slow_call_threshold_ns,
      result->completed_calls, result->failed_calls,
      result->failed_calls == 0 ? "yes" : "no", result->reload_trigger_call,
      result->first_changed_call, result->post_reload_channels,
      result->post_reload_algo, result->post_reload_proto);
  return 0;
}

static int test_adaptive_policy_curve(const char *plugin_path,
                                      struct adaptive_curve_result *result) {
  const struct policy_case policy = {
      "adaptive_channels", NCCL_POLICY_TEST_ADAPTIVE_CHANNELS_BPF_PATH,
      "strict"};
  const uint64_t total_iterations = 2ull * (uint64_t)kAdaptivePhaseIterations;
  const size_t expected_samples = total_iterations / kAdaptiveSampleStride;
  const size_t boundary_index =
      (size_t)(kAdaptivePhaseIterations / kAdaptiveSampleStride) - 1;
  struct plugin_session session;
  struct nccl_policy_telemetry_key key = {
      .coll_type = NCCL_POLICY_COLL_ALLREDUCE,
      .n_nodes = 1,
  };
  int telemetry_map_fd = -1;

  memset(result, 0, sizeof(*result));

  if (expected_samples > adaptive_curve_result::kMaxSamples)
    return failf("adaptive curve sample budget exceeded");
  if (open_plugin_session(&session, plugin_path, &policy, 8, 1) != 0)
    return -1;
  if (!session.debug_get_map_fd || !session.map_update_elem ||
      !session.map_lookup_elem || !session.debug_set_synthetic_telemetry) {
    close_plugin_session(&session);
    return failf("adaptive curve test requires debug map + telemetry hooks");
  }

  telemetry_map_fd =
      session.debug_get_map_fd(session.plugin_context, "telemetry_map");
  if (telemetry_map_fd < 0) {
    close_plugin_session(&session);
    return failf("adaptive curve test could not find telemetry_map");
  }

  for (size_t sample_idx = 0; sample_idx < expected_samples; ++sample_idx) {
    const uint32_t phase_high = sample_idx >= expected_samples / 2 ? 1u : 0u;
    const uint64_t seeded_latency_ns = phase_high ? 100 : 200;
    const uint64_t current_latency_ns = phase_high ? 10000 : 100;
    const struct nccl_policy_telemetry_value *current =
        lookup_telemetry_map(&session, telemetry_map_fd, &key);
    struct nccl_policy_telemetry_value seeded = {
        .last_latency_ns = seeded_latency_ns,
        .avg_latency_ns = seeded_latency_ns,
        .p99_latency_ns = seeded_latency_ns,
        .last_n_bytes = 1u << 20,
        .samples = current ? current->samples : 1,
        .recommended_channels = current ? current->recommended_channels : 4,
    };

    if (seed_telemetry_map(&session, telemetry_map_fd, &key, &seeded) != 0) {
      close_plugin_session(&session);
      return -1;
    }
    if (set_synthetic_telemetry(&session, current_latency_ns,
                                current_latency_ns, current_latency_ns,
                                1) != 0) {
      close_plugin_session(&session);
      return -1;
    }

    for (size_t iter = 0; iter < kAdaptiveSampleStride; ++iter) {
      struct decision_result decision = {-1, -1, -1};
      if (run_policy_once(&session, ncclFuncAllReduce, 1u << 20, 1, 0, 1,
                          &decision) != 0) {
        close_plugin_session(&session);
        return -1;
      }
    }

    current = lookup_telemetry_map(&session, telemetry_map_fd, &key);
    if (!current) {
      close_plugin_session(&session);
      return failf("adaptive curve test lost telemetry state");
    }

    result->calls[sample_idx] = (sample_idx + 1) * kAdaptiveSampleStride;
    result->channels[sample_idx] = current->recommended_channels;
    result->map_channels[sample_idx] = current->recommended_channels;
    result->sample_count++;
  }

  if (set_synthetic_telemetry(&session, 0, 0, 0, 0) != 0) {
    close_plugin_session(&session);
    return -1;
  }

  close_plugin_session(&session);

  result->boundary_before_channels = result->channels[boundary_index];
  result->boundary_after_channels = result->channels[boundary_index + 1];
  if (result->boundary_before_channels <= 4 ||
      result->boundary_after_channels >= result->boundary_before_channels) {
    return failf("adaptive curve did not show increase then decrease");
  }

  printf("adaptive curve boundary: before=%u after=%u samples=%zu\n",
         result->boundary_before_channels, result->boundary_after_channels,
         result->sample_count);
  for (size_t i = 0; i < result->sample_count; ++i) {
    printf("adaptive sample: call=%" PRIu64 " phase=%s channels=%u"
           " map_channels=%u\n",
           result->calls[i],
           result->calls[i] <= kAdaptivePhaseIterations ? "low" : "high",
           result->channels[i], result->map_channels[i]);
  }
  return 0;
}

int main(int argc, char **argv) {
  const char *plugin_path = argc > 1 ? argv[1] : NCCL_POLICY_TEST_PLUGIN_PATH;
  const struct policy_case strict_policies[] = {
      {"noop", NCCL_POLICY_TEST_NOOP_BPF_PATH, "strict"},
      {"size_aware", NCCL_POLICY_TEST_SIZE_AWARE_BPF_PATH, "strict"},
      {"size_aware_v2", NCCL_POLICY_TEST_SIZE_AWARE_V2_BPF_PATH, "strict"},
      {"lookup_only", NCCL_POLICY_TEST_LOOKUP_ONLY_BPF_PATH, "strict"},
      {"lookup_update", NCCL_POLICY_TEST_LOOKUP_UPDATE_BPF_PATH, "strict"},
      {"adaptive_channels", NCCL_POLICY_TEST_ADAPTIVE_CHANNELS_BPF_PATH,
       "strict"},
      {"slo_enforcer", NCCL_POLICY_TEST_SLO_ENFORCER_BPF_PATH, "strict"},
  };
  const struct policy_case benchmark_policies[] = {
      {"noop", NCCL_POLICY_TEST_NOOP_BPF_PATH, "strict"},
      {"size_aware_v2", NCCL_POLICY_TEST_SIZE_AWARE_V2_BPF_PATH, "strict"},
      {"lookup_only", NCCL_POLICY_TEST_LOOKUP_ONLY_BPF_PATH, "strict"},
      {"lookup_update", NCCL_POLICY_TEST_LOOKUP_UPDATE_BPF_PATH, "strict"},
      {"adaptive_channels", NCCL_POLICY_TEST_ADAPTIVE_CHANNELS_BPF_PATH,
       "strict"},
      {"slo_enforcer", NCCL_POLICY_TEST_SLO_ENFORCER_BPF_PATH, "strict"},
  };
  struct benchmark_result native_result = {0};
  struct benchmark_result policy_result = {0};
  struct hot_reload_result hot_reload_result = {0};
  struct adaptive_curve_result adaptive_curve_result = {0};
  uint64_t *samples = (uint64_t *)calloc(kIterations, sizeof(*samples));
  size_t i;

  if (!samples)
    return failf("failed to allocate benchmark samples");

  if (test_verifier_acceptance(plugin_path, strict_policies,
                               sizeof(strict_policies) /
                                   sizeof(strict_policies[0])) != 0) {
    free(samples);
    return 1;
  }

  if (test_size_aware_policies(plugin_path) != 0) {
    free(samples);
    return 1;
  }

  if (test_adaptive_channels_map_state(plugin_path) != 0) {
    free(samples);
    return 1;
  }

  if (test_verifier_rejection(plugin_path) != 0) {
    free(samples);
    return 1;
  }

  if (benchmark_native_size_aware(samples, kIterations, &native_result) != 0) {
    free(samples);
    return 1;
  }

  printf("native baseline ns: p50=%" PRIu64 " p99=%" PRIu64 " max=%" PRIu64
         " channels=%d algo=%d proto=%d\n",
         native_result.p50, native_result.p99, native_result.max,
         native_result.last_channels, native_result.last_algo,
         native_result.last_proto);

  for (i = 0; i < sizeof(benchmark_policies) / sizeof(benchmark_policies[0]);
       ++i) {
    if (benchmark_policy(plugin_path, &benchmark_policies[i], samples,
                         kIterations, &policy_result) != 0) {
      free(samples);
      return 1;
    }

    printf("%s (%s, verify=%s) ns: p50=%" PRIu64 " p99=%" PRIu64 " max=%" PRIu64
           " delta_p50=%" PRId64 " delta_p99=%" PRId64
           " channels=%d algo=%d proto=%d\n",
           policy_result.name, policy_result.path, policy_result.verify_mode,
           policy_result.p50, policy_result.p99, policy_result.max,
           delta_u64(policy_result.p50, native_result.p50),
           delta_u64(policy_result.p99, native_result.p99),
           policy_result.last_channels, policy_result.last_algo,
           policy_result.last_proto);
  }

  if (test_hot_reload_latency(plugin_path, &hot_reload_result) != 0) {
    free(samples);
    return 1;
  }

  if (test_adaptive_policy_curve(plugin_path, &adaptive_curve_result) != 0) {
    free(samples);
    return 1;
  }

  free(samples);
  return 0;
}
