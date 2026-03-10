#include <atomic>
#include <bpf/libbpf.h>
#include <cuda_runtime_api.h>
#include <fcntl.h>
#include <gelf.h>
#include <inttypes.h>
#include <linux/bpf.h>
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#include <algorithm>
#include <cerrno>
#include <map>
#include <memory>
#include <mutex>
#include <new>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

#include "bpftime-verifier.hpp"
#include "bpftime_config.hpp"
#include "bpftime_helper_group.hpp"
#include "bpftime_prog.hpp"
#include "bpftime_shm.hpp"
#include "nccl_tuner.h"
#include "nccl_profiler.h"

#include "../ebpf-policies/policy_action.h"
#include "../ebpf-policies/policy_context.h"
#include "../ebpf-policies/policy_maps.h"

namespace {

constexpr ebpf_inst kHardcodedNoopProgram[] = {
    {
        .code = static_cast<uint8_t>(BPF_ALU64 | BPF_MOV | BPF_K),
        .dst_reg = BPF_REG_0,
        .src_reg = 0,
        .off = 0,
        .imm = 0,
    },
    {
        .code = static_cast<uint8_t>(BPF_JMP | BPF_EXIT),
        .dst_reg = 0,
        .src_reg = 0,
        .off = 0,
        .imm = 0,
    },
};

static_assert(sizeof(ebpf_inst) == sizeof(uint64_t),
              "bpftime verifier expects raw 64-bit instructions");

enum class VerifyMode {
  kStrict,
  kWarning,
  kNone,
};

struct SharedCommState {
  ncclDebugLogger_t log_function = nullptr;
  uint64_t comm_id = 0;
  size_t n_ranks = 0;
  size_t n_nodes = 0;
  std::mutex reload_mu;
  std::mutex profiler_mu;
  size_t tuner_refs = 0;
  size_t profiler_refs = 0;
  uint64_t profiler_write_count = 0;
  struct LoadedPolicyState {
    ~LoadedPolicyState() {
      for (const auto &entry : map_fds) {
        if (entry.second >= 0)
          bpftime_close(entry.second);
      }
    }

