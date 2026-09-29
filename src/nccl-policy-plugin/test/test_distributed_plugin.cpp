#include <dlfcn.h>
#include <sys/wait.h>
#include <unistd.h>

#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <vector>

#include "nccl_tuner.h"
#include "nccl_profiler.h"

using GetMapFd = int (*)(void *, const char *);
using MapUpdate = long (*)(int, const void *, const void *, uint64_t);
using MapLookup = const void *(*)(int, const void *);
using Reload = int (*)(void *, const char *, void *);

static void Log(ncclDebugLogLevel, unsigned long, const char *, int,
                const char *, ...) {}

static int RunRank(const char *plugin_path, const char *policy_path,
                   uint64_t comm_id, int rank, int n_ranks) {
  if (setenv("NCCL_POLICY_BPF_PATH", policy_path, 1) != 0 ||
      setenv("NCCL_POLICY_VERIFY_MODE", "strict", 1) != 0 ||
      setenv("NCCL_POLICY_PROFILER_MODE", "native", 1) != 0 ||
      setenv("NCCL_POLICY_EXPERIMENTAL_DIST_MAP", "1", 1) != 0 ||
      setenv("BPFTIME_SHM_MEMORY_MB", "4", 1) != 0)
    return 1;
  void *lib = dlopen(plugin_path, RTLD_NOW | RTLD_LOCAL);
  if (!lib) {
    std::fprintf(stderr, "dlopen: %s\n", dlerror());
    return 2;
  }
  auto *tuner = static_cast<const ncclTuner_v5_t *>(
      dlsym(lib, NCCL_TUNER_PLUGIN_SYMBOL));
  auto *profiler = static_cast<const ncclProfiler_v6_t *>(
      dlsym(lib, "ncclProfiler_v6"));
  auto get_map_fd = reinterpret_cast<GetMapFd>(
      dlsym(lib, "ncclPolicyPluginDebugGetMapFd"));
  auto update = reinterpret_cast<MapUpdate>(
      dlsym(lib, "bpftime_map_update_elem"));
  auto lookup = reinterpret_cast<MapLookup>(
      dlsym(lib, "bpftime_map_lookup_elem"));
  auto reload = reinterpret_cast<Reload>(
      dlsym(lib, "ncclPolicyPluginDebugReloadPolicy"));
  if (!tuner || !profiler || !get_map_fd || !update || !lookup ||
      !reload)
    return 3;

  void *tuner_ctx = nullptr;
  void *profiler_ctx = nullptr;
  int mask = 0;
  if (tuner->init(&tuner_ctx, comm_id, n_ranks, 1, Log, nullptr, nullptr) !=
      ncclSuccess) {
    std::fprintf(stderr, "rank %d tuner init rejected\n", rank);
    return 4;
  }
  if (profiler->init(&profiler_ctx, comm_id, &mask, "dist-test", 1, n_ranks,
                     rank, Log) != ncclSuccess)
    return 5;
  const int local_fd = get_map_fd(tuner_ctx, "local_latency");
  const int agreed_fd = get_map_fd(tuner_ctx, "agreed_map");
  if (local_fd < 0 || agreed_fd < 0)
    return 6;
  if (reload(tuner_ctx, policy_path, nullptr) != -1)
    return 12;
  const uint32_t zero = 0;
  const uint64_t local_ns = rank == n_ranks - 1 ? 2000000 : 500000;
  if (update(local_fd, &zero, &local_ns, 0) != 0)
    return 7;

  for (int call = 1; call <= 2048; ++call) {
    float costs[NCCL_NUM_ALGORITHMS][NCCL_NUM_PROTOCOLS];
    for (auto &row : costs)
      for (float &cost : row)
        cost = 1.0f;
    int channels = 8;
    if (tuner->getCollInfo(tuner_ctx, ncclFuncAllReduce, 1 << 20, 1,
                           reinterpret_cast<float **>(costs),
                           NCCL_NUM_ALGORITHMS, NCCL_NUM_PROTOCOLS, 0,
                           &channels) != ncclSuccess) {
      std::fprintf(stderr, "rank %d call %d failed\n", rank, call);
      return 8;
    }
    if ((call == 1024 && channels != 8) ||
        (call == 2048 && channels != 4)) {
      std::fprintf(stderr, "rank %d call %d channels %d\n",
                   rank, call, channels);
      return 9;
    }
  }
  const auto *agreed = static_cast<const uint64_t *>(
      lookup(agreed_fd, &zero));
  if (!agreed || *agreed != 2000000) {
    std::fprintf(stderr, "rank %d agreed value wrong\n", rank);
    return 10;
  }
  if (profiler->finalize(profiler_ctx) != ncclSuccess ||
      tuner->finalize(tuner_ctx) != ncclSuccess)
    return 11;
  dlclose(lib);
  return 0;
}

static int RunGroup(const char *plugin_path, const char *policy_path,
                    int n_ranks, uint64_t comm_id) {
  std::vector<pid_t> children;
  for (int rank = 1; rank < n_ranks; ++rank) {
    const pid_t child = fork();
    if (child < 0)
      return 2;
    if (child == 0)
      _exit(RunRank(plugin_path, policy_path, comm_id, rank, n_ranks));
    children.push_back(child);
  }
  const int parent_result =
      RunRank(plugin_path, policy_path, comm_id, 0, n_ranks);
  int child_result = 0;
  for (pid_t child : children) {
    int status = 0;
    if (waitpid(child, &status, 0) != child ||
        !WIFEXITED(status) || WEXITSTATUS(status) != 0)
      child_result = 3;
  }
  return parent_result ? parent_result : child_result;
}

static int CheckNineRanksRejected(const char *plugin_path,
                                  const char *policy_path,
                                  uint64_t comm_id) {
  if (setenv("NCCL_POLICY_BPF_PATH", policy_path, 1) != 0 ||
      setenv("NCCL_POLICY_EXPERIMENTAL_DIST_MAP", "1", 1) != 0 ||
      setenv("BPFTIME_SHM_MEMORY_MB", "4", 1) != 0)
    return 1;
  void *lib = dlopen(plugin_path, RTLD_NOW | RTLD_LOCAL);
  if (!lib)
    return 2;
  auto *tuner = static_cast<const ncclTuner_v5_t *>(
      dlsym(lib, NCCL_TUNER_PLUGIN_SYMBOL));
  void *ctx = nullptr;
  const bool rejected = tuner &&
      tuner->init(&ctx, comm_id, 9, 1, Log, nullptr, nullptr) != ncclSuccess;
  if (ctx)
    tuner->finalize(ctx);
  dlclose(lib);
  return rejected ? 0 : 3;
}

int main(int argc, char **argv) {
  if (argc != 4)
    return 1;
  const int n_ranks = std::atoi(argv[3]);
  const uint64_t comm_id =
      (static_cast<uint64_t>(getpid()) << 32) | 0xd157u;
  if (n_ranks == 9)
    return CheckNineRanksRejected(argv[1], argv[2], comm_id);
  if (n_ranks != 2 && n_ranks != 8)
    return 2;
  return RunGroup(argv[1], argv[2], n_ranks, comm_id);
}
