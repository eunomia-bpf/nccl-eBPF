#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif

#include <dlfcn.h>
#include <errno.h>
#include <inttypes.h>
#include <linux/bpf.h>
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "native_baseline.h"
#include "nccl_tuner.h"
#include "policy_action.h"
#include "policy_context.h"
#include "policy_maps.h"
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
typedef long (*bpftime_map_update_elem_fn)(int fd, const void *key,
                                           const void *value, uint64_t flags);
typedef const void *(*bpftime_map_lookup_elem_fn)(int fd, const void *key);

struct plugin_session {
  void *handle;
  const ncclTuner_v5_t *plugin;
  void *plugin_context;
  plugin_debug_get_map_fd_fn debug_get_map_fd;
  bpftime_map_update_elem_fn map_update_elem;
  bpftime_map_lookup_elem_fn map_lookup_elem;
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
  result->name = "native_size_aware_v2";
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

int main(int argc, char **argv) {
  const char *plugin_path = argc > 1 ? argv[1] : NCCL_POLICY_TEST_PLUGIN_PATH;
  const struct policy_case strict_policies[] = {
      {"noop", NCCL_POLICY_TEST_NOOP_BPF_PATH, "strict"},
      {"size_aware", NCCL_POLICY_TEST_SIZE_AWARE_BPF_PATH, "strict"},
      {"size_aware_v2", NCCL_POLICY_TEST_SIZE_AWARE_V2_BPF_PATH, "strict"},
      {"adaptive_channels", NCCL_POLICY_TEST_ADAPTIVE_CHANNELS_BPF_PATH,
       "strict"},
      {"slo_enforcer", NCCL_POLICY_TEST_SLO_ENFORCER_BPF_PATH, "strict"},
  };
  struct benchmark_result native_result = {0};
  struct benchmark_result policy_result = {0};
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

  for (i = 0; i < sizeof(strict_policies) / sizeof(strict_policies[0]); ++i) {
    if (benchmark_policy(plugin_path, &strict_policies[i], samples, kIterations,
                         &policy_result) != 0) {
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

  free(samples);
  return 0;
}