    std::unique_ptr<bpftime::bpftime_prog> prog;
    bool loaded_from_file = false;
    std::string policy_source = "hardcoded-noop";
    std::string section_name = "uprobe";
    std::unordered_map<std::string, int> map_fds;
    std::map<int, bpftime::verifier::BpftimeMapDescriptor> verifier_maps;
    mutable std::mutex exec_mu;
  };
  std::shared_ptr<LoadedPolicyState> active_policy;
  struct CollectiveEvent;
  struct CeCollectiveEvent;
  std::unordered_set<CollectiveEvent *> open_collectives;
  std::unordered_set<CeCollectiveEvent *> pending_ce_events;
};

struct TunerContext {
  std::shared_ptr<SharedCommState> shared;
  std::mutex stats_mu;
  uint64_t call_count = 0;
  uint64_t total_latency_ns = 0;
  uint64_t last_latency_ns = 0;
  uint64_t rolling_p99_ns = 0;
  int last_channels = 0;
  struct SyntheticTelemetryState {
    bool enabled = false;
    uint64_t last_latency_ns = 0;
    uint64_t avg_latency_ns = 0;
    uint64_t rolling_p99_ns = 0;
  } synthetic_telemetry;
};

struct ProfilerContext {
  std::shared_ptr<SharedCommState> shared;
  std::string comm_name;
  int rank = -1;
  int activation_mask = 0;
};

struct SharedCommState::CollectiveEvent {
  uint64_t type = ncclProfileColl;
  SharedCommState *shared = nullptr;
  uint32_t coll_type = 0;
  size_t n_bytes = 0;
  int rank = -1;
  uint64_t start_ns = 0;
  uint64_t host_stop_ns = 0;
  uint64_t max_kernel_latency_ns = 0;
  int expected_kernel_events = 0;
  int completed_kernel_events = 0;
  int pending_kernel_events = 0;
  bool host_stopped = false;
  bool saw_kernel_event = false;
};

struct KernelChannelEvent {
  uint64_t type = ncclProfileKernelCh;
  SharedCommState::CollectiveEvent *parent = nullptr;
  uint8_t channel_id = 0;
  uint64_t start_ptimer_ns = 0;
  uint64_t stop_ptimer_ns = 0;
};

struct SharedCommState::CeCollectiveEvent {
  uint64_t type = ncclProfileCeColl;
  SharedCommState *shared = nullptr;
  uint32_t coll_type = 0;
  size_t n_bytes = 0;
  int rank = -1;
  cudaStream_t stream = nullptr;
  cudaEvent_t start_event = nullptr;
  cudaEvent_t stop_event = nullptr;
  uint64_t start_ns = 0;
  uint64_t host_stop_ns = 0;
  bool stop_recorded = false;
};

struct ProgramSpec {
  std::string name;
  std::string section_name;
  std::vector<ebpf_inst> insns;
};

struct MapSymbol {
  std::string name;
  size_t section_index = 0;
  uint64_t value = 0;
};

struct SeededPolicyConfig {
  uint64_t target_p99_ns = 150;
  uint32_t min_channels = 1;
  uint32_t max_channels = 8;
  uint32_t aggressiveness_step = 1;
};

struct SyntheticTelemetryConfig {
  uint64_t last_latency_ns = 0;
  uint64_t avg_latency_ns = 0;
  uint64_t rolling_p99_ns = 0;
  uint32_t enabled = 0;
  uint32_t reserved = 0;
};

struct ReloadDebugStats {
  uint64_t load_ns = 0;
  uint64_t swap_ns = 0;
  uint64_t total_ns = 0;
};

std::mutex g_runtime_mu;
size_t g_runtime_users = 0;
std::mutex g_comm_registry_mu;
std::unordered_map<uint64_t, std::weak_ptr<SharedCommState>> g_comm_registry;
std::atomic<uint64_t> g_policy_instance_ids{1};

void ensure_bpftime_shm_name() {
  const char *existing = getenv("BPFTIME_GLOBAL_SHM_NAME");
  char buffer[128];

  if (existing && existing[0] != '\0')
    return;

  snprintf(buffer, sizeof(buffer), "nccl_policy_bpftime_%u_%d",
           static_cast<unsigned>(geteuid()), static_cast<int>(getpid()));
  setenv("BPFTIME_GLOBAL_SHM_NAME", buffer, 0);
}

uint64_t monotonic_time_ns() {
  struct timespec ts;
  clock_gettime(CLOCK_MONOTONIC_RAW, &ts);
  return static_cast<uint64_t>(ts.tv_sec) * 1000000000ull +
         static_cast<uint64_t>(ts.tv_nsec);
}

uint64_t update_p99_estimate(uint64_t current_p99, uint64_t sample_ns) {
  if (current_p99 == 0 || sample_ns > current_p99)
    return sample_ns;
  return (current_p99 * 99 + sample_ns) / 100;
}

void log_plugin_message(ncclDebugLogger_t log_function, unsigned long flags,
                        ncclDebugLogLevel level, const char *fmt, ...) {
  char buffer[4096];
  va_list args;
  va_start(args, fmt);
  vsnprintf(buffer, sizeof(buffer), fmt, args);
  va_end(args);

  fprintf(stderr, "[nccl-policy-plugin] %s\n", buffer);
  if (log_function)
    log_function(level, flags, __FILE__, __LINE__, "%s", buffer);
}

std::shared_ptr<SharedCommState>
acquire_shared_comm_state(uint64_t comm_id, size_t n_ranks, size_t n_nodes,
                          ncclDebugLogger_t log_function, bool tuner_ref) {
  std::lock_guard<std::mutex> lock(g_comm_registry_mu);
  auto &entry = g_comm_registry[comm_id];
  auto shared = entry.lock();

  if (!shared) {
    shared = std::make_shared<SharedCommState>();
    shared->comm_id = comm_id;
    entry = shared;
  }

  if (shared->n_ranks == 0)
    shared->n_ranks = n_ranks;
  if (shared->n_nodes == 0)
    shared->n_nodes = n_nodes;
  if (log_function)
    shared->log_function = log_function;

  if (tuner_ref)
    shared->tuner_refs++;
  else
    shared->profiler_refs++;

  return shared;
}

bool release_shared_comm_state(const std::shared_ptr<SharedCommState> &shared,
                               bool tuner_ref) {
  bool last_profiler_ref = false;

  if (!shared)
    return true;

  std::lock_guard<std::mutex> lock(g_comm_registry_mu);
  if (tuner_ref) {
    if (shared->tuner_refs > 0)
      shared->tuner_refs--;
  } else {
    if (shared->profiler_refs > 0)
      shared->profiler_refs--;
    last_profiler_ref = shared->profiler_refs == 0;
  }

  if (shared->tuner_refs == 0 && shared->profiler_refs == 0) {
    auto it = g_comm_registry.find(shared->comm_id);
    if (it != g_comm_registry.end() && it->second.lock().get() == shared.get())
      g_comm_registry.erase(it);
  }

  return last_profiler_ref;
}

void acquire_bpftime_runtime() {
  std::lock_guard<std::mutex> lock(g_runtime_mu);
  if (g_runtime_users++ == 0) {
    ensure_bpftime_shm_name();
    bpftime_initialize_global_shm(
        bpftime::shm_open_type::SHM_REMOVE_AND_CREATE);
    auto config = bpftime::construct_agent_config_from_env();
    config.set_vm_name("llvm");
    config.jit_enabled = true;
    config.enable_kernel_helper_group = true;
    config.enable_shm_maps_helper_group = true;
    bpftime::bpftime_set_agent_config(std::move(config));
  }
}

void release_bpftime_runtime() {
  std::lock_guard<std::mutex> lock(g_runtime_mu);
  if (g_runtime_users == 0)
    return;
  if (--g_runtime_users == 0) {
    bpftime_destroy_global_shm();
    bpftime_remove_global_shm();
  }
}

VerifyMode get_verify_mode() {
  const char *mode = getenv("NCCL_POLICY_VERIFY_MODE");
  if (!mode || mode[0] == '\0')
    return VerifyMode::kStrict;

  if (strcmp(mode, "none") == 0)
    return VerifyMode::kNone;
  if (strcmp(mode, "warning") == 0 || strcmp(mode, "warn") == 0)
    return VerifyMode::kWarning;
  return VerifyMode::kStrict;
}

uint64_t parse_env_u64(const char *name, uint64_t fallback) {
  const char *value = getenv(name);
  char *end = nullptr;

  if (!value || value[0] == '\0')
    return fallback;

  errno = 0;
  unsigned long long parsed = strtoull(value, &end, 10);
  if (errno != 0 || end == value || *end != '\0')
    return fallback;
  return static_cast<uint64_t>(parsed);
}

bool verifier_supports_section(const std::string &section_name) {
  return section_name.rfind("uprobe", 0) == 0 ||
         section_name.rfind("uretprobe", 0) == 0 ||
         section_name.rfind("tracepoint", 0) == 0;
}

std::string verifier_section_name(const std::string &section_name) {
  if (verifier_supports_section(section_name))
    return section_name;
  return "uprobe";
}

bool collect_map_helpers(std::vector<int32_t> *helper_ids) {
  bpftime::bpftime_helper_group helpers =
      bpftime::bpftime_helper_group::get_kernel_utils_helper_group();
  if (helpers.append(
          bpftime::bpftime_helper_group::get_shm_maps_helper_group()) < 0)
    return false;

  *helper_ids = helpers.get_helper_ids();
  std::sort(helper_ids->begin(), helper_ids->end());
  helper_ids->erase(std::unique(helper_ids->begin(), helper_ids->end()),
                    helper_ids->end());
  return true;
}

bool register_helpers(bpftime::bpftime_prog *prog) {
  bpftime::bpftime_helper_group helpers =
      bpftime::bpftime_helper_group::get_kernel_utils_helper_group();
  if (helpers.append(
          bpftime::bpftime_helper_group::get_shm_maps_helper_group()) < 0)
    return false;
  return helpers.add_helper_group_to_prog(prog) == 0;
}

uint32_t parse_seeded_channel_setting(SharedCommState *shared, const char *name,
                                      uint32_t fallback) {
  const uint64_t parsed = parse_env_u64(name, fallback);

  if (parsed == 0) {
    log_plugin_message(shared ? shared->log_function : nullptr, NCCL_TUNING,
                       NCCL_LOG_WARN,
                       "%s must be at least 1, using fallback %u", name,
                       fallback);
    return fallback;
  }
  if (parsed > UINT8_MAX) {
    log_plugin_message(shared ? shared->log_function : nullptr, NCCL_TUNING,
                       NCCL_LOG_WARN,
                       "%s=%" PRIu64 " exceeds action ABI limit %u, clamping",
                       name, parsed, UINT8_MAX);
    return UINT8_MAX;
  }
  return static_cast<uint32_t>(parsed);
}

SeededPolicyConfig load_seeded_policy_config(SharedCommState *shared) {
  SeededPolicyConfig cfg;

  cfg.target_p99_ns = parse_env_u64("NCCL_POLICY_SLO_TARGET_NS", 150);
  cfg.min_channels =
      parse_seeded_channel_setting(shared, "NCCL_POLICY_MIN_CHANNELS", 1);
  cfg.max_channels =
      parse_seeded_channel_setting(shared, "NCCL_POLICY_MAX_CHANNELS", 8);
  cfg.aggressiveness_step =
      parse_seeded_channel_setting(shared, "NCCL_POLICY_AGGRESSIVENESS_STEP",
                                   1);

  if (cfg.max_channels < cfg.min_channels) {
    log_plugin_message(shared ? shared->log_function : nullptr, NCCL_TUNING,
                       NCCL_LOG_WARN,
                       "NCCL_POLICY_MAX_CHANNELS=%u is smaller than "
                       "NCCL_POLICY_MIN_CHANNELS=%u, raising max to match min",
                       cfg.max_channels, cfg.min_channels);
    cfg.max_channels = cfg.min_channels;
  }

  return cfg;
}

std::shared_ptr<SharedCommState::LoadedPolicyState>
load_active_policy(SharedCommState *shared) {
  return std::atomic_load_explicit(&shared->active_policy,
                                   std::memory_order_acquire);
}

void store_active_policy(
    SharedCommState *shared,
    std::shared_ptr<SharedCommState::LoadedPolicyState> policy_state) {
  std::atomic_store_explicit(&shared->active_policy, std::move(policy_state),
                             std::memory_order_release);
}

std::shared_ptr<SharedCommState::LoadedPolicyState> exchange_active_policy(
    SharedCommState *shared,
    std::shared_ptr<SharedCommState::LoadedPolicyState> policy_state) {
  return std::atomic_exchange_explicit(
      &shared->active_policy, std::move(policy_state),
      std::memory_order_acq_rel);
}

bool create_bpftime_maps(SharedCommState *shared,
                         SharedCommState::LoadedPolicyState *policy_state,
                         struct bpf_object *obj) {
  struct bpf_map *map = nullptr;
  const uint64_t policy_instance_id =
      g_policy_instance_ids.fetch_add(1, std::memory_order_relaxed);
  const std::string map_namespace =
      "comm_" + std::to_string(shared ? shared->comm_id : 0) + "_policy_" +
      std::to_string(policy_instance_id) + "_";

  bpf_object__for_each_map(map, obj) {
    bpftime::bpf_map_attr attr = {};
    const char *logical_name = bpf_map__name(map);
    const std::string runtime_name =
        map_namespace + (logical_name ? logical_name : "unnamed_map");
    int fd = -1;

    if (bpf_map__is_internal(map))
      continue;

    attr.type = static_cast<int>(bpf_map__type(map));
    attr.key_size = bpf_map__key_size(map);
    attr.value_size = bpf_map__value_size(map);
    attr.max_ents = bpf_map__max_entries(map);
    attr.flags = bpf_map__map_flags(map);
    attr.ifindex = bpf_map__ifindex(map);
    attr.btf_key_type_id = bpf_map__btf_key_type_id(map);
    attr.btf_value_type_id = bpf_map__btf_value_type_id(map);
    attr.map_extra = bpf_map__map_extra(map);

    fd = bpftime_maps_create(-1, runtime_name.c_str(), attr);
    if (fd < 0) {
      log_plugin_message(shared ? shared->log_function : nullptr, NCCL_TUNING,
                         NCCL_LOG_WARN,
                         "failed to create bpftime map %s (runtime=%s)",
                         logical_name ? logical_name : "unnamed_map",
                         runtime_name.c_str());
      return false;
    }
    policy_state->map_fds.emplace(logical_name ? logical_name : "", fd);
    policy_state->verifier_maps.emplace(
        fd, bpftime::verifier::BpftimeMapDescriptor{
                .original_fd = fd,
                .type = static_cast<uint32_t>(attr.type),
                .key_size = attr.key_size,
                .value_size = attr.value_size,
                .max_entries = attr.max_ents,
                .inner_map_fd = static_cast<unsigned int>(-1),
            });
  }

