#include <arpa/inet.h>
#include <dlfcn.h>
#include <sys/socket.h>
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
                   uint64_t comm_id, int rank, int n_ranks, int n_nodes,
                   bool expect_initial_reject) {
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
  if (tuner->init(&tuner_ctx, comm_id, n_ranks, n_nodes, Log, nullptr, nullptr) !=
      ncclSuccess) {
    std::fprintf(stderr, "rank %d tuner init rejected\n", rank);
    return 4;
  }
  if (profiler->init(&profiler_ctx, comm_id, &mask, "dist-test", n_nodes, n_ranks,
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
    const ncclResult_t result = tuner->getCollInfo(
        tuner_ctx, ncclFuncAllReduce, 1 << 20, 1,
        reinterpret_cast<float **>(costs), NCCL_NUM_ALGORITHMS,
        NCCL_NUM_PROTOCOLS, 0, &channels);
    if (expect_initial_reject) {
      if (call != 1 || result == ncclSuccess) {
        std::fprintf(stderr, "rank %d executed mismatched policy\n", rank);
        return 13;
      }
      if (profiler->finalize(profiler_ctx) != ncclSuccess ||
          tuner->finalize(tuner_ctx) != ncclSuccess)
        return 11;
      dlclose(lib);
      return 0;
    }
    if (result != ncclSuccess) {
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
                    int n_ranks, int n_nodes, uint64_t comm_id,
                    const char *alternate_policy = nullptr) {
  std::vector<pid_t> children;
  for (int rank = 1; rank < n_ranks; ++rank) {
    const pid_t child = fork();
    if (child < 0)
      return 2;
    if (child == 0) {
      const char *rank_policy =
          rank == 1 && alternate_policy ? alternate_policy : policy_path;
      _exit(RunRank(plugin_path, rank_policy, comm_id, rank, n_ranks,
                    n_nodes, alternate_policy != nullptr));
    }
    children.push_back(child);
  }
  const int parent_result =
      RunRank(plugin_path, policy_path, comm_id, 0, n_ranks, n_nodes,
              alternate_policy != nullptr);
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

static int CheckMissingCoordinatorRejected(const char *plugin_path,
                                           const char *policy_path,
                                           uint64_t comm_id) {
  if (setenv("NCCL_POLICY_BPF_PATH", policy_path, 1) != 0 ||
      setenv("NCCL_POLICY_EXPERIMENTAL_DIST_MAP", "1", 1) != 0 ||
      unsetenv("NCCL_POLICY_DIST_COORDINATOR") != 0)
    return 1;
  void *lib = dlopen(plugin_path, RTLD_NOW | RTLD_LOCAL);
  if (!lib)
    return 2;
  auto *tuner = static_cast<const ncclTuner_v5_t *>(
      dlsym(lib, NCCL_TUNER_PLUGIN_SYMBOL));
  void *ctx = nullptr;
  const bool rejected = tuner &&
      tuner->init(&ctx, comm_id, 4, 4, Log, nullptr, nullptr) != ncclSuccess;
  if (ctx)
    tuner->finalize(ctx);
  dlclose(lib);
  return rejected ? 0 : 3;
}

static int MakeDifferentPolicyObject(const char *source, char *path) {
  std::strcpy(path, "/tmp/ncclbpf-policy-XXXXXX");
  const int fd = mkstemp(path);
  if (fd < 0)
    return 1;
  FILE *input = std::fopen(source, "rb");
  FILE *output = fdopen(fd, "wb");
  if (!input || !output) {
    if (input)
      std::fclose(input);
    if (output)
      std::fclose(output);
    else
      close(fd);
    unlink(path);
    return 2;
  }
  unsigned char buffer[8192];
  size_t count;
  bool ok = true;
  while ((count = std::fread(buffer, 1, sizeof(buffer), input)) != 0)
    if (std::fwrite(buffer, 1, count, output) != count)
      ok = false;
  if (std::ferror(input) || std::fputc(0, output) == EOF)
    ok = false;
  const int input_close = std::fclose(input);
  const int output_close = std::fclose(output);
  if (input_close != 0 || output_close != 0)
    ok = false;
  if (!ok)
    unlink(path);
  return ok ? 0 : 3;
}

static int SetLoopbackCoordinator() {
  const int fd = socket(AF_INET, SOCK_STREAM, 0);
  if (fd < 0)
    return 1;
  sockaddr_in address = {};
  address.sin_family = AF_INET;
  address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
  address.sin_port = 0;
  if (bind(fd, reinterpret_cast<sockaddr *>(&address), sizeof(address)) != 0) {
    close(fd);
    return 2;
  }
  socklen_t length = sizeof(address);
  if (getsockname(fd, reinterpret_cast<sockaddr *>(&address), &length) != 0) {
    close(fd);
    return 3;
  }
  close(fd);
  char endpoint[64];
  std::snprintf(endpoint, sizeof(endpoint), "127.0.0.1:%u",
                static_cast<unsigned>(ntohs(address.sin_port)));
  return setenv("NCCL_POLICY_DIST_COORDINATOR", endpoint, 1) == 0 ? 0 : 4;
}

int main(int argc, char **argv) {
  if (argc != 4)
    return 1;
  const int n_ranks = std::atoi(argv[3]);
  const uint64_t comm_id =
      (static_cast<uint64_t>(getpid()) << 32) | 0xd157u;
  if (n_ranks == 5) {
    char alternate_policy[64];
    if (SetLoopbackCoordinator() != 0 ||
        MakeDifferentPolicyObject(argv[2], alternate_policy) != 0)
      return 3;
    const int result =
        RunGroup(argv[1], argv[2], 2, 2, comm_id, alternate_policy);
    unlink(alternate_policy);
    return result;
  }
  if (n_ranks == 3)
    return CheckMissingCoordinatorRejected(argv[1], argv[2], comm_id);
  if (n_ranks == 9)
    return CheckNineRanksRejected(argv[1], argv[2], comm_id);
  if (n_ranks != 2 && n_ranks != 4 && n_ranks != 8)
    return 2;
  if (n_ranks == 4 && SetLoopbackCoordinator() != 0)
    return 3;
  return RunGroup(argv[1], argv[2], n_ranks, n_ranks == 4 ? 4 : 1, comm_id);
}
