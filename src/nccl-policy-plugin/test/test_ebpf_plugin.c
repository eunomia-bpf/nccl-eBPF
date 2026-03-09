#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif

#include <atomic>
#include <ctype.h>
#include <dlfcn.h>
#include <errno.h>
#include <functional>
#include <inttypes.h>
#include <limits>
#include <linux/bpf.h>
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string>
#include <string.h>
#include <thread>
#include <time.h>
#include <unistd.h>
#include <vector>

#include "native_baseline.h"
#include "nccl_tuner.h"
#include "nccl_profiler.h"
#include "policy_action.h"
#include "policy_context.h"
#include "policy_maps.h"
#include "policy_test_paths.h"

enum { kIterations = 1000000 };
enum { kWarmupIterations = 10000 };
enum { kDifferentiatedIterations = 100000 };
enum { kAdaptivePhaseIterations = 100000 };
enum { kAdaptiveSampleStride = 10000 };
enum { kHotReloadIterations = 400000 };
enum { kHotReloadTriggerIteration = 200000 };

struct policy_case {
  const char *name;
  const char *path;
  const char *verify_mode;
};

struct verifier_case {
  const char *name;
  const char *path;
  const char *verify_mode;
  const char *error_type;
  int expected_accept;
};

struct verifier_case_result {
  const char *name;
  const char *path;
  const char *error_type;
  int expected_accept;
  int accepted;
  int init_rc;
  std::string verifier_detail;
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
  const ncclProfiler_v6_t *profiler;
  void *plugin_context;
  void *profiler_context;
  int profiler_activation_mask;
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
  size_t old_policy_calls;
  size_t new_policy_calls;
  size_t unexpected_call_count;
  int good_reload_rc;
  int bad_reload_rc;
  int post_reload_channels;
  int post_reload_algo;
  int post_reload_proto;
  int preserved_channels_after_bad_reload;
  int preserved_algo_after_bad_reload;
  int preserved_proto_after_bad_reload;
};

struct adaptive_curve_result {
  enum { kMaxSamples = 100 };
  size_t sample_count;
  uint64_t calls[kMaxSamples];
  uint64_t injected_latency_ns[kMaxSamples];
  uint32_t channels[kMaxSamples];
  uint32_t map_channels[kMaxSamples];
  uint32_t phase_ids[kMaxSamples];
  uint32_t phase_end_channels[3];
};

static void close_plugin_session(struct plugin_session *session);

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

static int decision_matches(const struct decision_result *decision,
                            int channels, int algo, int proto) {
  return decision->channels == channels && decision->algo == algo &&
         decision->proto == proto;
}

static const char *collective_name(ncclFunc_t coll_type) {
  switch (coll_type) {
  case ncclFuncBroadcast:
    return "Broadcast";
  case ncclFuncReduce:
    return "Reduce";
  case ncclFuncAllGather:
    return "AllGather";
  case ncclFuncReduceScatter:
    return "ReduceScatter";
  case ncclFuncAllReduce:
    return "AllReduce";
  default:
    return "Unknown";
  }
}

static int policy_coll_type_from_nccl_func_test(ncclFunc_t coll_type,
                                                uint32_t *policy_coll_type) {
  if (!policy_coll_type)
    return 0;

  switch (coll_type) {
  case ncclFuncBroadcast:
    *policy_coll_type = NCCL_POLICY_COLL_BROADCAST;
    return 1;
  case ncclFuncReduce:
    *policy_coll_type = NCCL_POLICY_COLL_REDUCE;
    return 1;
  case ncclFuncAllGather:
    *policy_coll_type = NCCL_POLICY_COLL_ALLGATHER;
    return 1;
  case ncclFuncReduceScatter:
    *policy_coll_type = NCCL_POLICY_COLL_REDUCESCATTER;
    return 1;
  case ncclFuncAllReduce:
    *policy_coll_type = NCCL_POLICY_COLL_ALLREDUCE;
    return 1;
  default:
    return 0;
  }
}

static const char *algo_name(int algo) {
  switch (algo) {
  case NCCL_ALGO_TREE:
    return "TREE";
  case NCCL_ALGO_RING:
    return "RING";
  case -1:
    return "-";
  default:
    return "UNKNOWN";
  }
}

static const char *proto_name(int proto) {
  switch (proto) {
  case NCCL_PROTO_LL:
    return "LL";
  case NCCL_PROTO_LL128:
    return "LL128";
  case NCCL_PROTO_SIMPLE:
    return "SIMPLE";
  case -1:
    return "-";
  default:
    return "UNKNOWN";
  }
}

static const char *phase_name(uint32_t phase_id) {
  switch (phase_id) {
  case 0:
    return "baseline";
  case 1:
    return "contention";
  case 2:
    return "recovery";
  default:
    return "unknown";
  }
}

static int read_fd_to_string(int fd, std::string *output) {
  char buffer[4096];
  ssize_t nread;

  output->clear();
  while ((nread = read(fd, buffer, sizeof(buffer))) > 0)
    output->append(buffer, (size_t)nread);
  if (nread < 0)
    return failf("read failed while capturing stderr: %s", strerror(errno));
  return 0;
}

