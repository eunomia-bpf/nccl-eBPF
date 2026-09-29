#ifndef NCCLBPF_DISTRIBUTED_MAP_H_
#define NCCLBPF_DISTRIBUTED_MAP_H_

#include <cstdint>
#include <string>
#include <vector>

namespace ncclbpf {

using Bytes = std::vector<uint8_t>;

struct MapSchema {
  std::string name;
  uint32_t key_size = 0;
  uint32_t value_size = 0;
  uint32_t max_entries = 0;
};

struct MapWrite {
  std::string map;
  Bytes key;
  Bytes value;
};

struct RankProposal {
  uint32_t rank = 0;
  uint32_t n_ranks = 0;
  uint64_t policy_version = 0;
  uint64_t round = 0;
  uint64_t activation_call = 0;
  std::vector<MapSchema> maps;
  std::vector<MapWrite> writes;
};

struct RankedMapWrite {
  uint32_t rank = 0;
  MapWrite write;
};

struct AgreedSnapshot {
  uint64_t policy_version = 0;
  uint64_t round = 0;
  uint64_t activation_call = 0;
  std::vector<RankedMapWrite> rank_writes;
};

// All participants independently merge the complete rank proposal set.
// Rank-indexed slots remain separate, including when two ranks write the
// same map/key. A verified merge program consumes these identical inputs.
bool MergeRankProposals(std::vector<RankProposal> proposals,
                        AgreedSnapshot *snapshot, std::string *error);

// The rank-zero process coordinates one exchange over a same-host Unix socket.
// This function is blocking and must run outside the NCCL getCollInfo path.
// It returns only after the local rank has received the full proposal set and
// independently verified the merged snapshot.
bool ExchangeSameHost(uint64_t communicator_id, const RankProposal &local,
                      AgreedSnapshot *snapshot, std::string *error);

}  // namespace ncclbpf

#endif  // NCCLBPF_DISTRIBUTED_MAP_H_