  return true;
}

bool extract_program_spec(SharedCommState *shared, struct bpf_object *obj,
                          ProgramSpec *spec) {
  struct bpf_program *prog = bpf_object__next_program(obj, nullptr);
  const struct bpf_insn *insns = nullptr;
  size_t insn_cnt = 0;

  if (!prog) {
    log_plugin_message(shared ? shared->log_function : nullptr, NCCL_TUNING,
                       NCCL_LOG_WARN, "no program found in BPF object");
    return false;
  }

  spec->name = bpf_program__name(prog) ? bpf_program__name(prog) : "policy";
  spec->section_name = bpf_program__section_name(prog)
                           ? bpf_program__section_name(prog)
                           : "uprobe";
  insns = bpf_program__insns(prog);
  insn_cnt = bpf_program__insn_cnt(prog);
  if (!insns || insn_cnt == 0)
    return false;

  spec->insns.resize(insn_cnt);
  memcpy(spec->insns.data(), insns, insn_cnt * sizeof(spec->insns[0]));
  return !spec->insns.empty();
}

bool relocate_program_maps(SharedCommState *shared, const char *path,
                           SharedCommState::LoadedPolicyState *policy_state,
                           ProgramSpec *spec) {
  int fd = -1;
  Elf *elf = nullptr;
  Elf_Scn *scn = nullptr;
  Elf_Scn *symtab_scn = nullptr;
  GElf_Shdr symtab_shdr = {};
  size_t shstrndx = 0;
  size_t target_sec_index = 0;
  std::unordered_map<size_t, MapSymbol> symbols;
  std::vector<ebpf_inst> relocated = spec->insns;

  if (policy_state->map_fds.empty())
    return true;

  if (elf_version(EV_CURRENT) == EV_NONE) {
    log_plugin_message(shared ? shared->log_function : nullptr, NCCL_TUNING,
                       NCCL_LOG_WARN, "libelf initialization failed");
    return false;
  }

  fd = open(path, O_RDONLY);
  if (fd < 0) {
    log_plugin_message(shared ? shared->log_function : nullptr, NCCL_TUNING,
                       NCCL_LOG_WARN, "failed to open %s for relocation", path);
    return false;
  }

  elf = elf_begin(fd, ELF_C_READ, nullptr);
  if (!elf) {
    log_plugin_message(shared ? shared->log_function : nullptr, NCCL_TUNING,
                       NCCL_LOG_WARN, "elf_begin failed for %s", path);
    close(fd);
    return false;
  }

  if (elf_getshdrstrndx(elf, &shstrndx) != 0) {
    log_plugin_message(shared ? shared->log_function : nullptr, NCCL_TUNING,
                       NCCL_LOG_WARN, "elf_getshdrstrndx failed for %s", path);
    elf_end(elf);
    close(fd);
    return false;
  }

  while ((scn = elf_nextscn(elf, scn)) != nullptr) {
    GElf_Shdr shdr = {};
    const char *section_name = nullptr;

    if (!gelf_getshdr(scn, &shdr))
      continue;

    section_name = elf_strptr(elf, shstrndx, shdr.sh_name);
    if (section_name && spec->section_name == section_name)
      target_sec_index = elf_ndxscn(scn);

    if (shdr.sh_type == SHT_SYMTAB) {
      symtab_scn = scn;
      symtab_shdr = shdr;
    }
  }

  if (target_sec_index == 0 || !symtab_scn) {
    log_plugin_message(shared ? shared->log_function : nullptr, NCCL_TUNING,
                       NCCL_LOG_WARN,
                       "unable to find symtab or target section for %s", path);
    elf_end(elf);
    close(fd);
    return false;
  }

  {
    Elf_Data *data = elf_getdata(symtab_scn, nullptr);
    size_t symbol_count = 0;

    if (!data) {
      log_plugin_message(shared ? shared->log_function : nullptr, NCCL_TUNING,
                         NCCL_LOG_WARN, "symtab data missing in %s", path);
      elf_end(elf);
      close(fd);
      return false;
    }
    if (symtab_shdr.sh_entsize == 0) {
      log_plugin_message(shared ? shared->log_function : nullptr, NCCL_TUNING,
                         NCCL_LOG_WARN, "symtab entry size is zero in %s",
                         path);
      elf_end(elf);
      close(fd);
      return false;
    }

    symbol_count = symtab_shdr.sh_size / symtab_shdr.sh_entsize;
    for (size_t i = 0; i < symbol_count; ++i) {
      GElf_Sym sym = {};
      const char *name = nullptr;

      if (!gelf_getsym(data, static_cast<int>(i), &sym))
        continue;

      name = elf_strptr(elf, symtab_shdr.sh_link, sym.st_name);
      symbols.emplace(i, MapSymbol{
                             .name = name ? name : "",
                             .section_index = sym.st_shndx,
                             .value = sym.st_value,
                         });
    }
  }

  scn = nullptr;
  while ((scn = elf_nextscn(elf, scn)) != nullptr) {
    GElf_Shdr shdr = {};
    Elf_Data *data = nullptr;
    const bool is_relocation =
        gelf_getshdr(scn, &shdr) &&
        (shdr.sh_type == SHT_REL || shdr.sh_type == SHT_RELA) &&
        shdr.sh_info == target_sec_index;

    if (!is_relocation)
      continue;

    data = elf_getdata(scn, nullptr);
    if (!data)
      continue;
    if (shdr.sh_entsize == 0) {
      log_plugin_message(shared ? shared->log_function : nullptr, NCCL_TUNING,
                         NCCL_LOG_WARN,
                         "relocation entry size is zero in %s", path);
      elf_end(elf);
      close(fd);
      return false;
    }

    const size_t reloc_count = shdr.sh_size / shdr.sh_entsize;
    for (size_t i = 0; i < reloc_count; ++i) {
      uint64_t offset = 0;
      size_t symbol_index = 0;
      auto symbol_it = symbols.end();
      auto map_it = policy_state->map_fds.end();

      if (shdr.sh_type == SHT_REL) {
        GElf_Rel rel = {};
        if (!gelf_getrel(data, static_cast<int>(i), &rel))
          continue;
        offset = rel.r_offset;
        symbol_index = GELF_R_SYM(rel.r_info);
      } else {
        GElf_Rela rela = {};
        if (!gelf_getrela(data, static_cast<int>(i), &rela))
          continue;
        offset = rela.r_offset;
        symbol_index = GELF_R_SYM(rela.r_info);
      }

      if (offset % sizeof(ebpf_inst) != 0)
        continue;
      if (offset / sizeof(ebpf_inst) >= relocated.size())
        continue;

      symbol_it = symbols.find(symbol_index);
      if (symbol_it == symbols.end())
        continue;

      map_it = policy_state->map_fds.find(symbol_it->second.name);
      if (map_it == policy_state->map_fds.end())
        continue;

      relocated[offset / sizeof(ebpf_inst)].src_reg = BPF_PSEUDO_MAP_FD;
      relocated[offset / sizeof(ebpf_inst)].imm = map_it->second;
    }
  }

  elf_end(elf);
  close(fd);

  spec->insns = std::move(relocated);
  return true;
}

bool verify_program(SharedCommState *shared,
                    const SharedCommState::LoadedPolicyState *policy_state,
                    const ProgramSpec &spec) {
  std::vector<int32_t> helper_ids;
  const VerifyMode mode = get_verify_mode();
  const std::string verify_section = verifier_section_name(spec.section_name);

  if (mode == VerifyMode::kNone)
    return true;

  if (!collect_map_helpers(&helper_ids)) {
    log_plugin_message(shared ? shared->log_function : nullptr, NCCL_TUNING,
                       NCCL_LOG_WARN,
                       "failed to assemble helper ids for verifier");
    return mode != VerifyMode::kStrict;
  }

  bpftime::verifier::set_map_descriptors(policy_state->verifier_maps);
  bpftime::verifier::set_available_helpers(helper_ids);
  bpftime::verifier::set_non_kernel_helpers({});

  if (verify_section != spec.section_name) {
    log_plugin_message(shared ? shared->log_function : nullptr, NCCL_TUNING,
                       NCCL_LOG_INFO,
                       "verifier does not support section %s, using %s rules",
                       spec.section_name.c_str(), verify_section.c_str());
  }

  auto result = bpftime::verifier::verify_ebpf_program(
      reinterpret_cast<const uint64_t *>(spec.insns.data()), spec.insns.size(),
      verify_section);
  if (!result)
    return true;

  if (mode == VerifyMode::kStrict) {
    fprintf(stderr, "[nccl-policy-plugin] verifier rejected %s:\n%s\n",
            policy_state->policy_source.c_str(), result->c_str());
    if (shared && shared->log_function) {
      shared->log_function(NCCL_LOG_WARN, NCCL_TUNING, __FILE__, __LINE__,
                        "verifier rejected %s",
                        policy_state->policy_source.c_str());
    }
    return false;
  }

  log_plugin_message(shared ? shared->log_function : nullptr, NCCL_TUNING,
                     NCCL_LOG_WARN, "verifier warning for %s: %s",
                     policy_state->policy_source.c_str(), result->c_str());
  return true;
}

void seed_config_map(SharedCommState *shared,
                     SharedCommState::LoadedPolicyState *policy_state) {
  auto map_it = policy_state->map_fds.find("config_map");
  const SeededPolicyConfig cfg = load_seeded_policy_config(shared);

  if (map_it == policy_state->map_fds.end())
    return;

  for (uint32_t coll_type = 0; coll_type <= NCCL_POLICY_COLL_ALLREDUCE;
       ++coll_type) {
    const nccl_policy_config_key key = {.coll_type = coll_type};
    const nccl_policy_config_value value = {
        .target_p99_ns = cfg.target_p99_ns,
        .min_channels = cfg.min_channels,
        .max_channels = cfg.max_channels,
        .aggressiveness_step = cfg.aggressiveness_step,
    };
    (void)bpftime_map_update_elem(map_it->second, &key, &value, BPF_ANY);
  }
}

bool load_hardcoded_program(SharedCommState *shared,
                            SharedCommState::LoadedPolicyState *policy_state) {
  auto config = bpftime::construct_agent_config_from_env();
  config.set_vm_name("llvm");
  policy_state->prog = std::make_unique<bpftime::bpftime_prog>(
      kHardcodedNoopProgram, std::size(kHardcodedNoopProgram), "hardcoded-noop",
      std::move(config));
  if (!policy_state->prog || !register_helpers(policy_state->prog.get()))
    return false;
  if (policy_state->prog->bpftime_prog_load(true) < 0) {
    log_plugin_message(shared ? shared->log_function : nullptr, NCCL_TUNING,
                       NCCL_LOG_WARN,
                       "failed to load hardcoded noop policy");
    return false;
  }
  policy_state->policy_source = "hardcoded-noop";
  policy_state->section_name = "uprobe";
  return true;
}

bool load_program_from_object(SharedCommState *shared,
                              SharedCommState::LoadedPolicyState *policy_state,
                              const char *path) {
  std::unique_ptr<struct bpf_object, decltype(&bpf_object__close)> obj(
      bpf_object__open_file(path, nullptr), &bpf_object__close);
  ProgramSpec spec;
  auto config = bpftime::construct_agent_config_from_env();

  if (!obj) {
    log_plugin_message(shared ? shared->log_function : nullptr, NCCL_TUNING,
                       NCCL_LOG_WARN, "unable to open BPF object %s", path);
    return false;
  }

  policy_state->policy_source = path;

  if (!extract_program_spec(shared, obj.get(), &spec)) {
    log_plugin_message(shared ? shared->log_function : nullptr, NCCL_TUNING,
                       NCCL_LOG_WARN,
                       "unable to extract a program from %s", path);
    return false;
  }
  if (!create_bpftime_maps(shared, policy_state, obj.get()))
    return false;
  if (!relocate_program_maps(shared, path, policy_state, &spec))
    return false;
  if (!verify_program(shared, policy_state, spec))
    return false;

  config.set_vm_name("llvm");
  policy_state->prog = std::make_unique<bpftime::bpftime_prog>(
      spec.insns.data(), spec.insns.size(), spec.name.c_str(),
      std::move(config));
  if (!policy_state->prog) {
    log_plugin_message(shared ? shared->log_function : nullptr, NCCL_TUNING,
                       NCCL_LOG_WARN,
                       "failed to allocate bpftime_prog for %s", path);
    return false;
  }
  if (!register_helpers(policy_state->prog.get())) {
    log_plugin_message(shared ? shared->log_function : nullptr, NCCL_TUNING,
                       NCCL_LOG_WARN,
                       "failed to register helper groups for %s", path);
    return false;
  }
  if (policy_state->prog->bpftime_prog_load(true) < 0) {
    log_plugin_message(shared ? shared->log_function : nullptr, NCCL_TUNING,
                       NCCL_LOG_WARN, "failed to JIT-load policy %s", path);
    return false;
  }

  policy_state->loaded_from_file = true;
  policy_state->section_name = spec.section_name;
  seed_config_map(shared, policy_state);
  return true;
}

bool warmup_program(SharedCommState *shared,
                    SharedCommState::LoadedPolicyState *policy_state) {
  struct nccl_policy_ctx warmup_ctx = {};
  uint64_t action = 0;
  const uint64_t start_ns = monotonic_time_ns();
  int err = 0;

  if (!policy_state->map_fds.empty()) {
    // Map-backed policies are stateful. Skip synthetic warmup so the first real
    // getCollInfo() call observes empty policy state.
    return true;
  }

  warmup_ctx.n_bytes = 1024;
  warmup_ctx.coll_type = NCCL_POLICY_COLL_ALLREDUCE;
  warmup_ctx.num_pipe_ops = 1;
  warmup_ctx.n_ranks = static_cast<uint32_t>(shared->n_ranks);
  warmup_ctx.n_nodes = static_cast<uint32_t>(shared->n_nodes);
  warmup_ctx.current_channels = 1;

  err = policy_state->prog->bpftime_prog_exec(&warmup_ctx, sizeof(warmup_ctx),
                                              &action);
  if (err < 0) {
    log_plugin_message(shared ? shared->log_function : nullptr, NCCL_TUNING,
                       NCCL_LOG_WARN, "warmup execution failed for %s",
                       policy_state->policy_source.c_str());
    return false;
  }

  fprintf(stderr,
          "[nccl-policy-plugin] init warmup bytes=%" PRIu64 " action=%" PRIu64
          " latency_ns=%" PRIu64 "\n",
          warmup_ctx.n_bytes, action, monotonic_time_ns() - start_ns);
  return true;
}

std::shared_ptr<SharedCommState::LoadedPolicyState>
load_policy_state(SharedCommState *shared, const char *policy_path) {
  auto policy_state = std::make_shared<SharedCommState::LoadedPolicyState>();
  bool loaded = false;

  if (policy_path && policy_path[0] != '\0') {
    loaded = load_program_from_object(shared, policy_state.get(), policy_path);
  } else {
    loaded = load_hardcoded_program(shared, policy_state.get());
  }
  if (!loaded || !policy_state->prog ||
      !warmup_program(shared, policy_state.get())) {
    return nullptr;
  }
  return policy_state;
}

bool ensure_policy_loaded(SharedCommState *shared, const char *policy_path) {
  if (!shared)
    return false;
  if (load_active_policy(shared))
    return true;

  std::lock_guard<std::mutex> lock(shared->reload_mu);
  if (load_active_policy(shared))
    return true;

  auto policy_state = load_policy_state(shared, policy_path);
  if (!policy_state)
    return false;
  store_active_policy(shared, std::move(policy_state));
  return true;
}

bool policy_coll_type_from_nccl_func(ncclFunc_t coll_type, uint32_t *out) {
  if (!out)
    return false;

  switch (coll_type) {
  case ncclFuncBroadcast:
    *out = NCCL_POLICY_COLL_BROADCAST;
    return true;
  case ncclFuncReduce:
    *out = NCCL_POLICY_COLL_REDUCE;
    return true;
  case ncclFuncAllGather:
    *out = NCCL_POLICY_COLL_ALLGATHER;
    return true;
  case ncclFuncReduceScatter:
    *out = NCCL_POLICY_COLL_REDUCESCATTER;
    return true;
  case ncclFuncAllReduce:
    *out = NCCL_POLICY_COLL_ALLREDUCE;
    return true;
  default:
    return false;
  }
}

bool policy_coll_type_from_name(const char *name, uint32_t *out) {
  if (!name || !out)
    return false;
  if (strcmp(name, "Broadcast") == 0) {
    *out = NCCL_POLICY_COLL_BROADCAST;
    return true;
  }
  if (strcmp(name, "Reduce") == 0) {
    *out = NCCL_POLICY_COLL_REDUCE;
    return true;
  }
  if (strcmp(name, "AllGather") == 0) {
    *out = NCCL_POLICY_COLL_ALLGATHER;
    return true;
  }
  if (strcmp(name, "ReduceScatter") == 0) {
    *out = NCCL_POLICY_COLL_REDUCESCATTER;
    return true;
  }
  if (strcmp(name, "AllReduce") == 0) {
    *out = NCCL_POLICY_COLL_ALLREDUCE;
    return true;
  }
  return false;
}

size_t datatype_size_from_name(const char *name) {
  if (!name)
    return 0;
  if (strcmp(name, "ncclInt8") == 0 || strcmp(name, "ncclUint8") == 0 ||
      strcmp(name, "ncclFloat8e4m3") == 0 ||
      strcmp(name, "ncclFloat8e5m2") == 0)
    return 1;
  if (strcmp(name, "ncclFloat16") == 0 || strcmp(name, "ncclBfloat16") == 0)
    return 2;
  if (strcmp(name, "ncclInt32") == 0 || strcmp(name, "ncclUint32") == 0 ||
      strcmp(name, "ncclFloat32") == 0)
    return 4;
  if (strcmp(name, "ncclInt64") == 0 || strcmp(name, "ncclUint64") == 0 ||
      strcmp(name, "ncclFloat64") == 0)
    return 8;
  return 0;
}

int get_map_fd(const SharedCommState::LoadedPolicyState *policy_state,
               const char *name) {
  auto it = policy_state->map_fds.find(name);
  if (it == policy_state->map_fds.end())
    return -1;
  return it->second;
}

bool lookup_telemetry_snapshot(SharedCommState *shared, uint32_t coll_type,
                               nccl_policy_telemetry_value *value) {
  auto policy_state = load_active_policy(shared);
  const nccl_policy_telemetry_value *current = nullptr;
  const nccl_policy_telemetry_key key = {
      .coll_type = coll_type,
      .n_nodes = static_cast<uint32_t>(shared ? shared->n_nodes : 0),
  };
  int map_fd = policy_state ? get_map_fd(policy_state.get(), "telemetry_map")
                            : -1;

  if (!shared || !value || !policy_state || map_fd < 0)
    return false;

  std::lock_guard<std::mutex> lock(policy_state->exec_mu);
  current = reinterpret_cast<const nccl_policy_telemetry_value *>(
      bpftime_map_lookup_elem(map_fd, &key));
  if (!current)
    return false;
  *value = *current;
  return true;
}

bool record_real_telemetry(SharedCommState *shared, uint32_t coll_type,
                           size_t n_bytes, uint64_t latency_ns, int rank,
                           const char *source_tag) {
  auto policy_state = load_active_policy(shared);
  nccl_policy_telemetry_value next = {};
  const nccl_policy_telemetry_value *current = nullptr;
  const nccl_policy_telemetry_key key = {
      .coll_type = coll_type,
      .n_nodes = static_cast<uint32_t>(shared ? shared->n_nodes : 0),
  };
  int map_fd = policy_state ? get_map_fd(policy_state.get(), "telemetry_map")
                            : -1;

  if (!shared || !policy_state || map_fd < 0 || latency_ns == 0)
    return false;

  {
    std::lock_guard<std::mutex> lock(policy_state->exec_mu);
    current = reinterpret_cast<const nccl_policy_telemetry_value *>(
        bpftime_map_lookup_elem(map_fd, &key));
    if (current)
      next = *current;

    next.last_latency_ns = latency_ns;
    next.avg_latency_ns =
        current ? ((next.avg_latency_ns == 0)
                       ? latency_ns
                       : (next.avg_latency_ns * 7 + latency_ns) / 8)
                : latency_ns;
    next.p99_latency_ns = update_p99_estimate(next.p99_latency_ns, latency_ns);
    next.last_n_bytes = n_bytes;
    if (next.samples != UINT32_MAX)
      next.samples += 1;
    bpftime_map_update_elem(map_fd, &key, &next, BPF_ANY);
  }

  shared->profiler_write_count++;
  if (shared->profiler_write_count <= 8 ||
      shared->profiler_write_count % 1000 == 0) {
    log_plugin_message(shared->log_function, NCCL_PROFILE, NCCL_LOG_INFO,
                       "PROFILER/Plugin: %s rank=%d coll=%u bytes=%zu "
                       "latency_ns=%" PRIu64 " samples=%u",
                       source_tag ? source_tag : "telemetry", rank, coll_type,
                       n_bytes, latency_ns, next.samples);
  }
  return true;
}

void maybe_finalize_collective(SharedCommState::CollectiveEvent *event,
                               bool force) {
  uint64_t latency_ns = 0;
  SharedCommState *shared = event ? event->shared : nullptr;
  bool kernels_complete = false;

  if (!event || !shared)
    return;
  kernels_complete =
      event->expected_kernel_events > 0
          ? event->completed_kernel_events >= event->expected_kernel_events
          : (event->saw_kernel_event && event->pending_kernel_events == 0);
  if (!force &&
      (!event->host_stopped || !event->saw_kernel_event || !kernels_complete)) {
    return;
  }

  latency_ns =
      (event->saw_kernel_event && event->max_kernel_latency_ns != 0)
          ? event->max_kernel_latency_ns
          : (event->host_stop_ns > event->start_ns
                 ? event->host_stop_ns - event->start_ns
                 : 0);
  shared->open_collectives.erase(event);
  if (latency_ns != 0) {
    (void)record_real_telemetry(shared, event->coll_type, event->n_bytes,
                                latency_ns, event->rank, "kernel");
  }
  delete event;
}

void destroy_ce_event(SharedCommState::CeCollectiveEvent *event) {
  if (!event)
    return;
  if (event->start_event)
    cudaEventDestroy(event->start_event);
  if (event->stop_event)
    cudaEventDestroy(event->stop_event);
  delete event;
}

void drain_completed_ce_events(SharedCommState *shared, bool synchronize) {
  std::vector<SharedCommState::CeCollectiveEvent *> completed;

  if (!shared)
    return;

  {
    std::lock_guard<std::mutex> lock(shared->profiler_mu);
    for (auto it = shared->pending_ce_events.begin();
         it != shared->pending_ce_events.end();) {
      auto *event = *it;
      cudaError_t status = cudaErrorNotReady;

      if (!event->stop_recorded) {
        ++it;
        continue;
      }

      if (synchronize)
        status = cudaEventSynchronize(event->stop_event);
      else
        status = cudaEventQuery(event->stop_event);

      if (status == cudaSuccess || status != cudaErrorNotReady) {
        completed.push_back(event);
        it = shared->pending_ce_events.erase(it);
        continue;
      }
      ++it;
    }
  }

  for (auto *event : completed) {
    float elapsed_ms = 0.0f;
    uint64_t latency_ns = 0;
    if (cudaEventElapsedTime(&elapsed_ms, event->start_event,
                             event->stop_event) == cudaSuccess) {
      latency_ns = static_cast<uint64_t>(elapsed_ms * 1000000.0f);
    } else if (event->host_stop_ns > event->start_ns) {
      latency_ns = event->host_stop_ns - event->start_ns;
    }
    if (latency_ns != 0) {
      (void)record_real_telemetry(shared, event->coll_type, event->n_bytes,
                                  latency_ns, event->rank, "ce");
    }
    destroy_ce_event(event);
  }
}

void finalize_remaining_profiler_events(SharedCommState *shared) {
  std::vector<SharedCommState::CollectiveEvent *> collectives;

  if (!shared)
    return;

  drain_completed_ce_events(shared, true);

  {
    std::lock_guard<std::mutex> lock(shared->profiler_mu);
    collectives.insert(collectives.end(), shared->open_collectives.begin(),
                       shared->open_collectives.end());
    shared->open_collectives.clear();
  }

  for (auto *event : collectives)
    maybe_finalize_collective(event, true);
}

void apply_policy_action(uint64_t action, float **coll_cost_table, int num_algo,
                         int num_proto, int *n_channels) {
  const uint8_t flags = nccl_policy_action_flags_get(action);
  const int algo = nccl_policy_action_algo(action);
  const int proto = nccl_policy_action_proto(action);
  const int channels = nccl_policy_action_channels(action);

  if ((flags & NCCL_POLICY_ACTION_SET_CHANNELS) && channels > 0)
    *n_channels = channels;

  if (!coll_cost_table)
    return;

  /* NCCL passes collCostTable as float (*)[NCCL_NUM_PROTOCOLS] cast to float**.
   * It is a contiguous 2D array, NOT an array of float* pointers.
   * Treat it as a flat 2D array to avoid interpreting float values as pointers. */
  if ((flags & NCCL_POLICY_ACTION_SET_ALGO) &&
      (flags & NCCL_POLICY_ACTION_SET_PROTO) && algo >= 0 && proto >= 0 &&
      algo < num_algo && proto < num_proto && algo < NCCL_NUM_ALGORITHMS &&
      proto < NCCL_NUM_PROTOCOLS) {
    float (*table)[NCCL_NUM_PROTOCOLS] = (float (*)[NCCL_NUM_PROTOCOLS])coll_cost_table;
    if (table[algo][proto] != NCCL_ALGO_PROTO_IGNORE) {
      /* Set this algo/proto to cost 0.0 so NCCL selects it as best */
      table[algo][proto] = 0.0f;
    }
  }
}

ncclResult_t pluginInitImpl(void **context, uint64_t comm_id, size_t n_ranks,
                            size_t n_nodes, ncclDebugLogger_t log_function,
                            ncclNvlDomainInfo_v5_t *nvl_domain_info,
                            ncclTunerConstants_v5_t *constants) {
  (void)nvl_domain_info;
  (void)constants;

  auto *ctx = new (std::nothrow) TunerContext();
  if (!ctx)
    return ncclSystemError;

  acquire_bpftime_runtime();
  ctx->shared =
      acquire_shared_comm_state(comm_id, n_ranks, n_nodes, log_function, true);

  const char *policy_path = getenv("NCCL_POLICY_BPF_PATH");
  if (!ensure_policy_loaded(ctx->shared.get(), policy_path)) {
    release_shared_comm_state(ctx->shared, true);
    delete ctx;
    release_bpftime_runtime();
    return ncclInternalError;
  }

  log_plugin_message(
      ctx->shared->log_function, NCCL_TUNING, NCCL_LOG_INFO,
      "initialized for %zu ranks across %zu nodes using policy %s", n_ranks,
      n_nodes, load_active_policy(ctx->shared.get())->policy_source.c_str());
  *context = ctx;
  return ncclSuccess;
}

ncclResult_t pluginGetCollInfoImpl(void *context, ncclFunc_t coll_type,
                                   size_t n_bytes, int num_pipe_ops,
                                   float **coll_cost_table, int num_algo,
                                   int num_proto, int reg_buff,
                                   int *n_channels) {
  auto *ctx = reinterpret_cast<TunerContext *>(context);
  struct nccl_policy_ctx policy_ctx = {};
  struct nccl_policy_telemetry_value telemetry = {};
  uint64_t action = 0;
  uint64_t exec_latency_ns = 0;
  uint64_t call_count_snapshot = 0;
  uint32_t policy_coll_type = static_cast<uint32_t>(coll_type);
  int current_channels = 1;
  TunerContext::SyntheticTelemetryState synthetic = {};
  int err = 0;
  std::shared_ptr<SharedCommState::LoadedPolicyState> policy_state;

  if (!ctx || !ctx->shared || !n_channels)
    return ncclInternalError;
  drain_completed_ce_events(ctx->shared.get(), false);
  policy_state = load_active_policy(ctx->shared.get());
  if (!policy_state || !policy_state->prog)
    return ncclInternalError;
  (void)policy_coll_type_from_nccl_func(coll_type, &policy_coll_type);

  {
    std::lock_guard<std::mutex> lock(ctx->stats_mu);
    call_count_snapshot = ctx->call_count;
    current_channels = ctx->last_channels;
    synthetic = ctx->synthetic_telemetry;
  }

  *n_channels = current_channels;

  policy_ctx.n_bytes = n_bytes;
  if (synthetic.enabled) {
    policy_ctx.last_latency_ns = synthetic.last_latency_ns;
    policy_ctx.avg_latency_ns = synthetic.avg_latency_ns;
    policy_ctx.rolling_p99_ns = synthetic.rolling_p99_ns;
  } else if (lookup_telemetry_snapshot(ctx->shared.get(), policy_coll_type,
                                       &telemetry)) {
    policy_ctx.last_latency_ns = telemetry.last_latency_ns;
    policy_ctx.avg_latency_ns = telemetry.avg_latency_ns;
    policy_ctx.rolling_p99_ns = telemetry.p99_latency_ns;
  }
  policy_ctx.call_count = call_count_snapshot;
  policy_ctx.coll_type = policy_coll_type;
  policy_ctx.num_pipe_ops = static_cast<uint32_t>(num_pipe_ops);
  policy_ctx.reg_buff = static_cast<uint32_t>(reg_buff);
  policy_ctx.n_ranks = static_cast<uint32_t>(ctx->shared->n_ranks);
  policy_ctx.n_nodes = static_cast<uint32_t>(ctx->shared->n_nodes);
  policy_ctx.current_channels = static_cast<uint32_t>(current_channels);

  {
    const uint64_t start_ns = monotonic_time_ns();
    std::lock_guard<std::mutex> exec_lock(policy_state->exec_mu);
    err = policy_state->prog->bpftime_prog_exec(&policy_ctx, sizeof(policy_ctx),
                                                &action);
    exec_latency_ns = monotonic_time_ns() - start_ns;
  }

  if (err < 0) {
    log_plugin_message(ctx->shared->log_function, NCCL_TUNING, NCCL_LOG_WARN,
                       "bpftime execution failed for %s",
                       policy_state->policy_source.c_str());
    return ncclInternalError;
  }

  apply_policy_action(action, coll_cost_table, num_algo, num_proto, n_channels);
  {
    std::lock_guard<std::mutex> lock(ctx->stats_mu);
    ctx->last_channels = *n_channels;
    ctx->last_latency_ns = exec_latency_ns;
    ctx->rolling_p99_ns =
        update_p99_estimate(ctx->rolling_p99_ns, ctx->last_latency_ns);
    ctx->total_latency_ns += ctx->last_latency_ns;
    ctx->call_count++;
    call_count_snapshot = ctx->call_count;
  }

  if (call_count_snapshot <= 5 || call_count_snapshot % 100000 == 0) {
    fprintf(stderr,
            "[nccl-policy-plugin] call=%" PRIu64 " bytes=%zu action=%" PRIu64
            " latency_ns=%" PRIu64 " channels=%d aggr=%u\n",
            call_count_snapshot, n_bytes, action, exec_latency_ns, *n_channels,
            nccl_policy_action_aggressiveness(action));
  }

  return ncclSuccess;
}

ncclResult_t pluginFinalizeImpl(void *context) {
  auto *ctx = reinterpret_cast<TunerContext *>(context);
  uint64_t avg_latency = 0;
  uint64_t call_count = 0;
  uint64_t last_latency = 0;
  uint64_t p99_latency = 0;
  auto shared = ctx ? ctx->shared : nullptr;
  std::string policy_source = "none";

  if (!ctx)
    return ncclSuccess;

  {
    std::lock_guard<std::mutex> lock(ctx->stats_mu);
    call_count = ctx->call_count;
    avg_latency = call_count == 0 ? 0 : ctx->total_latency_ns / call_count;
    last_latency = ctx->last_latency_ns;
    p99_latency = ctx->rolling_p99_ns;
  }

  auto policy_state = shared ? load_active_policy(shared.get()) : nullptr;
  if (policy_state)
    policy_source = policy_state->policy_source;

  fprintf(stderr,
          "[nccl-policy-plugin] finalize calls=%" PRIu64
          " avg_latency_ns=%" PRIu64 " last_latency_ns=%" PRIu64
          " p99_estimate_ns=%" PRIu64 " source=%s\n",
          call_count, avg_latency, last_latency, p99_latency,
          policy_source.c_str());

  release_shared_comm_state(shared, true);
  policy_state.reset();
  ctx->shared.reset();
  delete ctx;
  shared.reset();
  release_bpftime_runtime();
  return ncclSuccess;
}

int pluginReloadPolicyImpl(void *context, const char *policy_path,
                           ReloadDebugStats *reload_stats) {
  auto *ctx = reinterpret_cast<TunerContext *>(context);
  auto shared = ctx ? ctx->shared : nullptr;
  const char *effective_path = policy_path;
  uint64_t load_start_ns = 0;
  uint64_t load_end_ns = 0;
  uint64_t swap_start_ns = 0;
  uint64_t swap_end_ns = 0;

  if (!ctx || !shared)
    return -1;
  if (!effective_path || effective_path[0] == '\0')
    effective_path = getenv("NCCL_POLICY_BPF_PATH");

  std::lock_guard<std::mutex> lock(shared->reload_mu);
  load_start_ns = monotonic_time_ns();
  auto next_policy = load_policy_state(shared.get(), effective_path);
  load_end_ns = monotonic_time_ns();
  if (!next_policy)
    return -1;

  swap_start_ns = monotonic_time_ns();
  auto previous_policy =
      exchange_active_policy(shared.get(), std::move(next_policy));
  swap_end_ns = monotonic_time_ns();

  if (reload_stats) {
    reload_stats->load_ns = load_end_ns - load_start_ns;
    reload_stats->swap_ns = swap_end_ns - swap_start_ns;
    reload_stats->total_ns = swap_end_ns - load_start_ns;
  }
  log_plugin_message(shared->log_function, NCCL_TUNING, NCCL_LOG_INFO,
                     "reloaded policy %s -> %s (load=%" PRIu64
                     "ns swap=%" PRIu64 "ns total=%" PRIu64 "ns)",
                     previous_policy ? previous_policy->policy_source.c_str()
                                     : "none",
                     load_active_policy(shared.get())->policy_source.c_str(),
                     load_end_ns - load_start_ns, swap_end_ns - swap_start_ns,
                     swap_end_ns - load_start_ns);
  return 0;
}

int pluginSetSyntheticTelemetryImpl(void *context,
                                    const SyntheticTelemetryConfig *config) {
  auto *ctx = reinterpret_cast<TunerContext *>(context);

  if (!ctx)
    return -1;

  std::lock_guard<std::mutex> lock(ctx->stats_mu);
  if (!config) {
    ctx->synthetic_telemetry = {};
    return 0;
  }

  ctx->synthetic_telemetry.enabled = config->enabled != 0;
  ctx->synthetic_telemetry.last_latency_ns = config->last_latency_ns;
  ctx->synthetic_telemetry.avg_latency_ns = config->avg_latency_ns;
  ctx->synthetic_telemetry.rolling_p99_ns = config->rolling_p99_ns;
  return 0;
}

ncclResult_t profilerInitImpl(void **context, uint64_t comm_id,
                              int *e_activation_mask, const char *comm_name,
                              int n_nodes, int nranks, int rank,
                              ncclDebugLogger_t log_function) {
  auto *ctx = new (std::nothrow) ProfilerContext();

  if (!ctx)
    return ncclSystemError;

  acquire_bpftime_runtime();
  ctx->shared = acquire_shared_comm_state(comm_id, static_cast<size_t>(nranks),
                                          static_cast<size_t>(n_nodes),
                                          log_function, false);
  ctx->comm_name = comm_name ? comm_name : "";
  ctx->rank = rank;
  ctx->activation_mask = ncclProfileColl | ncclProfileKernelCh |
                         ncclProfileCeColl;

  if (!ensure_policy_loaded(ctx->shared.get(), getenv("NCCL_POLICY_BPF_PATH"))) {
    release_shared_comm_state(ctx->shared, false);
    delete ctx;
    release_bpftime_runtime();
    return ncclInternalError;
  }

  if (e_activation_mask)
    *e_activation_mask = ctx->activation_mask;

  log_plugin_message(
      ctx->shared->log_function, NCCL_PROFILE, NCCL_LOG_INFO,
      "PROFILER/Plugin: init commName=%s commHash=%" PRIu64 " nranks=%d "
      "rank=%d mask=0x%x",
      ctx->comm_name.c_str(), comm_id, nranks, rank, ctx->activation_mask);
  *context = ctx;
  return ncclSuccess;
}

ncclResult_t profilerStartEventImpl(void *context, void **e_handle,
                                    ncclProfilerEventDescr_v6_t *e_descr) {
  auto *ctx = reinterpret_cast<ProfilerContext *>(context);
  SharedCommState::CollectiveEvent *coll_event = nullptr;
  SharedCommState::CeCollectiveEvent *ce_event = nullptr;
  KernelChannelEvent *kernel_event = nullptr;
  uint32_t coll_type = 0;
  size_t n_bytes = 0;

  if (e_handle)
    *e_handle = nullptr;
  if (!ctx || !ctx->shared || !e_handle || !e_descr)
    return ncclSuccess;

  drain_completed_ce_events(ctx->shared.get(), false);

  switch (e_descr->type) {
  case ncclProfileColl:
    if (!policy_coll_type_from_name(e_descr->coll.func, &coll_type))
      return ncclSuccess;
    n_bytes = e_descr->coll.count * datatype_size_from_name(e_descr->coll.datatype);
    if (n_bytes == 0)
      n_bytes = e_descr->coll.count;
    coll_event = new (std::nothrow) SharedCommState::CollectiveEvent();
    if (!coll_event)
      return ncclSuccess;
    coll_event->shared = ctx->shared.get();
    coll_event->coll_type = coll_type;
    coll_event->n_bytes = n_bytes;
    coll_event->rank = e_descr->rank;
    coll_event->start_ns = monotonic_time_ns();
    coll_event->expected_kernel_events = e_descr->coll.nChannels;
    {
      std::lock_guard<std::mutex> lock(ctx->shared->profiler_mu);
      ctx->shared->open_collectives.insert(coll_event);
    }
    *e_handle = coll_event;
    return ncclSuccess;
  case ncclProfileKernelCh:
    coll_event =
        reinterpret_cast<SharedCommState::CollectiveEvent *>(e_descr->parentObj);
    if (!coll_event || coll_event->type != ncclProfileColl ||
        coll_event->shared != ctx->shared.get()) {
      return ncclSuccess;
    }
    kernel_event = new (std::nothrow) KernelChannelEvent();
    if (!kernel_event)
      return ncclSuccess;
    kernel_event->parent = coll_event;
    kernel_event->channel_id = e_descr->kernelCh.channelId;
    kernel_event->start_ptimer_ns = e_descr->kernelCh.pTimer;
    {
      std::lock_guard<std::mutex> lock(ctx->shared->profiler_mu);
      coll_event->pending_kernel_events++;
      coll_event->saw_kernel_event = true;
    }
    *e_handle = kernel_event;
    return ncclSuccess;
  case ncclProfileCeColl:
    if (!policy_coll_type_from_name(e_descr->ceColl.func, &coll_type))
      return ncclSuccess;
    n_bytes =
        e_descr->ceColl.count * datatype_size_from_name(e_descr->ceColl.datatype);
    if (n_bytes == 0)
      n_bytes = e_descr->ceColl.count;
    ce_event = new (std::nothrow) SharedCommState::CeCollectiveEvent();
    if (!ce_event)
      return ncclSuccess;
    ce_event->shared = ctx->shared.get();
    ce_event->coll_type = coll_type;
    ce_event->n_bytes = n_bytes;
    ce_event->rank = e_descr->rank;
    ce_event->stream = reinterpret_cast<cudaStream_t>(e_descr->ceColl.stream);
    ce_event->start_ns = monotonic_time_ns();
    if (cudaEventCreate(&ce_event->start_event) != cudaSuccess ||
        cudaEventCreate(&ce_event->stop_event) != cudaSuccess ||
        cudaEventRecord(ce_event->start_event, ce_event->stream) !=
            cudaSuccess) {
      destroy_ce_event(ce_event);
      return ncclSuccess;
    }
    *e_handle = ce_event;
    return ncclSuccess;
  default:
    return ncclSuccess;
  }
}

ncclResult_t profilerStopEventImpl(void *e_handle) {
  auto type = e_handle ? *reinterpret_cast<uint64_t *>(e_handle) : 0;

  if (!e_handle)
    return ncclSuccess;

  if (type == ncclProfileColl) {
    auto *event = reinterpret_cast<SharedCommState::CollectiveEvent *>(e_handle);
    std::lock_guard<std::mutex> lock(event->shared->profiler_mu);
    event->host_stop_ns = monotonic_time_ns();
    event->host_stopped = true;
    maybe_finalize_collective(event, false);
    return ncclSuccess;
  }

  if (type == ncclProfileKernelCh) {
    auto *event = reinterpret_cast<KernelChannelEvent *>(e_handle);
    auto *parent = event->parent;
    if (parent && parent->shared) {
      std::lock_guard<std::mutex> lock(parent->shared->profiler_mu);
      parent->completed_kernel_events++;
      if (parent->pending_kernel_events > 0)
        parent->pending_kernel_events--;
      maybe_finalize_collective(parent, false);
    }
    delete event;
    return ncclSuccess;
  }

  if (type == ncclProfileCeColl) {
    auto *event = reinterpret_cast<SharedCommState::CeCollectiveEvent *>(e_handle);
    event->host_stop_ns = monotonic_time_ns();
    if (cudaEventRecord(event->stop_event, event->stream) == cudaSuccess) {
      event->stop_recorded = true;
      {
        std::lock_guard<std::mutex> lock(event->shared->profiler_mu);
        event->shared->pending_ce_events.insert(event);
      }
      drain_completed_ce_events(event->shared, false);
    } else {
      if (event->host_stop_ns > event->start_ns) {
        (void)record_real_telemetry(event->shared, event->coll_type,
                                    event->n_bytes,
                                    event->host_stop_ns - event->start_ns,
                                    event->rank, "ce-fallback");
      }
      destroy_ce_event(event);
    }
    return ncclSuccess;
  }

  return ncclSuccess;
}

ncclResult_t profilerRecordEventStateImpl(
    void *e_handle, ncclProfilerEventState_v6_t e_state,
    ncclProfilerEventStateArgs_v6_t *e_state_args) {
  auto type = e_handle ? *reinterpret_cast<uint64_t *>(e_handle) : 0;

  if (!e_handle || type != ncclProfileKernelCh ||
      e_state != ncclProfilerKernelChStop || !e_state_args)
    return ncclSuccess;

  auto *event = reinterpret_cast<KernelChannelEvent *>(e_handle);
  auto *parent = event->parent;

  event->stop_ptimer_ns = e_state_args->kernelCh.pTimer;
  if (parent && parent->shared &&
      event->stop_ptimer_ns > event->start_ptimer_ns) {
    std::lock_guard<std::mutex> lock(parent->shared->profiler_mu);
    parent->max_kernel_latency_ns =
        std::max(parent->max_kernel_latency_ns,
                 event->stop_ptimer_ns - event->start_ptimer_ns);
  }
  return ncclSuccess;
}

ncclResult_t profilerFinalizeImpl(void *context) {
  auto *ctx = reinterpret_cast<ProfilerContext *>(context);
  auto shared = ctx ? ctx->shared : nullptr;
  bool last_profiler_ref = false;

  if (!ctx)
    return ncclSuccess;

  log_plugin_message(shared ? shared->log_function : nullptr, NCCL_PROFILE,
                     NCCL_LOG_INFO, "PROFILER/Plugin: finalize commName=%s "
                                     "rank=%d",
                     ctx->comm_name.c_str(), ctx->rank);
  last_profiler_ref = release_shared_comm_state(shared, false);
  if (last_profiler_ref)
    finalize_remaining_profiler_events(shared.get());
  ctx->shared.reset();
  delete ctx;
  shared.reset();
  release_bpftime_runtime();
  return ncclSuccess;
}

} // namespace