static int capture_stderr(const std::function<void(void)> &fn,
                          std::string *captured) {
  int pipe_fds[2] = {-1, -1};
  int saved_stderr = -1;
  int rc = 0;

  if (pipe(pipe_fds) != 0)
    return failf("pipe failed while capturing stderr: %s", strerror(errno));

  saved_stderr = dup(STDERR_FILENO);
  if (saved_stderr < 0) {
    close(pipe_fds[0]);
    close(pipe_fds[1]);
    return failf("dup failed while capturing stderr: %s", strerror(errno));
  }

  fflush(stderr);
  if (dup2(pipe_fds[1], STDERR_FILENO) < 0) {
    rc = failf("dup2 failed while capturing stderr: %s", strerror(errno));
    close(pipe_fds[0]);
    close(pipe_fds[1]);
    close(saved_stderr);
    return rc;
  }
  close(pipe_fds[1]);
  pipe_fds[1] = -1;

  fn();

  fflush(stderr);
  if (dup2(saved_stderr, STDERR_FILENO) < 0)
    rc = failf("failed to restore stderr: %s", strerror(errno));
  close(saved_stderr);

  if (pipe_fds[0] >= 0) {
    std::string local_capture;
    if (read_fd_to_string(pipe_fds[0], &local_capture) != 0)
      rc = -1;
    close(pipe_fds[0]);
    if (captured)
      *captured = std::move(local_capture);
  } else if (captured) {
    captured->clear();
  }

  return rc;
}

static std::string collapse_whitespace(const std::string &text) {
  std::string collapsed;
  int last_was_space = 1;

  for (char ch : text) {
    if (isspace((unsigned char)ch)) {
      if (!last_was_space) {
        collapsed.push_back(' ');
        last_was_space = 1;
      }
      continue;
    }
    collapsed.push_back(ch);
    last_was_space = 0;
  }

  if (!collapsed.empty() && collapsed.back() == ' ')
    collapsed.pop_back();
  return collapsed;
}

