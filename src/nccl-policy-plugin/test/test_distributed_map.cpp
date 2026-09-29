#include "../distributed_map.h"

#include <sys/wait.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <netinet/in.h>
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
using ncclbpf::ExchangeTCP;
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

static uint16_t UnusedLoopbackPort() {
  int fd = socket(AF_INET, SOCK_STREAM, 0);
  assert(fd >= 0);
  sockaddr_in address = {};
  address.sin_family = AF_INET;
  address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
  address.sin_port = 0;
  assert(bind(fd, reinterpret_cast<sockaddr *>(&address), sizeof(address)) == 0);
  socklen_t length = sizeof(address);
  assert(getsockname(fd, reinterpret_cast<sockaddr *>(&address), &length) == 0);
  close(fd);
  return ntohs(address.sin_port);
}

static void TestTCP(uint64_t comm_id, bool initial, bool version_mismatch,
                    const std::string &endpoint) {
  constexpr uint32_t kRanks = 4;
  std::vector<pid_t> children;
  for (uint32_t rank = 1; rank < kRanks; ++rank) {
    const pid_t child = fork();
    assert(child >= 0);
    if (child == 0) {
      RankProposal proposal = Proposal(rank, version_mismatch && rank == 3 ? 8 : 7);
      proposal.n_ranks = kRanks;
      if (initial) {
        proposal.round = 0;
        proposal.activation_call = 0;
        proposal.maps.clear();
        proposal.writes.clear();
      }
      AgreedSnapshot result;
      std::string reason;
      const bool ok = ExchangeTCP(comm_id, proposal, endpoint, &result, &reason);
      if (version_mismatch)
        _exit(ok ? 1 : 0);
      if (!ok || result.policy_version != 7 ||
          result.round != (initial ? 0 : 3) ||
          result.activation_call != (initial ? 0 : 9) ||
          result.rank_writes.size() != (initial ? 0 : kRanks))
        _exit(1);
      _exit(0);
    }
    children.push_back(child);
  }
  RankProposal local = Proposal(0);
  local.n_ranks = kRanks;
  if (initial) {
    local.round = 0;
    local.activation_call = 0;
    local.maps.clear();
    local.writes.clear();
  }
  AgreedSnapshot result;
  std::string reason;
  const bool ok = ExchangeTCP(comm_id, local, endpoint, &result, &reason);
  if (version_mismatch) {
    assert(!ok && reason.find("version") != std::string::npos);
  } else {
    assert(ok);
    assert(result.policy_version == 7);
    assert(result.rank_writes.size() == (initial ? 0 : kRanks));
  }
  for (pid_t child : children) {
    int status = 0;
    assert(waitpid(child, &status, 0) == child);
    assert(WIFEXITED(status) && WEXITSTATUS(status) == 0);
  }
}

static void PutNetwork32(ncclbpf::Bytes *out, uint32_t value) {
  for (int shift = 24; shift >= 0; shift -= 8)
    out->push_back(static_cast<uint8_t>(value >> shift));
}

static void PutNetwork64(ncclbpf::Bytes *out, uint64_t value) {
  for (int shift = 56; shift >= 0; shift -= 8)
    out->push_back(static_cast<uint8_t>(value >> shift));
}

static bool SendRaw(int fd, const ncclbpf::Bytes &bytes) {
  size_t sent = 0;
  while (sent < bytes.size()) {
    const ssize_t n = send(fd, bytes.data() + sent, bytes.size() - sent, 0);
    if (n <= 0)
      return false;
    sent += static_cast<size_t>(n);
  }
  return true;
}

static bool ReadRaw(int fd, uint8_t *bytes, size_t count) {
  while (count) {
    const ssize_t n = recv(fd, bytes, count, 0);
    if (n <= 0)
      return false;
    bytes += n;
    count -= static_cast<size_t>(n);
  }
  return true;
}