extern "C" {

static ncclResult_t pluginInit(void **context, uint64_t comm_id, size_t n_ranks,
                               size_t n_nodes, ncclDebugLogger_t log_function,
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

static ncclResult_t profilerInit(void **context, uint64_t comm_id,
                                 int *e_activation_mask,
                                 const char *comm_name, int n_nodes,
                                 int nranks, int rank,
                                 ncclDebugLogger_t log_function) {
  return profilerInitImpl(context, comm_id, e_activation_mask, comm_name,
                          n_nodes, nranks, rank, log_function);
}

static ncclResult_t profilerStartEvent(void *context, void **e_handle,
                                       ncclProfilerEventDescr_v6_t *e_descr) {
  return profilerStartEventImpl(context, e_handle, e_descr);
}

static ncclResult_t profilerStopEvent(void *e_handle) {
  return profilerStopEventImpl(e_handle);
}

static ncclResult_t profilerRecordEventState(
    void *e_handle, ncclProfilerEventState_v6_t e_state,
    ncclProfilerEventStateArgs_v6_t *e_state_args) {
  return profilerRecordEventStateImpl(e_handle, e_state, e_state_args);
}

static ncclResult_t profilerFinalize(void *context) {
  return profilerFinalizeImpl(context);
}

extern const ncclTuner_v5_t ncclTunerPlugin_v5
    __attribute__((visibility("default"))) = {
        .name = "eBPFPolicy",
        .init = pluginInit,
        .getCollInfo = pluginGetCollInfo,
        .finalize = pluginFinalize,
};

extern const ncclProfiler_v6_t ncclProfiler_v6
    __attribute__((visibility("default"))) = {
        .name = "eBPFPolicyProfiler",
        .init = profilerInit,
        .startEvent = profilerStartEvent,
        .stopEvent = profilerStopEvent,
        .recordEventState = profilerRecordEventState,
        .finalize = profilerFinalize,
};

extern int ncclPolicyPluginDebugGetMapFd(void *context, const char *map_name)
    __attribute__((visibility("default")));
extern int ncclPolicyPluginDebugReloadPolicy(void *context,
                                             const char *policy_path,
                                             ReloadDebugStats *reload_stats)
    __attribute__((visibility("default")));
extern int ncclPolicyPluginDebugSetSyntheticTelemetry(
    void *context, const SyntheticTelemetryConfig *config)
    __attribute__((visibility("default")));

int ncclPolicyPluginDebugGetMapFd(void *context, const char *map_name) {
  auto *ctx = reinterpret_cast<TunerContext *>(context);
  auto policy_state =
      (ctx && ctx->shared) ? load_active_policy(ctx->shared.get()) : nullptr;
  std::unordered_map<std::string, int>::const_iterator map_it;

  if (!ctx || !policy_state || !map_name)
    return -1;
  map_it = policy_state->map_fds.find(map_name);
  if (map_it == policy_state->map_fds.end())
    return -1;
  return map_it->second;
}

int ncclPolicyPluginDebugReloadPolicy(void *context, const char *policy_path,
                                      ReloadDebugStats *reload_stats) {
  return pluginReloadPolicyImpl(context, policy_path, reload_stats);
}

int ncclPolicyPluginDebugSetSyntheticTelemetry(
    void *context, const SyntheticTelemetryConfig *config) {
  return pluginSetSyntheticTelemetryImpl(context, config);
}
}
