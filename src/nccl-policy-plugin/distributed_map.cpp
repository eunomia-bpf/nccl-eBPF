#include "distributed_map.h"

#include <sys/socket.h>
#include <poll.h>
#include <sys/stat.h>
#include <sys/un.h>
#include <unistd.h>

#include <algorithm>
#include <cerrno>
#include <chrono>
#include <cstdio>
#include <cstring>
#include <map>
#include <set>
#include <string>
#include <tuple>
#include <utility>
#include <vector>

namespace ncclbpf {
namespace {

constexpr uint32_t kMaxFrameBytes = 16u * 1024u * 1024u;
#ifndef NCCLBPF_EXCHANGE_WAIT_MS
#define NCCLBPF_EXCHANGE_WAIT_MS 30000
#endif
constexpr auto kExchangeWait =
    std::chrono::milliseconds(NCCLBPF_EXCHANGE_WAIT_MS);

void Put32(Bytes *out, uint32_t value) {
  for (int shift = 24; shift >= 0; shift -= 8)
    out->push_back(static_cast<uint8_t>(value >> shift));
}

void Put64(Bytes *out, uint64_t value) {
  for (int shift = 56; shift >= 0; shift -= 8)
    out->push_back(static_cast<uint8_t>(value >> shift));
}

bool Get32(const Bytes &in, size_t *offset, uint32_t *value) {
  if (in.size() - *offset < 4)
    return false;
  *value = 0;
  for (int i = 0; i < 4; ++i)
    *value = (*value << 8) | in[(*offset)++];
  return true;
}

bool Get64(const Bytes &in, size_t *offset, uint64_t *value) {
  if (in.size() - *offset < 8)
    return false;
  *value = 0;
  for (int i = 0; i < 8; ++i)
    *value = (*value << 8) | in[(*offset)++];
  return true;
}

void PutBytes(Bytes *out, const Bytes &value) {
  Put32(out, static_cast<uint32_t>(value.size()));
  out->insert(out->end(), value.begin(), value.end());
}

void PutString(Bytes *out, const std::string &value) {
  PutBytes(out, Bytes(value.begin(), value.end()));
}

bool GetBytes(const Bytes &in, size_t *offset, Bytes *value) {
  uint32_t size = 0;
  if (!Get32(in, offset, &size) || size > in.size() - *offset)
    return false;
  value->assign(in.begin() + *offset, in.begin() + *offset + size);
  *offset += size;
  return true;
}

bool GetString(const Bytes &in, size_t *offset, std::string *value) {
  Bytes bytes;
  if (!GetBytes(in, offset, &bytes))
    return false;
  value->assign(bytes.begin(), bytes.end());
  return true;
}

void EncodeProposal(Bytes *out, const RankProposal &proposal) {
  Put32(out, proposal.rank);
  Put32(out, proposal.n_ranks);
  Put64(out, proposal.policy_version);
  Put64(out, proposal.round);
  Put64(out, proposal.activation_call);
  Put32(out, static_cast<uint32_t>(proposal.maps.size()));
  for (const auto &schema : proposal.maps) {
    PutString(out, schema.name);
    Put32(out, schema.key_size);
    Put32(out, schema.value_size);
    Put32(out, schema.max_entries);
  }
  Put32(out, static_cast<uint32_t>(proposal.writes.size()));
  for (const auto &write : proposal.writes) {
    PutString(out, write.map);
    PutBytes(out, write.key);
    PutBytes(out, write.value);
  }
}

bool DecodeProposal(const Bytes &in, size_t *offset, RankProposal *proposal) {
  uint32_t count = 0;
  if (!Get32(in, offset, &proposal->rank) ||
      !Get32(in, offset, &proposal->n_ranks) ||
      !Get64(in, offset, &proposal->policy_version) ||
      !Get64(in, offset, &proposal->round) ||
      !Get64(in, offset, &proposal->activation_call) ||
      !Get32(in, offset, &count) || count > 1024)
    return false;
  proposal->maps.resize(count);
  for (auto &schema : proposal->maps) {
    if (!GetString(in, offset, &schema.name) ||
        !Get32(in, offset, &schema.key_size) ||
        !Get32(in, offset, &schema.value_size) ||
        !Get32(in, offset, &schema.max_entries))
      return false;
  }
  if (!Get32(in, offset, &count) || count > 65536)
    return false;
  proposal->writes.resize(count);
  for (auto &write : proposal->writes) {
    if (!GetString(in, offset, &write.map) ||
        !GetBytes(in, offset, &write.key) ||
        !GetBytes(in, offset, &write.value))
      return false;
  }
  return true;
}

using Deadline = std::chrono::steady_clock::time_point;

bool ReadyUntil(int fd, short events, Deadline deadline) {
  for (;;) {
    const auto now = std::chrono::steady_clock::now();
    if (now >= deadline)
      return false;
    const auto remaining =
        std::chrono::duration_cast<std::chrono::milliseconds>(
            deadline - now).count();
    pollfd descriptor = {.fd = fd, .events = events, .revents = 0};
    const int rc = poll(&descriptor, 1,
                        static_cast<int>(std::max<int64_t>(1, remaining)));
    if (rc < 0 && errno == EINTR)
      continue;
    return rc > 0 && (descriptor.revents & events) != 0;
  }
}

bool WriteAll(int fd, const uint8_t *data, size_t size, Deadline deadline) {
  while (size) {
    if (!ReadyUntil(fd, POLLOUT, deadline))
      return false;
    const ssize_t n = send(fd, data, size, MSG_NOSIGNAL | MSG_DONTWAIT);
    if (n < 0 && (errno == EINTR || errno == EAGAIN))
      continue;
    if (n <= 0)
      return false;
    data += n;
    size -= static_cast<size_t>(n);
  }
  return true;
}

bool ReadAll(int fd, uint8_t *data, size_t size, Deadline deadline) {
  while (size) {
    if (!ReadyUntil(fd, POLLIN, deadline))
      return false;
    const ssize_t n = recv(fd, data, size, MSG_DONTWAIT);
    if (n < 0 && (errno == EINTR || errno == EAGAIN))
      continue;
    if (n <= 0)
      return false;
    data += n;
    size -= static_cast<size_t>(n);
  }
  return true;
}

bool SendFrame(int fd, const Bytes &frame, Deadline deadline) {
  if (frame.size() > kMaxFrameBytes)
    return false;
  Bytes header;
  Put32(&header, static_cast<uint32_t>(frame.size()));
  return WriteAll(fd, header.data(), header.size(), deadline) &&
         WriteAll(fd, frame.data(), frame.size(), deadline);
}

bool ReceiveFrame(int fd, Bytes *frame, Deadline deadline) {
  Bytes header(4);
  uint32_t size = 0;
  size_t offset = 0;
  if (!ReadAll(fd, header.data(), header.size(), deadline) ||
      !Get32(header, &offset, &size) || size > kMaxFrameBytes)
    return false;
  frame->resize(size);
  return ReadAll(fd, frame->data(), frame->size(), deadline);
}

std::string SocketPath(uint64_t communicator_id, uint64_t round) {
  return "/tmp/ncclbpf-" + std::to_string(getuid()) + "-" +
         std::to_string(communicator_id) + "-" + std::to_string(round) +
         ".sock";
}

bool DecodeAll(const Bytes &frame, std::vector<RankProposal> *proposals) {
  size_t offset = 0;
  uint32_t count = 0;
  if (!Get32(frame, &offset, &count) || count == 0 || count > 65536)
    return false;
  proposals->resize(count);
  for (auto &proposal : *proposals)
    if (!DecodeProposal(frame, &offset, &proposal))
      return false;
  return offset == frame.size();
}

Bytes EncodeAll(const std::vector<RankProposal> &proposals) {
  Bytes frame;
  Put32(&frame, static_cast<uint32_t>(proposals.size()));
  for (const auto &proposal : proposals)
    EncodeProposal(&frame, proposal);
  return frame;
}

void SetError(std::string *error, const char *message) {
  if (error)
    *error = message;
}

}  // namespace

bool MergeRankProposals(std::vector<RankProposal> proposals,
                        AgreedSnapshot *snapshot, std::string *error) {
  if (!snapshot || proposals.empty()) {
    SetError(error, "missing snapshot or rank proposals");
    return false;
  }
  std::sort(proposals.begin(), proposals.end(),
            [](const auto &left, const auto &right) {
              return left.rank < right.rank;
            });
  const auto &first = proposals.front();
  if (first.n_ranks == 0 || first.n_ranks != proposals.size()) {
    SetError(error, "rank count does not match proposals");
    return false;
  }

  using Slot = std::pair<std::string, Bytes>;
  std::vector<RankedMapWrite> ranked;
  std::map<std::string, MapSchema> schemas;
  for (const auto &schema : first.maps) {
    if (schema.name.empty() || schema.key_size == 0 ||
        schema.value_size == 0 || schema.max_entries == 0 ||
        !schemas.emplace(schema.name, schema).second) {
      SetError(error, "invalid or duplicate map schema");
      return false;
    }
  }

  for (uint32_t rank = 0; rank < first.n_ranks; ++rank) {
    const auto &proposal = proposals[rank];
    if (proposal.rank != rank || proposal.n_ranks != first.n_ranks ||
        proposal.policy_version != first.policy_version ||
        proposal.round != first.round ||
        proposal.activation_call != first.activation_call) {
      SetError(error, "rank, policy version, round, or activation mismatch");
      return false;
    }
    std::map<std::string, MapSchema> proposed_schemas;
    for (const auto &schema : proposal.maps)
      proposed_schemas.emplace(schema.name, schema);
    if (proposed_schemas.size() != schemas.size()) {
      SetError(error, "map schema count mismatch");
      return false;
    }
    for (const auto &[name, schema] : schemas) {
      auto it = proposed_schemas.find(name);
      if (it == proposed_schemas.end() ||
          it->second.key_size != schema.key_size ||
          it->second.value_size != schema.value_size ||
          it->second.max_entries != schema.max_entries) {
        SetError(error, "map schema mismatch");
        return false;
      }
    }
    std::set<Slot> local_slots;
    for (const auto &write : proposal.writes) {
      auto shape = schemas.find(write.map);
      if (shape == schemas.end() ||
          write.key.size() != shape->second.key_size ||
          write.value.size() != shape->second.value_size ||
          !local_slots.emplace(write.map, write.key).second) {
        SetError(error, "invalid or duplicate rank-local map write");
        return false;
      }
      ranked.push_back({rank, write});
    }
  }

  std::sort(ranked.begin(), ranked.end(),
            [](const RankedMapWrite &left, const RankedMapWrite &right) {
              return std::tie(left.rank, left.write.map, left.write.key) <
                     std::tie(right.rank, right.write.map, right.write.key);
            });
  std::map<std::pair<uint32_t, std::string>, uint32_t> entries;
  for (const auto &rank_write : ranked)
    if (++entries[{rank_write.rank, rank_write.write.map}] >
        schemas.at(rank_write.write.map).max_entries) {
      SetError(error, "rank-local snapshot exceeds map capacity");
      return false;
    }

  snapshot->policy_version = first.policy_version;
  snapshot->round = first.round;
  snapshot->activation_call = first.activation_call;
  snapshot->rank_writes = std::move(ranked);
  return true;
}

bool ExchangeSameHost(uint64_t communicator_id, const RankProposal &local,
                      AgreedSnapshot *snapshot, std::string *error) {
  if (local.n_ranks == 0 || local.rank >= local.n_ranks || !snapshot) {
    SetError(error, "invalid local rank metadata");
    return false;
  }
  if (local.n_ranks == 1)
    return MergeRankProposals({local}, snapshot, error);

  const std::string path = SocketPath(communicator_id, local.round);
  if (path.size() >= sizeof(sockaddr_un::sun_path)) {
    SetError(error, "communicator socket path too long");
    return false;
  }
  sockaddr_un address = {};
  address.sun_family = AF_UNIX;
  std::strncpy(address.sun_path, path.c_str(), sizeof(address.sun_path) - 1);
  const Deadline deadline = std::chrono::steady_clock::now() + kExchangeWait;
  Bytes local_frame;
  EncodeProposal(&local_frame, local);
  if (local_frame.size() > kMaxFrameBytes) {
    SetError(error, "local map proposal exceeds exchange frame");
    return false;
  }

  if (local.rank != 0) {
    int fd = socket(AF_UNIX, SOCK_STREAM, 0);
    if (fd < 0) {
      SetError(error, "failed to create exchange socket");
      return false;
    }
    while (connect(fd, reinterpret_cast<sockaddr *>(&address),
                   sizeof(address)) != 0) {
      if (errno != ENOENT && errno != ECONNREFUSED) {
        close(fd);
        SetError(error, "failed to connect to rank-zero exchange");
        return false;
      }
      if (std::chrono::steady_clock::now() >= deadline) {
        close(fd);
        SetError(error, "rank-zero exchange unavailable");
        return false;
      }
      usleep(1000);
    }
    Bytes all_frame;
    const bool exchanged = SendFrame(fd, local_frame, deadline) &&
                           ReceiveFrame(fd, &all_frame, deadline);
    close(fd);
    std::vector<RankProposal> proposals;
    if (!exchanged || !DecodeAll(all_frame, &proposals)) {
      SetError(error, "invalid rank-zero exchange response");
      return false;
    }
    return MergeRankProposals(std::move(proposals), snapshot, error);
  }

  int listener = socket(AF_UNIX, SOCK_STREAM, 0);
  if (listener < 0) {
    SetError(error, "failed to create rank-zero socket");
    return false;
  }
  if (bind(listener, reinterpret_cast<sockaddr *>(&address),
           sizeof(address)) != 0) {
    if (errno != EADDRINUSE) {
      close(listener);
      SetError(error, "failed to bind rank-zero exchange socket");
      return false;
    }
    int probe = socket(AF_UNIX, SOCK_STREAM, 0);
    const int probe_rc =
        probe >= 0 ? connect(probe, reinterpret_cast<sockaddr *>(&address),
                             sizeof(address)) : 0;
    const int probe_errno = errno;
    if (probe >= 0)
      close(probe);
    if (probe < 0 || probe_rc == 0 || probe_errno != ECONNREFUSED ||
        unlink(path.c_str()) != 0 ||
        bind(listener, reinterpret_cast<sockaddr *>(&address),
             sizeof(address)) != 0) {
      close(listener);
      SetError(error, "rank-zero exchange socket is already in use");
      return false;
    }
  }
  if (listen(listener, static_cast<int>(local.n_ranks)) != 0) {
    close(listener);
    SetError(error, "failed to bind rank-zero exchange socket");
    return false;
  }
  chmod(path.c_str(), 0600);
  std::vector<RankProposal> proposals{local};
  std::vector<int> peers;
  bool ok = true;
  while (proposals.size() < local.n_ranks) {
    if (std::chrono::steady_clock::now() >= deadline) {
      SetError(error, "not all ranks joined the map exchange");
      ok = false;
      break;
    }
    fd_set readfds;
    FD_ZERO(&readfds);
    FD_SET(listener, &readfds);
    timeval wait = {.tv_sec = 0, .tv_usec = 100000};
    const int ready = select(listener + 1, &readfds, nullptr, nullptr, &wait);
    if (ready <= 0)
      continue;
    int peer = accept(listener, nullptr, nullptr);
    Bytes frame;
    RankProposal proposal;
    size_t offset = 0;
    if (peer < 0 || !ReceiveFrame(peer, &frame, deadline) ||
        !DecodeProposal(frame, &offset, &proposal) ||
        offset != frame.size()) {
      if (peer >= 0)
        close(peer);
      SetError(error, "invalid peer map proposal");
      ok = false;
      break;
    }
    peers.push_back(peer);
    proposals.push_back(std::move(proposal));
  }
  if (ok)
    ok = MergeRankProposals(proposals, snapshot, error);
  if (ok) {
    Bytes all_frame = EncodeAll(proposals);
    if (all_frame.size() > kMaxFrameBytes) {
      SetError(error, "merged rank proposal set exceeds exchange frame");
      ok = false;
    } else {
      for (int peer : peers)
        if (!SendFrame(peer, all_frame, deadline)) {
          SetError(error, "failed to send merged proposals");
          ok = false;
        }
    }
  }
  for (int peer : peers)
    close(peer);
  close(listener);
  unlink(path.c_str());
  return ok;
}

}  // namespace ncclbpf