// Rank 2 receives PREPARE but disappears before ACK. Rank 1 must not
// activate, even though it already received the full proposal set.
static void TestPartialPrepare(uint64_t comm_id) {
  const std::string endpoint =
      "127.0.0.1:" + std::to_string(UnusedLoopbackPort());
  const uint16_t port = static_cast<uint16_t>(
      std::stoi(endpoint.substr(endpoint.rfind(':') + 1)));
  const pid_t rank1 = fork();
  assert(rank1 >= 0);
  if (rank1 == 0) {
    RankProposal local = Proposal(1);
    local.n_ranks = 3;
    local.round = 0;
    local.activation_call = 0;
    local.maps.clear();
    local.writes.clear();
    AgreedSnapshot result;
    std::string reason;
    _exit(ExchangeTCP(comm_id, local, endpoint, &result, &reason) ? 1 : 0);
  }
  const pid_t rank2 = fork();
  assert(rank2 >= 0);
  if (rank2 == 0) {
    sockaddr_in address = {};
    address.sin_family = AF_INET;
    address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    address.sin_port = htons(port);
    int fd = -1;
    for (int attempt = 0; attempt < 300 && fd < 0; ++attempt) {
      int candidate = socket(AF_INET, SOCK_STREAM, 0);
      if (candidate >= 0 &&
          connect(candidate, reinterpret_cast<sockaddr *>(&address),
                  sizeof(address)) == 0)
        fd = candidate;
      else {
        if (candidate >= 0)
          close(candidate);
        usleep(1000);
      }
    }
    if (fd < 0)
      _exit(2);
    ncclbpf::Bytes body{'P'};
    PutNetwork64(&body, comm_id);
    PutNetwork32(&body, 2);
    PutNetwork32(&body, 3);
    PutNetwork64(&body, 7);
    PutNetwork64(&body, 0);
    PutNetwork64(&body, 0);
    PutNetwork32(&body, 0);
    PutNetwork32(&body, 0);
    ncclbpf::Bytes frame;
    PutNetwork32(&frame, static_cast<uint32_t>(body.size()));
    frame.insert(frame.end(), body.begin(), body.end());
    if (!SendRaw(fd, frame))
      _exit(3);
    uint8_t header[4];
    if (!ReadRaw(fd, header, sizeof(header)))
      _exit(4);
    const uint32_t size = (uint32_t(header[0]) << 24) |
                          (uint32_t(header[1]) << 16) |
                          (uint32_t(header[2]) << 8) |
                          uint32_t(header[3]);
    if (size == 0 || size > 4096)
      _exit(5);
    ncclbpf::Bytes prepare(size);
    if (!ReadRaw(fd, prepare.data(), prepare.size()) || prepare[0] != 'R')
      _exit(6);
    close(fd);
    _exit(0);
  }
  RankProposal local = Proposal(0);
  local.n_ranks = 3;
  local.round = 0;
  local.activation_call = 0;
  local.maps.clear();
  local.writes.clear();
  AgreedSnapshot result;
  result.policy_version = 99;
  std::string reason;
  assert(!ExchangeTCP(comm_id, local, endpoint, &result, &reason));
  assert(reason.find("acknowledged") != std::string::npos);
  assert(result.policy_version == 99 && result.rank_writes.empty());
  int status = 0;
  assert(waitpid(rank1, &status, 0) == rank1);
  assert(WIFEXITED(status) && WEXITSTATUS(status) == 0);
  assert(waitpid(rank2, &status, 0) == rank2);
  assert(WIFEXITED(status) && WEXITSTATUS(status) == 0);
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
  snapshot = {};
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
  const std::string tcp_endpoint =
      "127.0.0.1:" + std::to_string(UnusedLoopbackPort());
  TestTCP(comm_id + 10, true, false, tcp_endpoint);
  TestTCP(comm_id + 10, false, false, tcp_endpoint);
  TestTCP(comm_id + 12, false, true,
          "127.0.0.1:" + std::to_string(UnusedLoopbackPort()));
  TestPartialPrepare(comm_id + 14);
  RankProposal tcp_local = Proposal(0);
  assert(!ExchangeTCP(comm_id + 13, tcp_local, "0.0.0.0:12345",
                      &snapshot, &error));
  assert(error.find("one interface") != std::string::npos);
  return 0;
}

