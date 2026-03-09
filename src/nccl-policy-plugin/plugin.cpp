#include <inttypes.h>
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include <memory>
#include <new>
#include <string>

#include "llvmbpf.hpp"
#include "nccl_tuner.h"
#include "../ebpf-policies/policy_context.h"

extern "C" {
struct bpf_object;
struct bpf_program;
struct bpf_insn;

void bpf_object__close(struct bpf_object *obj);
struct bpf_object *bpf_object__open(const char *path);
struct bpf_program *bpf_object__next_program(const struct bpf_object *obj,
                                             struct bpf_program *prog);
const struct bpf_insn *bpf_program__insns(const struct bpf_program *prog);
size_t bpf_program__insn_cnt(const struct bpf_program *prog);
}

namespace {

constexpr unsigned char kHardcodedNoopProgram[] = {
    0xb7, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x95, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
};

enum PolicyDecision : uint64_t {
  kDecisionDefault = 0,
  kDecisionTreeSimple = 1,
  kDecisionRingLl = 2,
  kDecisionRingSimple = 3,
};

struct PluginContext {
  std::unique_ptr<bpftime::llvmbpf_vm> vm;
  ncclDebugLogger_t log_function = nullptr;
  size_t n_ranks = 0;
  size_t n_nodes = 0;
  uint64_t call_count = 0;
  uint64_t total_latency_ns = 0;
  uint64_t last_latency_ns = 0;
  bool loaded_from_file = false;
  std::string policy_source = "hardcoded-noop";
};

uint64_t monotonic_time_ns() {
  struct timespec ts;
  clock_gettime(CLOCK_MONOTONIC_RAW, &ts);
  return static_cast<uint64_t>(ts.tv_sec) * 1000000000ull +
         static_cast<uint64_t>(ts.tv_nsec);
}

void log_plugin_message(PluginContext *ctx, ncclDebugLogLevel level,
                        const char *fmt, ...) {
  char buffer[512];
  va_list args;
  va_start(args, fmt);
  vsnprintf(buffer, sizeof(buffer), fmt, args);
  va_end(args);

  fprintf(stderr, "[nccl-policy-plugin] %s\n", buffer);
  if (ctx && ctx->log_function) {
    ctx->log_function(level, NCCL_TUNING, __FILE__, __LINE__, "%s", buffer);
  }
}

bool load_raw_program(PluginContext *ctx, const void *code, size_t code_len,
                      const char *source_name) {
  if (ctx->vm->load_code(code, code_len) < 0) {
    log_plugin_message(ctx, NCCL_LOG_WARN,
                       "failed to load %s: %s", source_name,
                       ctx->vm->get_error_message().c_str());
    return false;
  }
  ctx->policy_source = source_name;
  return true;
}

bool load_program_from_object(PluginContext *ctx, const char *path) {
  struct bpf_object *obj = bpf_object__open(path);
  if (!obj) {
    log_plugin_message(ctx, NCCL_LOG_WARN,
                       "unable to open BPF object %s", path);
    return false;
  }

  struct bpf_program *prog = bpf_object__next_program(obj, nullptr);
  if (!prog) {
    log_plugin_message(ctx, NCCL_LOG_WARN,
                       "no program found in BPF object %s", path);
    bpf_object__close(obj);
    return false;
  }

  const struct bpf_insn *insns = bpf_program__insns(prog);
  const size_t code_len = bpf_program__insn_cnt(prog) * 8;
  const bool ok = insns && code_len > 0 &&
                  load_raw_program(ctx, insns, code_len, path);

  if (ok) {
    ctx->loaded_from_file = true;
  }
  bpf_object__close(obj);
  return ok;
}

bool warmup_program(PluginContext *ctx) {
  struct nccl_policy_ctx warmup_ctx = {};
  warmup_ctx.n_bytes = 1024;
  warmup_ctx.coll_type = static_cast<uint32_t>(ncclFuncAllReduce);
  warmup_ctx.num_pipe_ops = 1;
  warmup_ctx.n_ranks = static_cast<uint32_t>(ctx->n_ranks);
  warmup_ctx.n_nodes = static_cast<uint32_t>(ctx->n_nodes);

  uint64_t decision = 0;
  const uint64_t start_ns = monotonic_time_ns();
  const int err = ctx->vm->exec(&warmup_ctx, sizeof(warmup_ctx), decision);
  const uint64_t end_ns = monotonic_time_ns();

  if (err < 0) {
    log_plugin_message(ctx, NCCL_LOG_WARN,
                       "warmup execution failed: %s",
                       ctx->vm->get_error_message().c_str());
    return false;
  }

  fprintf(stderr,
          "[nccl-policy-plugin] init warmup bytes=%" PRIu64
          " decision=%" PRIu64 " latency_ns=%" PRIu64 "\n",
          warmup_ctx.n_bytes, decision, end_ns - start_ns);
  return true;
}

void apply_policy_decision(uint64_t decision, float **coll_cost_table,
                           int num_algo, int num_proto, int *n_channels) {
  int algo = -1;
  int proto = -1;
  int channels = 1;

  switch (decision) {
    case kDecisionTreeSimple:
      algo = NCCL_ALGO_TREE;
      proto = NCCL_PROTO_SIMPLE;
      channels = 2;
      break;
    case kDecisionRingLl:
      algo = NCCL_ALGO_RING;
      proto = NCCL_PROTO_LL;
      channels = 4;
      break;
    case kDecisionRingSimple:
      algo = NCCL_ALGO_RING;
      proto = NCCL_PROTO_SIMPLE;
      channels = 8;
      break;
    case kDecisionDefault:
    default:
      *n_channels = 1;
      return;
  }

  if (algo >= 0 && proto >= 0 && algo < num_algo && proto < num_proto &&
      coll_cost_table[algo][proto] != NCCL_ALGO_PROTO_IGNORE) {
    coll_cost_table[algo][proto] = 0.0f;
  }
  *n_channels = channels;
}

ncclResult_t pluginInitImpl(void **context, uint64_t comm_id, size_t n_ranks,
                            size_t n_nodes,
                            ncclDebugLogger_t log_function,
                            ncclNvlDomainInfo_v5_t *nvl_domain_info,
                            ncclTunerConstants_v5_t *constants) {
  (void)comm_id;
  (void)nvl_domain_info;
  (void)constants;

  auto *ctx = new (std::nothrow) PluginContext();
  if (!ctx) {
    return ncclSystemError;
  }

  ctx->vm = std::make_unique<bpftime::llvmbpf_vm>();
  ctx->log_function = log_function;
  ctx->n_ranks = n_ranks;
  ctx->n_nodes = n_nodes;

  const char *policy_path = getenv("NCCL_POLICY_BPF_PATH");
  bool loaded = false;
  if (policy_path && policy_path[0] != '\0') {
    loaded = load_program_from_object(ctx, policy_path);
  }
  if (!loaded) {
    loaded = load_raw_program(ctx, kHardcodedNoopProgram,
                              sizeof(kHardcodedNoopProgram),
                              "hardcoded-noop");
  }
  if (!loaded) {
    delete ctx;
    return ncclInternalError;
  }
  if (!warmup_program(ctx)) {
    delete ctx;
    return ncclInternalError;
  }

  log_plugin_message(ctx, NCCL_LOG_INFO,
                     "initialized for %zu ranks across %zu nodes using policy %s",
                     n_ranks, n_nodes, ctx->policy_source.c_str());
  *context = ctx;
  return ncclSuccess;
}

ncclResult_t pluginGetCollInfoImpl(void *context, ncclFunc_t coll_type,
                                   size_t n_bytes, int num_pipe_ops,
                                   float **coll_cost_table, int num_algo,
                                   int num_proto, int reg_buff,
                                   int *n_channels) {
  auto *ctx = reinterpret_cast<PluginContext *>(context);
  if (!ctx || !ctx->vm || !n_channels) {
    return ncclInternalError;
  }

  struct nccl_policy_ctx policy_ctx = {};
  policy_ctx.n_bytes = n_bytes;
  policy_ctx.coll_type = static_cast<uint32_t>(coll_type);
  policy_ctx.num_pipe_ops = static_cast<uint32_t>(num_pipe_ops);
  policy_ctx.reg_buff = static_cast<uint32_t>(reg_buff);
  policy_ctx.n_ranks = static_cast<uint32_t>(ctx->n_ranks);
  policy_ctx.n_nodes = static_cast<uint32_t>(ctx->n_nodes);

  uint64_t decision = 0;
  const uint64_t start_ns = monotonic_time_ns();
  const int err = ctx->vm->exec(&policy_ctx, sizeof(policy_ctx), decision);
  const uint64_t end_ns = monotonic_time_ns();

  if (err < 0) {
    log_plugin_message(ctx, NCCL_LOG_WARN,
                       "eBPF execution failed: %s",
                       ctx->vm->get_error_message().c_str());
    return ncclInternalError;
  }

  ctx->last_latency_ns = end_ns - start_ns;
  ctx->total_latency_ns += ctx->last_latency_ns;
  ctx->call_count++;

  apply_policy_decision(decision, coll_cost_table, num_algo, num_proto,
                        n_channels);

  if (ctx->call_count <= 5 || ctx->call_count % 100000 == 0) {
    fprintf(stderr,
            "[nccl-policy-plugin] call=%" PRIu64 " bytes=%zu decision=%" PRIu64
            " latency_ns=%" PRIu64 "\n",
            ctx->call_count, n_bytes, decision, ctx->last_latency_ns);
  }

  return ncclSuccess;
}

ncclResult_t pluginFinalizeImpl(void *context) {
  auto *ctx = reinterpret_cast<PluginContext *>(context);
  if (!ctx) {
    return ncclSuccess;
  }

  const uint64_t avg_latency =
      ctx->call_count == 0 ? 0 : ctx->total_latency_ns / ctx->call_count;
  fprintf(stderr,
          "[nccl-policy-plugin] finalize calls=%" PRIu64
          " avg_latency_ns=%" PRIu64 " last_latency_ns=%" PRIu64
          " source=%s\n",
          ctx->call_count, avg_latency, ctx->last_latency_ns,
          ctx->policy_source.c_str());

  delete ctx;
  return ncclSuccess;
}

}  // namespace

