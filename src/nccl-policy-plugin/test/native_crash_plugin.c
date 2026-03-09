#include "nccl_tuner.h"

static ncclResult_t crash_plugin_init(void **context, uint64_t comm_id,
                                      size_t n_ranks, size_t n_nodes,
                                      ncclDebugLogger_t log_function,
                                      ncclNvlDomainInfo_v5_t *nvl_domain_info,
                                      ncclTunerConstants_v5_t *constants) {
  (void)comm_id;
  (void)n_ranks;
  (void)n_nodes;
  (void)log_function;
  (void)nvl_domain_info;
  (void)constants;
  if (context)
    *context = NULL;
  return ncclSuccess;
}

static ncclResult_t crash_plugin_get_coll_info(
    void *context, ncclFunc_t coll_type, size_t n_bytes, int num_pipe_ops,
    float **coll_cost_table, int num_algo, int num_proto, int reg_buff,
    int *n_channels) {
  volatile int *null_slot = (volatile int *)0;

  (void)context;
  (void)coll_type;
  (void)n_bytes;
  (void)num_pipe_ops;
  (void)coll_cost_table;
  (void)num_algo;
  (void)num_proto;
  (void)reg_buff;
  (void)n_channels;

  *null_slot = 1;
  return ncclSuccess;
}

static ncclResult_t crash_plugin_finalize(void *context) {
  (void)context;
  return ncclSuccess;
}

extern const ncclTuner_v5_t ncclTunerPlugin_v5
    __attribute__((visibility("default"))) = {
        .name = "native-crash-policy",
        .init = crash_plugin_init,
        .getCollInfo = crash_plugin_get_coll_info,
        .finalize = crash_plugin_finalize,
};
