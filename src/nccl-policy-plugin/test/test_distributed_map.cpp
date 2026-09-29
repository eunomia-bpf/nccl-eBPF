#include "../distributed_map.h"

#include <sys/wait.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>

#ifdef NDEBUG
#undef NDEBUG
#endif
#include <cassert>
#include <cstring>
#include <chrono>
#include <cstdint>
#include <string>
#include <vector>

using ncclbpf::AgreedSnapshot;
using ncclbpf::ExchangeSameHost;
using ncclbpf::MapSchema;
using ncclbpf::MapWrite;
using ncclbpf::MergeRankProposals;
using ncclbpf::RankProposal;

static RankProposal Proposal(uint32_t rank, uint64_t version = 7) {
  RankProposal p;
  p.rank = rank;
  p.n_ranks = 2;
  p.policy_version = version;
  p.round = 3;
  p.activation_call = 9;
  p.maps = {MapSchema{"agreed_map", 1, 1, 3}};
  p.writes = {MapWrite{"agreed_map", {1}, {static_cast<uint8_t>(rank + 10)}}};
  return p;
}

static void AssertMerged(const AgreedSnapshot &snapshot) {
  assert(snapshot.policy_version == 7);
  assert(snapshot.round == 3);
  assert(snapshot.activation_call == 9);
  assert(snapshot.rank_writes.size() == 2);
  assert(snapshot.rank_writes[0].rank == 0);
  assert(snapshot.rank_writes[0].write.key == ncclbpf::Bytes{1});
  assert(snapshot.rank_writes[0].write.value == ncclbpf::Bytes{10});
  assert(snapshot.rank_writes[1].rank == 1);
  assert(snapshot.rank_writes[1].write.key == ncclbpf::Bytes{1});
  assert(snapshot.rank_writes[1].write.value == ncclbpf::Bytes{11});
}

int main() {
  AgreedSnapshot snapshot;
  std::string error;
  assert(MergeRankProposals({Proposal(1), Proposal(0)}, &snapshot, &error));
  AssertMerged(snapshot);

  auto wrong_version = Proposal(1, 8);
  assert(!MergeRankProposals({Proposal(0), wrong_version}, &snapshot, &error));
  assert(error.find("version") != std::string::npos);
  auto wrong_call = Proposal(1);
  wrong_call.activation_call = 10;
  assert(!MergeRankProposals({Proposal(0), wrong_call}, &snapshot, &error));
  auto duplicate_rank = Proposal(0);
  assert(!MergeRankProposals({Proposal(0), duplicate_rank}, &snapshot, &error));
  auto wrong_shape = Proposal(1);
  wrong_shape.maps[0].value_size = 2;
  assert(!MergeRankProposals({Proposal(0), wrong_shape}, &snapshot, &error));
  auto too_many_slots = Proposal(1);
  too_many_slots.writes = {
      MapWrite{"agreed_map", {2}, {12}},
      MapWrite{"agreed_map", {3}, {13}},
      MapWrite{"agreed_map", {4}, {14}},
      MapWrite{"agreed_map", {5}, {15}},
  };
  assert(!MergeRankProposals({Proposal(0), too_many_slots}, &snapshot, &error));
  auto duplicate_local = Proposal(1);
  duplicate_local.writes.push_back(duplicate_local.writes[0]);
  assert(!MergeRankProposals({Proposal(0), duplicate_local}, &snapshot, &error));

  const uint64_t comm_id = static_cast<uint64_t>(getpid()) << 32 | 0xc011u;
  pid_t child = fork();
  assert(child >= 0);
  if (child == 0) {
    AgreedSnapshot peer_snapshot;
    std::string peer_error;
    const bool ok = ExchangeSameHost(comm_id, Proposal(1), &peer_snapshot,
                                     &peer_error);
    if (!ok || peer_snapshot.rank_writes.size() != 2 ||
        peer_snapshot.rank_writes[1].write.value != ncclbpf::Bytes{11})
      _exit(1);
    _exit(0);
  }
  assert(ExchangeSameHost(comm_id, Proposal(0), &snapshot, &error));
  AssertMerged(snapshot);
  int status = 0;
  assert(waitpid(child, &status, 0) == child);
  assert(WIFEXITED(status) && WEXITSTATUS(status) == 0);

  const uint64_t mismatched_id = comm_id + 3;
  child = fork();
  assert(child >= 0);
  if (child == 0) {
    AgreedSnapshot peer_snapshot;
    std::string peer_error;
    _exit(ExchangeSameHost(mismatched_id, Proposal(1, 8),
                           &peer_snapshot, &peer_error) ? 1 : 0);
  }
  assert(!ExchangeSameHost(mismatched_id, Proposal(0), &snapshot, &error));
  assert(error.find("version") != std::string::npos);
  assert(waitpid(child, &status, 0) == child);
  assert(WIFEXITED(status) && WEXITSTATUS(status) == 0);

  const auto path_for = [](uint64_t id) {
    return std::string("/tmp/ncclbpf-") + std::to_string(getuid()) +
           "-" + std::to_string(id) + "-3.sock";
  };
  const uint64_t stale_id = comm_id + 1;
  const std::string stale_path = path_for(stale_id);
  sockaddr_un stale_address = {};
  stale_address.sun_family = AF_UNIX;
  std::strncpy(stale_address.sun_path, stale_path.c_str(),
               sizeof(stale_address.sun_path) - 1);
  int stale_fd = socket(AF_UNIX, SOCK_STREAM, 0);
  assert(stale_fd >= 0);
  assert(bind(stale_fd, reinterpret_cast<sockaddr *>(&stale_address),
              sizeof(stale_address)) == 0);
  close(stale_fd);
  child = fork();
  assert(child >= 0);
  if (child == 0) {
    AgreedSnapshot peer_snapshot;
    std::string peer_error;
    _exit(ExchangeSameHost(stale_id, Proposal(1), &peer_snapshot,
                           &peer_error) ? 0 : 1);
  }
  assert(ExchangeSameHost(stale_id, Proposal(0), &snapshot, &error));
  assert(waitpid(child, &status, 0) == child);
  assert(WIFEXITED(status) && WEXITSTATUS(status) == 0);

  const uint64_t stalled_id = comm_id + 2;
  const std::string stalled_path = path_for(stalled_id);
  sockaddr_un stalled_address = {};
  stalled_address.sun_family = AF_UNIX;
  std::strncpy(stalled_address.sun_path, stalled_path.c_str(),
               sizeof(stalled_address.sun_path) - 1);
  child = fork();
  assert(child >= 0);
  if (child == 0) {
    int fd = socket(AF_UNIX, SOCK_STREAM, 0);
    if (fd < 0)
      _exit(1);
    bool connected = false;
    for (int attempt = 0; attempt < 200; ++attempt) {
      if (connect(fd, reinterpret_cast<sockaddr *>(&stalled_address),
                  sizeof(stalled_address)) == 0) {
        connected = true;
        break;
      }
      usleep(1000);
    }
    if (!connected)
      _exit(2);
    usleep(800000);
    close(fd);
    _exit(0);
  }
  const auto wait_start = std::chrono::steady_clock::now();
  assert(!ExchangeSameHost(stalled_id, Proposal(0), &snapshot, &error));
  const auto elapsed = std::chrono::steady_clock::now() - wait_start;
  assert(elapsed < std::chrono::milliseconds(650));
  assert(error.find("invalid peer") != std::string::npos);
  assert(waitpid(child, &status, 0) == child);
  assert(WIFEXITED(status) && WEXITSTATUS(status) == 0);
  return 0;
}