extern "C" {

static ncclResult_t pluginInit(void **context, uint64_t comm_id, size_t n_ranks,
                               size_t n_nodes,
                               ncclDebugLogger_t log_function,
                               ncclNvlDomainInfo_v5_t *nvl_domain_info,
                               ncclTunerConstants_v5_t *constants) {
  return pluginInitImpl(context, comm_id, n_ranks, n_nodes, log_function,
                        nvl_domain_info, constants);
}

static ncclResult_t pluginGetCollInfo(void *context, ncclFunc_t coll_type,
                                      size_t n_bytes, int num_pipe_ops,
                                      float **coll_cost_table, int num_algo,
                                      int num_proto, int reg_buff,
                                      int *n_channels) {
  return pluginGetCollInfoImpl(context, coll_type, n_bytes, num_pipe_ops,
                               coll_cost_table, num_algo, num_proto, reg_buff,
                               n_channels);
}

static ncclResult_t pluginFinalize(void *context) {
  return pluginFinalizeImpl(context);
}

extern const ncclTuner_v5_t ncclTunerPlugin_v5
    __attribute__((visibility("default"))) = {
    .name = "eBPFPolicy",
    .init = pluginInit,
    .getCollInfo = pluginGetCollInfo,
    .finalize = pluginFinalize,
};

}