static std::string summarize_verifier_detail(const std::string &captured) {
  const std::string needle = "verifier rejected";
  std::string summary;
  size_t pos = captured.find(needle);

  if (pos == std::string::npos)
    return "-";

  summary = collapse_whitespace(captured.substr(pos));
  if (summary.size() > 200) {
    summary.resize(197);
    summary += "...";
  }
  return summary;
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

static int open_plugin_session_for_comm(struct plugin_session *session,
                                        const char *plugin_path,
                                        const struct policy_case *policy,
                                        uint64_t comm_id, size_t n_ranks,
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

  if (session->plugin->init(&session->plugin_context, comm_id, n_ranks,
                            n_nodes,
                            no_op_logger, NULL, NULL) != ncclSuccess) {
    dlclose(session->handle);
    memset(session, 0, sizeof(*session));
    return failf("plugin init failed for %s (verify=%s)", policy->name,
                 policy->verify_mode);
  }

  session->profiler =
      (const ncclProfiler_v6_t *)dlsym(session->handle, "ncclProfiler_v6");
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

static int open_plugin_session(struct plugin_session *session,
                               const char *plugin_path,
                               const struct policy_case *policy, size_t n_ranks,
                               size_t n_nodes) {
  return open_plugin_session_for_comm(session, plugin_path, policy, 0, n_ranks,
                                      n_nodes);
}

static int open_mixed_plugin_session_for_comm(struct plugin_session *session,
                                              const char *plugin_path,
                                              const struct policy_case *policy,
                                              uint64_t comm_id,
                                              size_t n_ranks, size_t n_nodes,
                                              int rank) {
  if (open_plugin_session_for_comm(session, plugin_path, policy, comm_id,
                                   n_ranks, n_nodes) != 0)
    return -1;
  if (!session->profiler) {
    close_plugin_session(session);
    return failf("dlsym failed for ncclProfiler_v6: %s", dlerror());
  }
  if (session->profiler->init(&session->profiler_context, comm_id,
                              &session->profiler_activation_mask, "test-comm",
                              (int)n_nodes, (int)n_ranks, rank,
                              no_op_logger) != ncclSuccess) {
    close_plugin_session(session);
    return failf("profiler init failed for %s", policy->name);
  }
  return 0;
}

static int open_mixed_plugin_session(struct plugin_session *session,
                                     const char *plugin_path,
                                     const struct policy_case *policy,
                                     size_t n_ranks, size_t n_nodes, int rank) {
  return open_mixed_plugin_session_for_comm(session, plugin_path, policy, 0,
                                            n_ranks, n_nodes, rank);
}

static void close_plugin_session(struct plugin_session *session) {
  if (session->profiler && session->profiler_context)
    session->profiler->finalize(session->profiler_context);
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

static int seed_config_map(struct plugin_session *session, int config_map_fd,
                           const struct nccl_policy_config_key *key,
                           const struct nccl_policy_config_value *value) {
  if (!session->map_update_elem)
    return failf("bpftime_map_update_elem is unavailable");
  if (session->map_update_elem(config_map_fd, key, value, BPF_ANY) != 0)
    return failf("failed to seed config_map");
  return 0;
}

static const struct nccl_policy_config_value *
lookup_config_map(struct plugin_session *session, int config_map_fd,
                  const struct nccl_policy_config_key *key) {
  if (!session->map_lookup_elem)
    return NULL;
  return (const struct nccl_policy_config_value *)session->map_lookup_elem(
      config_map_fd, key);
}

static int seed_uniform_config_map(struct plugin_session *session,
                                   uint64_t target_p99_ns,
                                   uint32_t min_channels,
                                   uint32_t max_channels,
                                   uint32_t aggressiveness_step) {
  int config_map_fd = -1;

  if (!session->debug_get_map_fd || !session->map_update_elem ||
      !session->map_lookup_elem) {
    return failf("config_map test requires debug map helpers");
  }

  config_map_fd = session->debug_get_map_fd(session->plugin_context,
                                            "config_map");
  if (config_map_fd < 0)
    return failf("unable to find config_map");

  for (uint32_t coll_type = 0; coll_type <= NCCL_POLICY_COLL_ALLREDUCE;
       ++coll_type) {
    const struct nccl_policy_config_key key = {.coll_type = coll_type};
    const struct nccl_policy_config_value value = {
        .target_p99_ns = target_p99_ns,
        .min_channels = min_channels,
        .max_channels = max_channels,
        .aggressiveness_step = aggressiveness_step,
    };

    if (seed_config_map(session, config_map_fd, &key, &value) != 0)
      return -1;
  }

  return 0;
}

static void compute_native_size_aware_v2_decision(ncclFunc_t coll_type,
                                                  size_t n_bytes,
                                                  int initial_channels,
                                                  struct decision_result
                                                      *decision) {
  struct nccl_policy_ctx ctx = {};

  if (!decision)
    return;

  ctx.n_bytes = n_bytes;
  if (!policy_coll_type_from_nccl_func_test(coll_type, &ctx.coll_type)) {
    *decision = {-1, -1, -1};
    return;
  }
  ctx.num_pipe_ops = 1;
  ctx.n_ranks = 8;
  ctx.n_nodes = 1;
  ctx.current_channels = (uint32_t)initial_channels;
  decode_action(nccl_native_size_aware_v2(&ctx), decision);
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

static int emit_kernel_timed_collective(struct plugin_session *session,
                                        const char *func_name, size_t n_bytes,
                                        uint64_t ch0_start_ns,
                                        uint64_t ch0_stop_ns,
                                        uint64_t ch1_start_ns,
                                        uint64_t ch1_stop_ns) {
  ncclProfilerEventDescr_v6_t coll_descr = {};
  ncclProfilerEventDescr_v6_t kernel_descr = {};
  ncclProfilerEventStateArgs_v6_t kernel_args = {};
  void *coll_handle = NULL;
  void *kernel_handle = NULL;

  if (!session->profiler || !session->profiler_context)
    return failf("mixed session is missing profiler context");
  if (n_bytes % sizeof(float) != 0)
    return failf("kernel timing helper expects float-aligned byte counts");

  coll_descr.type = ncclProfileColl;
  coll_descr.rank = 0;
  coll_descr.coll.func = func_name;
  coll_descr.coll.count = n_bytes / sizeof(float);
  coll_descr.coll.datatype = "ncclFloat32";
  coll_descr.coll.nChannels = 2;
  if (session->profiler->startEvent(session->profiler_context, &coll_handle,
                                    &coll_descr) != ncclSuccess) {
    return failf("profiler coll startEvent failed");
  }
  if (session->profiler->stopEvent(coll_handle) != ncclSuccess)
    return failf("profiler coll stopEvent failed");

  kernel_descr.type = ncclProfileKernelCh;
  kernel_descr.parentObj = coll_handle;
  kernel_descr.kernelCh.channelId = 0;
  kernel_descr.kernelCh.pTimer = ch0_start_ns;
  if (session->profiler->startEvent(session->profiler_context, &kernel_handle,
                                    &kernel_descr) != ncclSuccess) {
    return failf("profiler kernel channel 0 startEvent failed");
  }
  kernel_args.kernelCh.pTimer = ch0_stop_ns;
  if (session->profiler->recordEventState(
          kernel_handle, ncclProfilerKernelChStop, &kernel_args) !=
      ncclSuccess) {
    return failf("profiler kernel channel 0 recordEventState failed");
  }
  if (session->profiler->stopEvent(kernel_handle) != ncclSuccess)
    return failf("profiler kernel channel 0 stopEvent failed");

  kernel_handle = NULL;
  kernel_descr.kernelCh.channelId = 1;
  kernel_descr.kernelCh.pTimer = ch1_start_ns;
  if (session->profiler->startEvent(session->profiler_context, &kernel_handle,
                                    &kernel_descr) != ncclSuccess) {
    return failf("profiler kernel channel 1 startEvent failed");
  }
  kernel_args.kernelCh.pTimer = ch1_stop_ns;
  if (session->profiler->recordEventState(
          kernel_handle, ncclProfilerKernelChStop, &kernel_args) !=
      ncclSuccess) {
    return failf("profiler kernel channel 1 recordEventState failed");
  }
  if (session->profiler->stopEvent(kernel_handle) != ncclSuccess)
    return failf("profiler kernel channel 1 stopEvent failed");

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

static int probe_policy_verdict(const char *plugin_path,
                                const struct verifier_case *policy,
                                struct verifier_case_result *result) {
  void *handle = NULL;
  const ncclTuner_v5_t *plugin = NULL;
  void *plugin_context = NULL;
  std::string captured;
  std::string dlopen_error;
  std::string dlsym_error;
  int init_rc = ncclInternalError;
  int dlopen_ok = 0;
  int dlsym_ok = 0;
  int init_called = 0;

  result->name = policy->name;
  result->path = policy->path;
  result->error_type = policy->error_type;
  result->expected_accept = policy->expected_accept;
  result->accepted = 0;
  result->init_rc = ncclInternalError;
  result->verifier_detail = "-";

  if (setenv("NCCL_POLICY_BPF_PATH", policy->path, 1) != 0)
    return failf("setenv failed for NCCL_POLICY_BPF_PATH: %s", strerror(errno));
  if (setenv("NCCL_POLICY_VERIFY_MODE", policy->verify_mode, 1) != 0)
    return failf("setenv failed for NCCL_POLICY_VERIFY_MODE: %s",
                 strerror(errno));

  if (capture_stderr(
          [&]() {
            handle = dlopen(plugin_path, RTLD_NOW | RTLD_LOCAL);
            if (!handle) {
              dlopen_error = dlerror() ? dlerror() : "unknown";
              return;
            }
            dlopen_ok = 1;

            plugin = (const ncclTuner_v5_t *)dlsym(handle,
                                                   NCCL_TUNER_PLUGIN_SYMBOL);
            if (!plugin) {
              dlsym_error = dlerror() ? dlerror() : "unknown";
              return;
            }
            dlsym_ok = 1;

            init_rc = plugin->init(&plugin_context, 0, 8, 1, no_op_logger,
                                   NULL, NULL);
            init_called = 1;
            if (init_rc == ncclSuccess && plugin_context) {
              plugin->finalize(plugin_context);
              plugin_context = NULL;
            }

            dlclose(handle);
            handle = NULL;
          },
          &captured) != 0) {
    if (handle)
      dlclose(handle);
    return -1;
  }

  if (handle)
    dlclose(handle);
  if (!dlopen_ok)
    return failf("dlopen failed for %s: %s", plugin_path, dlopen_error.c_str());
  if (!dlsym_ok)
    return failf("dlsym failed for %s: %s", NCCL_TUNER_PLUGIN_SYMBOL,
                 dlsym_error.c_str());
  if (!init_called)
    return failf("plugin init was not attempted for %s", policy->name);

  result->accepted = init_rc == ncclSuccess;
  result->init_rc = init_rc;
  result->verifier_detail = summarize_verifier_detail(captured);
  return 0;
}

static int test_verifier_matrix(const char *plugin_path,
                                const struct verifier_case *policies,
                                size_t policy_count,
                                std::vector<struct verifier_case_result>
                                    *results_out) {
  std::vector<struct verifier_case_result> results;
  size_t i;
  int failed = 0;

  results.reserve(policy_count);
  for (i = 0; i < policy_count; ++i) {
    struct verifier_case_result result = {};

    if (probe_policy_verdict(plugin_path, &policies[i], &result) != 0)
      return -1;
    if (result.accepted != result.expected_accept)
      failed = 1;
    results.push_back(std::move(result));
  }

  printf("verifier matrix:\n");
  printf("| program | error_type | verifier_verdict | expected | pass/fail |\n");
  printf("| --- | --- | --- | --- | --- |\n");
  for (const auto &result : results) {
    const char *verdict = result.accepted ? "ACCEPTED" : "REJECTED";
    const char *expected = result.expected_accept ? "ACCEPTED" : "REJECTED";
    const char *pass_fail =
        result.accepted == result.expected_accept ? "PASS" : "FAIL";

    printf("| %s | %s | %s | %s | %s |\n", result.name, result.error_type,
           verdict, expected, pass_fail);
    if (result.verifier_detail != "-") {
      printf("verifier detail: %s => %s\n", result.name,
             result.verifier_detail.c_str());
    }
  }

  if (results_out)
    *results_out = results;

  if (failed)
    return failf("verifier matrix contained unexpected verdicts");

  printf("verifier matrix: PASS (%zu programs)\n", policy_count);
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
      .last_latency_ns = 240,
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
  if (observed->samples != seeded.samples ||
      observed->recommended_channels != seeded.recommended_channels ||
      observed->last_n_bytes != seeded.last_n_bytes ||
      observed->last_latency_ns != seeded.last_latency_ns) {
    close_plugin_session(&session);
    return failf("adaptive_channels telemetry state mismatch: samples=%u "
                 "recommended_channels=%u last_n_bytes=%" PRIu64
                 " last_latency_ns=%" PRIu64,
                 observed->samples, observed->recommended_channels,
                 observed->last_n_bytes, observed->last_latency_ns);
  }

  close_plugin_session(&session);
  printf("adaptive_channels map state: PASS\n");
  return 0;
}

static int test_profiler_telemetry_bridge(const char *plugin_path) {
  const struct policy_case policy = {
      "adaptive_channels", NCCL_POLICY_TEST_ADAPTIVE_CHANNELS_BPF_PATH,
      "strict"};
  struct plugin_session session;
  struct nccl_policy_telemetry_key key = {
      .coll_type = NCCL_POLICY_COLL_ALLREDUCE,
      .n_nodes = 1,
  };
  const struct nccl_policy_telemetry_value *observed = NULL;
  struct decision_result decision = {-1, -1, -1};
  int telemetry_map_fd = -1;
  uint64_t observed_last_latency_ns = 0;
  uint64_t observed_avg_latency_ns = 0;
  uint32_t observed_samples = 0;
  uint32_t observed_channels = 0;

  if (open_mixed_plugin_session(&session, plugin_path, &policy, 2, 1, 0) != 0)
    return -1;

  if (!session.debug_get_map_fd || !session.map_lookup_elem) {
    close_plugin_session(&session);
    return failf("profiler bridge test requires debug map helpers");
  }

  telemetry_map_fd =
      session.debug_get_map_fd(session.plugin_context, "telemetry_map");
  if (telemetry_map_fd < 0) {
    close_plugin_session(&session);
    return failf("profiler bridge test could not find telemetry_map");
  }

  if (run_policy_once(&session, ncclFuncAllReduce, 1u << 20, 1, 0, 1,
                      &decision) != 0) {
    close_plugin_session(&session);
    return -1;
  }
  if (decision.channels != 8) {
    close_plugin_session(&session);
    return failf("first adaptive_channels decision mismatch: got %d expected 8",
                 decision.channels);
  }

  if (emit_kernel_timed_collective(&session, "AllReduce", 1u << 20, 1000, 1400,
                                   1200, 1720) != 0) {
    close_plugin_session(&session);
    return -1;
  }

  observed = lookup_telemetry_map(&session, telemetry_map_fd, &key);
  if (observed) {
    observed_last_latency_ns = observed->last_latency_ns;
    observed_avg_latency_ns = observed->avg_latency_ns;
    observed_samples = observed->samples;
    observed_channels = observed->recommended_channels;
  }
  if (!observed || observed->last_latency_ns != 520 ||
      observed->avg_latency_ns != 520 || observed->samples != 1 ||
      observed->recommended_channels != 8) {
    close_plugin_session(&session);
    return failf("profiler bridge first sample mismatch: last=%" PRIu64
                 " avg=%" PRIu64 " samples=%u channels=%u",
                 observed_last_latency_ns, observed_avg_latency_ns,
                 observed_samples, observed_channels);
  }

  if (run_policy_once(&session, ncclFuncAllReduce, 1u << 20, 1, 0, 8,
                      &decision) != 0) {
    close_plugin_session(&session);
    return -1;
  }
  if (decision.channels != 9) {
    close_plugin_session(&session);
    return failf(
        "adaptive_channels did not react to profiler-fed low latency: got %d "
        "expected 9",
        decision.channels);
  }

  if (emit_kernel_timed_collective(&session, "AllReduce", 1u << 20, 2000, 2720,
                                   2100, 3020) != 0) {
    close_plugin_session(&session);
    return -1;
  }

  observed = lookup_telemetry_map(&session, telemetry_map_fd, &key);
  if (observed) {
    observed_last_latency_ns = observed->last_latency_ns;
    observed_samples = observed->samples;
    observed_channels = observed->recommended_channels;
  }
  if (!observed || observed->last_latency_ns != 920 || observed->samples != 2 ||
      observed->recommended_channels != 9) {
    close_plugin_session(&session);
    return failf("profiler bridge second sample mismatch: last=%" PRIu64
                 " samples=%u channels=%u",
                 observed_last_latency_ns, observed_samples, observed_channels);
  }

  if (run_policy_once(&session, ncclFuncAllReduce, 1u << 20, 1, 0, 9,
                      &decision) != 0) {
    close_plugin_session(&session);
    return -1;
  }
  if (decision.channels != 8) {
    close_plugin_session(&session);
    return failf(
        "adaptive_channels did not react to profiler-fed high latency: got %d "
        "expected 8",
        decision.channels);
  }

  close_plugin_session(&session);
  printf("profiler telemetry bridge: PASS\n");
  return 0;
}

static int test_multi_communicator_differentiation(const char *plugin_path) {
  const struct policy_case policy = {
      "slo_enforcer", NCCL_POLICY_TEST_SLO_ENFORCER_BPF_PATH, "strict"};
  const size_t sizes[] = {1024, 32768, 262144};
  struct plugin_session latency_sensitive = {};
  struct plugin_session throughput = {};
  struct decision_result latency_results[sizeof(sizes) / sizeof(sizes[0])];
  struct decision_result throughput_results[sizeof(sizes) / sizeof(sizes[0])];
  int rc = -1;

  memset(latency_results, 0, sizeof(latency_results));
  memset(throughput_results, 0, sizeof(throughput_results));

  if (open_plugin_session_for_comm(&latency_sensitive, plugin_path, &policy, 1,
                                   8, 1) != 0)
    goto done;
  if (open_plugin_session_for_comm(&throughput, plugin_path, &policy, 2, 8, 1) !=
      0)
    goto done;

  if (seed_uniform_config_map(&latency_sensitive, 1000000ull, 1, 8, 1) != 0)
    goto done;
  if (seed_uniform_config_map(&throughput, 10000000ull, 1, 8, 1) != 0)
    goto done;

  if (set_synthetic_telemetry(&latency_sensitive, 5000000ull, 5000000ull,
                              5000000ull, 1) != 0)
    goto done;
  if (set_synthetic_telemetry(&throughput, 5000000ull, 5000000ull, 5000000ull,
                              1) != 0)
    goto done;

  for (size_t i = 0; i < kDifferentiatedIterations; ++i) {
    const size_t n_bytes = sizes[i % (sizeof(sizes) / sizeof(sizes[0]))];
    struct decision_result ignored = {-1, -1, -1};

    if (run_policy_once(&latency_sensitive, ncclFuncAllReduce, n_bytes, 1, 0, 1,
                        &ignored) != 0)
      goto done;
    if (run_policy_once(&throughput, ncclFuncAllReduce, n_bytes, 1, 0, 1,
                        &ignored) != 0)
      goto done;
  }

  for (size_t i = 0; i < sizeof(sizes) / sizeof(sizes[0]); ++i) {
    if (run_policy_once(&latency_sensitive, ncclFuncAllReduce, sizes[i], 1, 0,
                        1, &latency_results[i]) != 0)
      goto done;
    if (run_policy_once(&throughput, ncclFuncAllReduce, sizes[i], 1, 0, 1,
                        &throughput_results[i]) != 0)
      goto done;
    if (latency_results[i].algo == throughput_results[i].algo &&
        latency_results[i].proto == throughput_results[i].proto &&
        latency_results[i].channels == throughput_results[i].channels) {
      failf("multi-communicator differentiation failed for bytes=%zu", sizes[i]);
      goto done;
    }
  }

  {
    int latency_cfg_fd =
        latency_sensitive.debug_get_map_fd(latency_sensitive.plugin_context,
                                           "config_map");
    int throughput_cfg_fd =
        throughput.debug_get_map_fd(throughput.plugin_context, "config_map");
    const struct nccl_policy_config_key allreduce_key = {
        .coll_type = NCCL_POLICY_COLL_ALLREDUCE,
    };
    const struct nccl_policy_config_value *latency_cfg =
        lookup_config_map(&latency_sensitive, latency_cfg_fd, &allreduce_key);
    const struct nccl_policy_config_value *throughput_cfg =
        lookup_config_map(&throughput, throughput_cfg_fd, &allreduce_key);

    if (!latency_cfg || !throughput_cfg) {
      failf("multi-communicator test could not read communicator configs back");
      goto done;
    }

    printf("multi-communicator differentiated policy:\n");
    printf("same_process=yes policy=%s calls_per_comm=%d injected_p99_ns=%d\n",
           policy.name, kDifferentiatedIterations, 5000000);
    printf("| message_bytes | comm=1 latency-sensitive target_p99=1ms | "
           "comm=2 throughput target_p99=10ms |\n");
    printf("| --- | --- | --- |\n");
    for (size_t i = 0; i < sizeof(sizes) / sizeof(sizes[0]); ++i) {
      printf("| %zu | algo=%s proto=%s channels=%d | algo=%s proto=%s "
             "channels=%d |\n",
             sizes[i], algo_name(latency_results[i].algo),
             proto_name(latency_results[i].proto),
             latency_results[i].channels, algo_name(throughput_results[i].algo),
             proto_name(throughput_results[i].proto),
             throughput_results[i].channels);
    }
    printf("config isolation: comm=1 target_p99_ns=%" PRIu64
           " comm=2 target_p99_ns=%" PRIu64 "\n",
           latency_cfg->target_p99_ns, throughput_cfg->target_p99_ns);
  }

  rc = 0;
done:
  close_plugin_session(&throughput);
  close_plugin_session(&latency_sensitive);
  if (rc == 0)
    printf("multi-communicator differentiation: PASS\n");
  return rc;
}

static int test_collective_type_coverage(const char *plugin_path) {
  const struct policy_case policy = {
      "size_aware_v2", NCCL_POLICY_TEST_SIZE_AWARE_V2_BPF_PATH, "strict"};
  const ncclFunc_t coll_types[] = {
      ncclFuncAllReduce,
      ncclFuncAllGather,
      ncclFuncReduceScatter,
      ncclFuncBroadcast,
  };
  const size_t sizes[] = {1024, 32768, 1u << 20};
  struct plugin_session session = {};
  int rc = -1;

  if (open_plugin_session(&session, plugin_path, &policy, 8, 1) != 0)
    return -1;

  printf("multi-collective coverage:\n");
  printf("| collective | message_bytes | algo | proto | channels |\n");
  printf("| --- | --- | --- | --- | --- |\n");
  for (size_t i = 0; i < sizeof(coll_types) / sizeof(coll_types[0]); ++i) {
    for (size_t j = 0; j < sizeof(sizes) / sizeof(sizes[0]); ++j) {
      struct decision_result plugin_decision = {-1, -1, -1};
      struct decision_result native_decision = {-1, -1, -1};

      if (run_policy_once(&session, coll_types[i], sizes[j], 1, 0, 1,
                          &plugin_decision) != 0)
        goto done;

      compute_native_size_aware_v2_decision(coll_types[i], sizes[j], 1,
                                            &native_decision);
      if (plugin_decision.algo != native_decision.algo ||
          plugin_decision.proto != native_decision.proto ||
          plugin_decision.channels != native_decision.channels) {
        failf("multi-collective coverage mismatch for %s bytes=%zu",
              collective_name(coll_types[i]), sizes[j]);
        goto done;
      }

      printf("| %s | %zu | %s | %s | %d |\n", collective_name(coll_types[i]),
             sizes[j], algo_name(plugin_decision.algo),
             proto_name(plugin_decision.proto), plugin_decision.channels);
    }
  }

  rc = 0;
done:
  close_plugin_session(&session);
  if (rc == 0)
    printf("multi-collective coverage: PASS\n");
  return rc;
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

static int test_hot_reload_safety(const char *plugin_path,
                                  struct hot_reload_result *result) {
  const struct policy_case initial_policy = {
      "noop", NCCL_POLICY_TEST_NOOP_BPF_PATH, "strict"};
  const size_t sentinel = std::numeric_limits<size_t>::max();
  struct plugin_session session;
  std::vector<uint64_t> call_latencies(kHotReloadIterations, 0);
  const struct decision_result unknown_decision = {-777, -777, -777};
  std::vector<struct decision_result> decisions(kHotReloadIterations,
                                                unknown_decision);
  std::atomic<size_t> calls_started(0);
  std::atomic<size_t> failed_calls(0);
  struct reload_debug_stats reload_stats = {0};
  struct decision_result preserved_decision = {-1, -1, -1};
  std::string bad_reload_capture;

  *result = hot_reload_result();
  result->first_changed_call = sentinel;
  result->good_reload_rc = -1;
  result->bad_reload_rc = -1;
  result->post_reload_channels = -1;
  result->post_reload_algo = -1;
  result->post_reload_proto = -1;
  result->preserved_channels_after_bad_reload = -1;
  result->preserved_algo_after_bad_reload = -1;
  result->preserved_proto_after_bad_reload = -1;

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
      decisions[i] = decision;
    }
  });

  std::thread reloader([&]() {
    while (calls_started.load(std::memory_order_acquire) <
           kHotReloadTriggerIteration) {
      std::this_thread::yield();
    }
    result->reload_trigger_call = calls_started.load(std::memory_order_acquire);

    result->good_reload_rc = session.debug_reload_policy(
        session.plugin_context, NCCL_POLICY_TEST_SIZE_AWARE_V2_BPF_PATH,
        &reload_stats);
  });

  caller.join();
  reloader.join();

  if (result->good_reload_rc != 0) {
    close_plugin_session(&session);
    return failf("hot reload failed");
  }
  if (result->reload_trigger_call == 0 ||
      result->reload_trigger_call > call_latencies.size()) {
    close_plugin_session(&session);
    return failf("hot reload trigger call was out of range");
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

  for (size_t i = 0; i < call_latencies.size(); ++i) {
    if (call_latencies[i] > result->max_call_latency_ns)
      result->max_call_latency_ns = call_latencies[i];
    if (call_latencies[i] > result->slow_call_threshold_ns)
      result->slow_call_count++;
  }

  for (size_t i = 0; i < decisions.size(); ++i) {
    if (decision_matches(&decisions[i], 1, -1, -1)) {
      result->old_policy_calls++;
      continue;
    }
    if (decision_matches(&decisions[i], 10, NCCL_ALGO_RING,
                         NCCL_PROTO_SIMPLE)) {
      if (result->first_changed_call == sentinel)
        result->first_changed_call = i;
      result->new_policy_calls++;
      continue;
    }
    result->unexpected_call_count++;
  }

  if (result->first_changed_call == sentinel) {
    close_plugin_session(&session);
    return failf("hot reload never switched to the replacement policy");
  }

  for (size_t i = 0; i < result->first_changed_call; ++i) {
    if (!decision_matches(&decisions[i], 1, -1, -1)) {
      close_plugin_session(&session);
      return failf("hot reload was not atomic before the transition point");
    }
  }
  for (size_t i = result->first_changed_call; i < decisions.size(); ++i) {
    if (!decision_matches(&decisions[i], 10, NCCL_ALGO_RING,
                          NCCL_PROTO_SIMPLE)) {
      close_plugin_session(&session);
      return failf("hot reload was not atomic after the transition point");
    }
  }

  result->post_reload_channels = 10;
  result->post_reload_algo = NCCL_ALGO_RING;
  result->post_reload_proto = NCCL_PROTO_SIMPLE;

  if (capture_stderr(
          [&]() {
            result->bad_reload_rc = session.debug_reload_policy(
                session.plugin_context, NCCL_POLICY_TEST_BAD_LOOKUP_BPF_PATH,
                NULL);
          },
          &bad_reload_capture) != 0) {
    close_plugin_session(&session);
    return -1;
  }
  if (result->bad_reload_rc == 0) {
    close_plugin_session(&session);
    return failf("bad hot-reload replacement unexpectedly succeeded");
  }
  if (run_policy_once(&session, ncclFuncAllReduce, 1u << 20, 1, 0, 1,
                      &preserved_decision) != 0) {
    close_plugin_session(&session);
    return -1;
  }

  result->preserved_channels_after_bad_reload = preserved_decision.channels;
  result->preserved_algo_after_bad_reload = preserved_decision.algo;
  result->preserved_proto_after_bad_reload = preserved_decision.proto;

  close_plugin_session(&session);
  if (result->failed_calls != 0 || result->unexpected_call_count != 0 ||
      result->post_reload_channels != 10 ||
      result->post_reload_algo != NCCL_ALGO_RING ||
      result->post_reload_proto != NCCL_PROTO_SIMPLE ||
      !decision_matches(&preserved_decision, 10, NCCL_ALGO_RING,
                        NCCL_PROTO_SIMPLE)) {
    return failf("hot reload correctness check failed");
  }

  result->first_changed_call += 1;
  printf(
      "hot reload safety: load=%.3f swap=%.3f total=%.3f"
      " pre_p50_ns=%" PRIu64 " pre_p99_ns=%" PRIu64 " max_call_ns=%" PRIu64
      " slow_calls=%zu threshold_ns=%" PRIu64
      " completed_calls=%zu failed_calls=%zu zero_call_loss=%s"
      " trigger_call=%zu first_changed_call=%zu old_calls=%zu new_calls=%zu"
      " unexpected_calls=%zu channels=%d algo=%d proto=%d"
      " bad_replacement_rc=%d preserved_channels=%d preserved_algo=%d"
      " preserved_proto=%d\n",
      result->reload_load_ns / 1000.0, result->reload_swap_ns / 1000.0,
      result->reload_total_ns / 1000.0, result->pre_reload_p50_ns,
      result->pre_reload_p99_ns, result->max_call_latency_ns,
      result->slow_call_count, result->slow_call_threshold_ns,
      result->completed_calls, result->failed_calls,
      result->failed_calls == 0 ? "yes" : "no", result->reload_trigger_call,
      result->first_changed_call, result->old_policy_calls,
      result->new_policy_calls, result->unexpected_call_count,
      result->post_reload_channels, result->post_reload_algo,
      result->post_reload_proto, result->bad_reload_rc,
      result->preserved_channels_after_bad_reload,
      result->preserved_algo_after_bad_reload,
      result->preserved_proto_after_bad_reload);
  if (!bad_reload_capture.empty()) {
    printf("hot reload rejected detail: %s\n",
           summarize_verifier_detail(bad_reload_capture).c_str());
  }
  return 0;
}

static int test_adaptive_policy_curve(const char *plugin_path,
                                      struct adaptive_curve_result *result) {
  const struct policy_case policy = {
      "adaptive_channels", NCCL_POLICY_TEST_ADAPTIVE_CHANNELS_BPF_PATH,
      "strict"};
  const uint64_t phase_latencies_ns[] = {100, 10000, 100};
  const size_t samples_per_phase = kAdaptivePhaseIterations / kAdaptiveSampleStride;
  const size_t expected_samples =
      samples_per_phase * (sizeof(phase_latencies_ns) / sizeof(phase_latencies_ns[0]));
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
      !session.map_lookup_elem) {
    close_plugin_session(&session);
    return failf("adaptive curve test requires debug map helpers");
  }

  telemetry_map_fd =
      session.debug_get_map_fd(session.plugin_context, "telemetry_map");
  if (telemetry_map_fd < 0) {
    close_plugin_session(&session);
    return failf("adaptive curve test could not find telemetry_map");
  }

  for (size_t sample_idx = 0; sample_idx < expected_samples; ++sample_idx) {
    const uint32_t phase_id = (uint32_t)(sample_idx / samples_per_phase);
    const uint64_t seeded_latency_ns = phase_latencies_ns[phase_id];
    const struct nccl_policy_telemetry_value *current =
        lookup_telemetry_map(&session, telemetry_map_fd, &key);
    struct nccl_policy_telemetry_value seeded = {
        .last_latency_ns = seeded_latency_ns,
        .avg_latency_ns = phase_id == 1 ? 100 : seeded_latency_ns,
        .p99_latency_ns = seeded_latency_ns,
        .last_n_bytes = 1u << 20,
        .samples = current ? current->samples : 1,
        .recommended_channels = current ? current->recommended_channels : 8,
    };
    struct decision_result decision = {-1, -1, -1};

    if (seed_telemetry_map(&session, telemetry_map_fd, &key, &seeded) != 0) {
      close_plugin_session(&session);
      return -1;
    }

    for (size_t iter = 0; iter < kAdaptiveSampleStride; ++iter) {
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
    result->injected_latency_ns[sample_idx] = seeded_latency_ns;
    result->channels[sample_idx] = current->recommended_channels;
    result->map_channels[sample_idx] = current->recommended_channels;
    result->phase_ids[sample_idx] = phase_id;
    result->phase_end_channels[phase_id] = current->recommended_channels;
    result->sample_count++;
  }

  close_plugin_session(&session);

  if (result->phase_end_channels[0] < 10 ||
      result->phase_end_channels[1] >= result->phase_end_channels[0] ||
      result->phase_end_channels[2] <= result->phase_end_channels[1]) {
    return failf("adaptive curve did not show high channels, contention drop, "
                 "and recovery");
  }

  printf("adaptive contention response:\n");
  printf("phase_end_channels: baseline=%u contention=%u recovery=%u samples=%zu\n",
         result->phase_end_channels[0], result->phase_end_channels[1],
         result->phase_end_channels[2], result->sample_count);
  printf("| sample | call_count | phase | injected_latency_ns | channels |\n");
  printf("| --- | --- | --- | --- | --- |\n");
  for (size_t i = 0; i < result->sample_count; ++i) {
    printf("| %zu | %" PRIu64 " | %s | %" PRIu64 " | %u |\n", i + 1,
           result->calls[i], phase_name(result->phase_ids[i]),
           result->injected_latency_ns[i], result->channels[i]);
  }
  printf("adaptive contention response: PASS\n");
  return 0;
}

int main(int argc, char **argv) {
  const char *plugin_path = argc > 1 ? argv[1] : NCCL_POLICY_TEST_PLUGIN_PATH;
  const struct verifier_case verifier_policies[] = {
      {"noop", NCCL_POLICY_TEST_NOOP_BPF_PATH, "strict", "valid", 1},
      {"size_aware", NCCL_POLICY_TEST_SIZE_AWARE_BPF_PATH, "strict", "valid",
       1},
      {"size_aware_v2", NCCL_POLICY_TEST_SIZE_AWARE_V2_BPF_PATH, "strict",
       "valid", 1},
      {"lookup_only", NCCL_POLICY_TEST_LOOKUP_ONLY_BPF_PATH, "strict", "valid",
       1},
      {"lookup_update", NCCL_POLICY_TEST_LOOKUP_UPDATE_BPF_PATH, "strict",
       "valid", 1},
      {"adaptive_channels", NCCL_POLICY_TEST_ADAPTIVE_CHANNELS_BPF_PATH,
       "strict", "valid", 1},
      {"slo_enforcer", NCCL_POLICY_TEST_SLO_ENFORCER_BPF_PATH, "strict",
       "valid", 1},
      {"bad_lookup", NCCL_POLICY_TEST_BAD_LOOKUP_BPF_PATH, "strict",
       "null_deref_after_map_lookup", 0},
      {"bad_oob_access", NCCL_POLICY_TEST_BAD_OOB_ACCESS_BPF_PATH, "strict",
       "ctx_out_of_bounds_read", 0},
      {"bad_unregistered_helper",
       NCCL_POLICY_TEST_BAD_UNREGISTERED_HELPER_BPF_PATH, "strict",
       "helper_not_registered", 0},
      {"bad_stack_overflow", NCCL_POLICY_TEST_BAD_STACK_OVERFLOW_BPF_PATH,
       "strict", "stack_limit_exceeded", 0},
      {"bad_infinite_loop", NCCL_POLICY_TEST_BAD_INFINITE_LOOP_BPF_PATH,
       "strict", "unbounded_loop", 0},
      {"bad_write_ctx", NCCL_POLICY_TEST_BAD_WRITE_CTX_BPF_PATH, "strict",
       "write_to_read_only_ctx", 0},
      {"bad_div_zero", NCCL_POLICY_TEST_BAD_DIV_ZERO_BPF_PATH, "strict",
       "potential_divide_by_zero", 0},
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

  if (test_verifier_matrix(plugin_path, verifier_policies,
                           sizeof(verifier_policies) /
                               sizeof(verifier_policies[0]),
                           NULL) != 0) {
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

  if (test_profiler_telemetry_bridge(plugin_path) != 0) {
    free(samples);
    return 1;
  }

  if (test_multi_communicator_differentiation(plugin_path) != 0) {
    free(samples);
    return 1;
  }

  if (test_collective_type_coverage(plugin_path) != 0) {
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

  if (test_hot_reload_safety(plugin_path, &hot_reload_result) != 0) {
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
