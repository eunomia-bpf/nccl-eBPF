#include <bpf/libbpf.h>
#include <dlfcn.h>
#include <fcntl.h>
#include <gelf.h>
#include <inttypes.h>
#include <linux/bpf.h>
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#include <algorithm>
#include <cerrno>
#include <fstream>
#include <map>
#include <memory>
#include <mutex>
#include <new>
#include <string>
#include <unordered_map>
#include <vector>

#include "bpftime-verifier.hpp"
#include "bpftime_config.hpp"
#include "bpftime_helper_group.hpp"
#include "bpftime_prog.hpp"
#include "bpftime_shm.hpp"
#include "nccl_net.h"

#include "net_ebpf_ctx.h"
#include "plugin_paths.h"

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

struct ProgramSpec {
  std::string name;
  std::string section_name;
  std::vector<ebpf_inst> insns;
};

struct PolicyState {
  ~PolicyState() {
    for (const auto &entry : map_fds) {
      if (entry.second >= 0)
        bpftime_close(entry.second);
    }
  }

  std::unique_ptr<bpftime::bpftime_prog> prog;
  std::string policy_source = "hardcoded-noop";
  std::string section_name = "uprobe";
  std::unordered_map<std::string, int> map_fds;
  std::map<int, bpftime::verifier::BpftimeMapDescriptor> verifier_maps;
  mutable std::mutex exec_mu;
};

struct PluginCtx {
  void *base_ctx = nullptr;
  ncclDebugLogger_t log_function = nullptr;
  uint64_t comm_id = 0;
  std::shared_ptr<PolicyState> policy;
};

enum class WrappedCommKind {
  kListen,
  kSend,
  kRecv,
};

struct WrappedComm {
  WrappedCommKind kind = WrappedCommKind::kListen;
  PluginCtx *ctx = nullptr;
  void *base_comm = nullptr;
  int dev = -1;
};

std::mutex g_runtime_mu;
size_t g_runtime_users = 0;
std::mutex g_backend_mu;
ncclNet_t *g_backend_socket = nullptr;

const char *hook_name(uint32_t hook) {
  switch (hook) {
  case NCCL_NET_EBPF_HOOK_INIT:
    return "init";
  case NCCL_NET_EBPF_HOOK_LISTEN:
    return "listen";
  case NCCL_NET_EBPF_HOOK_CONNECT:
    return "connect";
  case NCCL_NET_EBPF_HOOK_ACCEPT:
    return "accept";
  case NCCL_NET_EBPF_HOOK_ISEND:
    return "isend";
  case NCCL_NET_EBPF_HOOK_IRECV:
    return "irecv";
  case NCCL_NET_EBPF_HOOK_FINALIZE:
    return "finalize";
  default:
    return "unknown";
  }
}

void ensure_bpftime_shm_name() {
  const char *existing = getenv("BPFTIME_GLOBAL_SHM_NAME");
  char buffer[128];

  if (existing && existing[0] != '\0')
    return;

  snprintf(buffer, sizeof(buffer), "nccl_net_ebpf_bpftime_%u_%d",
           static_cast<unsigned>(geteuid()), static_cast<int>(getpid()));
  setenv("BPFTIME_GLOBAL_SHM_NAME", buffer, 0);
}

uint64_t monotonic_time_ns() {
  struct timespec ts;
  clock_gettime(CLOCK_MONOTONIC_RAW, &ts);
  return static_cast<uint64_t>(ts.tv_sec) * 1000000000ull +
         static_cast<uint64_t>(ts.tv_nsec);
}

void log_message(ncclDebugLogger_t log_function, unsigned long flags,
                 ncclDebugLogLevel level, const char *fmt, ...) {
  char buffer[4096];
  va_list args;
  va_start(args, fmt);
  vsnprintf(buffer, sizeof(buffer), fmt, args);
  va_end(args);

  fprintf(stderr, "[nccl-net-ebpf] %s\n", buffer);
  if (log_function)
    log_function(level, flags, __FILE__, __LINE__, "%s", buffer);
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
  const char *mode = getenv("NCCL_NET_EBPF_VERIFY_MODE");
  if (!mode || mode[0] == '\0')
    return VerifyMode::kStrict;

  if (strcmp(mode, "none") == 0)
    return VerifyMode::kNone;
  if (strcmp(mode, "warning") == 0 || strcmp(mode, "warn") == 0)
    return VerifyMode::kWarning;
  return VerifyMode::kStrict;
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

bool create_bpftime_maps(PolicyState *policy_state, struct bpf_object *obj) {
  struct bpf_map *map = nullptr;

  bpf_object__for_each_map(map, obj) {
    bpftime::bpf_map_attr attr = {};
    const char *logical_name = bpf_map__name(map);
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

    fd = bpftime_maps_create(-1, logical_name ? logical_name : "unnamed_map",
                             attr);
    if (fd < 0)
      return false;

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

bool extract_program_spec(struct bpf_object *obj, ProgramSpec *spec) {
  struct bpf_program *prog = bpf_object__next_program(obj, nullptr);
  const struct bpf_insn *insns = nullptr;
  size_t insn_cnt = 0;

  if (!prog)
    return false;

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

struct MapSymbol {
  std::string name;
  size_t section_index = 0;
  uint64_t value = 0;
};

bool relocate_program_maps(const char *path, PolicyState *policy_state,
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

  if (elf_version(EV_CURRENT) == EV_NONE)
    return false;

  fd = open(path, O_RDONLY);
  if (fd < 0)
    return false;

  elf = elf_begin(fd, ELF_C_READ, nullptr);
  if (!elf) {
    close(fd);
    return false;
  }

  if (elf_getshdrstrndx(elf, &shstrndx) != 0) {
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
    elf_end(elf);
    close(fd);
    return false;
  }

  {
    Elf_Data *data = elf_getdata(symtab_scn, nullptr);
    size_t symbol_count = 0;

    if (!data || symtab_shdr.sh_entsize == 0) {
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
    if (!data || shdr.sh_entsize == 0)
      continue;

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

bool verify_program(ncclDebugLogger_t log_function,
                    const PolicyState *policy_state,
                    const ProgramSpec &spec) {
  std::vector<int32_t> helper_ids;
  const VerifyMode mode = get_verify_mode();
  const std::string verify_section = verifier_section_name(spec.section_name);

  if (mode == VerifyMode::kNone)
    return true;
  if (!collect_map_helpers(&helper_ids))
    return mode != VerifyMode::kStrict;

  bpftime::verifier::set_map_descriptors(policy_state->verifier_maps);
  bpftime::verifier::set_available_helpers(helper_ids);
  bpftime::verifier::set_non_kernel_helpers({});

  auto result = bpftime::verifier::verify_ebpf_program(
      reinterpret_cast<const uint64_t *>(spec.insns.data()), spec.insns.size(),
      verify_section);
  if (!result)
    return true;

  if (mode == VerifyMode::kStrict) {
    log_message(log_function, NCCL_NET, NCCL_LOG_WARN,
                "verifier rejected %s: %s", policy_state->policy_source.c_str(),
                result->c_str());
    return false;
  }

  log_message(log_function, NCCL_NET, NCCL_LOG_WARN,
              "verifier warning for %s: %s",
              policy_state->policy_source.c_str(), result->c_str());
  return true;
}

bool load_hardcoded_program(ncclDebugLogger_t log_function,
                            PolicyState *policy_state) {
  auto config = bpftime::construct_agent_config_from_env();
  config.set_vm_name("llvm");
  policy_state->prog = std::make_unique<bpftime::bpftime_prog>(
      kHardcodedNoopProgram, std::size(kHardcodedNoopProgram), "hardcoded-noop",
      std::move(config));
  if (!policy_state->prog || !register_helpers(policy_state->prog.get()))
    return false;
  if (policy_state->prog->bpftime_prog_load(true) < 0) {
    log_message(log_function, NCCL_NET, NCCL_LOG_WARN,
                "failed to load fallback noop eBPF program");
    return false;
  }
  policy_state->policy_source = "hardcoded-noop";
  policy_state->section_name = "uprobe";
  return true;
}

bool load_program_from_object(ncclDebugLogger_t log_function,
                              PolicyState *policy_state, const char *path) {
  std::unique_ptr<struct bpf_object, decltype(&bpf_object__close)> obj(
      bpf_object__open_file(path, nullptr), &bpf_object__close);
  ProgramSpec spec;
  auto config = bpftime::construct_agent_config_from_env();

  if (!obj)
    return false;

  policy_state->policy_source = path;
  if (!extract_program_spec(obj.get(), &spec))
    return false;
  if (!create_bpftime_maps(policy_state, obj.get()))
    return false;
  if (!relocate_program_maps(path, policy_state, &spec))
    return false;
  if (!verify_program(log_function, policy_state, spec))
    return false;

  config.set_vm_name("llvm");
  policy_state->prog = std::make_unique<bpftime::bpftime_prog>(
      spec.insns.data(), spec.insns.size(), spec.name.c_str(),
      std::move(config));
  if (!policy_state->prog || !register_helpers(policy_state->prog.get()))
    return false;
  if (policy_state->prog->bpftime_prog_load(true) < 0)
    return false;

  policy_state->section_name = spec.section_name;
  return true;
}

std::shared_ptr<PolicyState> load_policy(ncclDebugLogger_t log_function) {
  auto policy_state = std::make_shared<PolicyState>();
  const char *env_path = getenv("NCCL_NET_EBPF_BPF_PATH");
  const char *path = (env_path && env_path[0] != '\0') ? env_path
                                                       : NCCL_NET_EBPF_DEFAULT_BPF_PATH;

  if (path && path[0] != '\0' && access(path, R_OK) == 0 &&
      load_program_from_object(log_function, policy_state.get(), path)) {
    log_message(log_function, NCCL_NET, NCCL_LOG_INFO,
                "loaded net eBPF object from %s", path);
    return policy_state;
  }

  log_message(log_function, NCCL_NET, NCCL_LOG_WARN,
              "falling back to noop eBPF program (requested path=%s)",
              path ? path : "(null)");
  if (!load_hardcoded_program(log_function, policy_state.get()))
    return nullptr;
  return policy_state;
}

// Find the real path to libnccl.so by scanning /proc/self/maps.
// This works regardless of how libnccl was loaded or what its soname is.
std::string find_libnccl_path_from_maps() {
  std::ifstream maps("/proc/self/maps");
  std::string line;

  while (std::getline(maps, line)) {
    // Each line looks like: addr-addr perms offset dev inode  pathname
    // We look for a mapping whose pathname contains "libnccl"
    auto slash_pos = line.rfind('/');
    if (slash_pos == std::string::npos)
      continue;
    auto path = line.substr(slash_pos);
    // Trim trailing whitespace
    auto end = path.find_last_not_of(" \t\n\r");
    if (end != std::string::npos)
      path.resize(end + 1);
    // Match libnccl.so but not libnccl-net-ebpf (ourselves)
    auto basename_start = path.rfind('/');
    std::string basename = (basename_start != std::string::npos)
                               ? path.substr(basename_start + 1)
                               : path;
    if (basename.find("libnccl.so") == 0 ||
        basename.find("libnccl_") == 0) {
      // Extract the full absolute path from the original line
      auto space_before_path = line.rfind("  ");
      if (space_before_path == std::string::npos)
        space_before_path = line.rfind(' ');
      if (space_before_path != std::string::npos) {
        auto full_path = line.substr(space_before_path + 1);
        // Trim leading/trailing whitespace
        auto start = full_path.find_first_not_of(" \t");
        auto fend = full_path.find_last_not_of(" \t\n\r");
        if (start != std::string::npos && fend != std::string::npos) {
          full_path = full_path.substr(start, fend - start + 1);
          // Verify it's an absolute path and not our own plugin
          if (full_path[0] == '/' &&
              full_path.find("nccl-net-ebpf") == std::string::npos) {
            return full_path;
          }
        }
      }
    }
  }
  return {};
}

ncclNet_t *resolve_socket_backend(ncclDebugLogger_t log_function) {
  std::lock_guard<std::mutex> lock(g_backend_mu);
  if (g_backend_socket)
    return g_backend_socket;

  // Strategy 1: RTLD_DEFAULT finds globally-visible symbols.
  // Works when libnccl.so was loaded with RTLD_GLOBAL (the common case).
  g_backend_socket =
      reinterpret_cast<ncclNet_t *>(dlsym(RTLD_DEFAULT, "ncclNetSocket"));
  if (g_backend_socket) {
    log_message(log_function, NCCL_NET, NCCL_LOG_INFO,
                "resolved ncclNetSocket via RTLD_DEFAULT");
    return g_backend_socket;
  }

  // Strategy 2: Get a handle to the already-loaded libnccl by soname.
  // Works when libnccl was loaded with RTLD_LOCAL but uses standard soname.
  {
    const char *sonames[] = {"libnccl.so.2", "libnccl.so", nullptr};
    for (const char **name = sonames; *name; ++name) {
      void *handle = dlopen(*name, RTLD_NOW | RTLD_NOLOAD);
      if (!handle)
        continue;
      g_backend_socket =
          reinterpret_cast<ncclNet_t *>(dlsym(handle, "ncclNetSocket"));
      if (g_backend_socket) {
        log_message(log_function, NCCL_NET, NCCL_LOG_INFO,
                    "resolved ncclNetSocket via dlopen(%s, RTLD_NOLOAD)",
                    *name);
        return g_backend_socket;
      }
    }
  }

  // Strategy 3: Scan /proc/self/maps for the actual libnccl path, then
  // dlopen that exact path.  Handles non-standard install locations and
  // LD_LIBRARY_PATH-based loading where the soname lookup above might miss.
  {
    std::string path = find_libnccl_path_from_maps();
    if (!path.empty()) {
      void *handle = dlopen(path.c_str(), RTLD_NOW | RTLD_NOLOAD);
      if (handle) {
        g_backend_socket =
            reinterpret_cast<ncclNet_t *>(dlsym(handle, "ncclNetSocket"));
        if (g_backend_socket) {
          log_message(log_function, NCCL_NET, NCCL_LOG_INFO,
                      "resolved ncclNetSocket via /proc/self/maps (%s)",
                      path.c_str());
          return g_backend_socket;
        }
      }
    }
  }

  log_message(log_function, NCCL_NET, NCCL_LOG_WARN,
              "unable to resolve ncclNetSocket -- "
              "ensure libnccl.so exports this symbol "
              "(NCCL 2.x default builds with nccl* version-script do)");
  return g_backend_socket;
}

void exec_policy_hook(PluginCtx *ctx, uint32_t hook, int dev, size_t size,
                      int tag) {
  struct nccl_net_ebpf_ctx event = {};
  uint64_t return_value = 0;

  if (!ctx || !ctx->policy || !ctx->policy->prog)
    return;

  event.hook = hook;
  event.dev = dev;
  event.tag = tag;
  event.size = size;
  event.comm_id = ctx->comm_id;
  event.timestamp_ns = monotonic_time_ns();

  std::lock_guard<std::mutex> lock(ctx->policy->exec_mu);
  if (ctx->policy->prog->bpftime_prog_exec(&event, sizeof(event),
                                           &return_value) < 0) {
    log_message(ctx->log_function, NCCL_NET, NCCL_LOG_WARN,
                "bpftime execution failed for hook=%s comm=%" PRIu64,
                hook_name(hook), ctx->comm_id);
  }
}

void dump_policy_stats(PluginCtx *ctx) {
  auto map_it = ctx && ctx->policy
                    ? ctx->policy->map_fds.find(NCCL_NET_EBPF_STATS_MAP_NAME)
                    : std::unordered_map<std::string, int>::const_iterator();
  int map_fd = -1;

  if (!ctx || !ctx->policy)
    return;
  if (map_it == ctx->policy->map_fds.end())
    return;

  map_fd = map_it->second;
  for (uint32_t hook = 0; hook < NCCL_NET_EBPF_HOOK_COUNT; ++hook) {
    const auto *stat = reinterpret_cast<const nccl_net_ebpf_stat *>(
        bpftime_map_lookup_elem(map_fd, &hook));

    if (!stat || stat->calls == 0)
      continue;

    log_message(ctx->log_function, NCCL_NET, NCCL_LOG_INFO,
                "stats comm=%" PRIu64 " hook=%s calls=%" PRIu64
                " bytes=%" PRIu64 " lastTag=%" PRId64,
                ctx->comm_id, hook_name(hook), stat->calls, stat->bytes,
                stat->last_tag);
  }
}

void *unwrap_comm(void *comm) {
  auto *wrapped = reinterpret_cast<WrappedComm *>(comm);
  return wrapped ? wrapped->base_comm : nullptr;
}

PluginCtx *unwrap_ctx(void *comm) {
  auto *wrapped = reinterpret_cast<WrappedComm *>(comm);
  return wrapped ? wrapped->ctx : nullptr;
}

int unwrap_dev(void *comm) {
  auto *wrapped = reinterpret_cast<WrappedComm *>(comm);
  return wrapped ? wrapped->dev : -1;
}

WrappedComm *wrap_comm(PluginCtx *ctx, WrappedCommKind kind, void *base_comm,
                       int dev) {
  auto *wrapped = new (std::nothrow) WrappedComm();
  if (!wrapped)
    return nullptr;
  wrapped->kind = kind;
  wrapped->ctx = ctx;
  wrapped->base_comm = base_comm;
  wrapped->dev = dev;
  return wrapped;
}

size_t recv_total_bytes(int n, const size_t *sizes) {
  size_t total = 0;
  if (!sizes)
    return 0;
  for (int i = 0; i < n; ++i)
    total += sizes[i];
  return total;
}

int recv_tag(int n, const int *tags) {
  if (!tags || n <= 0)
    return -1;
  return tags[0];
}

ncclResult_t pluginInitImpl(void **context, uint64_t comm_id,
                            ncclNetCommConfig_t *config,
                            ncclDebugLogger_t log_function,
                            ncclProfilerCallback_t prof_function) {
  ncclNet_t *base = resolve_socket_backend(log_function);
  auto *ctx = new (std::nothrow) PluginCtx();
  ncclResult_t ret = ncclSuccess;

  if (context)
    *context = nullptr;
  if (!base || !ctx)
    return ncclInternalError;

  ctx->comm_id = comm_id;
  ctx->log_function = log_function;

  ret = base->init(&ctx->base_ctx, comm_id, config, log_function, prof_function);
  if (ret != ncclSuccess) {
    delete ctx;
    return ret;
  }

  acquire_bpftime_runtime();
  ctx->policy = load_policy(log_function);
  if (!ctx->policy) {
    (void)base->finalize(ctx->base_ctx);
    delete ctx;
    release_bpftime_runtime();
    return ncclInternalError;
  }

  exec_policy_hook(ctx, NCCL_NET_EBPF_HOOK_INIT, -1, 0, -1);
  log_message(log_function, NCCL_NET, NCCL_LOG_INFO,
              "initialized wrapper comm=%" PRIu64 " backend=%s policy=%s",
              comm_id, base->name, ctx->policy->policy_source.c_str());
  *context = ctx;
  return ncclSuccess;
}

ncclResult_t pluginDevicesImpl(int *ndev) {
  ncclNet_t *base = resolve_socket_backend(nullptr);
  return base ? base->devices(ndev) : ncclInternalError;
}

ncclResult_t pluginGetPropertiesImpl(int dev, ncclNetProperties_t *props) {
  ncclNet_t *base = resolve_socket_backend(nullptr);
  return base ? base->getProperties(dev, props) : ncclInternalError;
}

ncclResult_t pluginListenImpl(void *context, int dev, void *handle,
                              void **listen_comm) {
  auto *ctx = reinterpret_cast<PluginCtx *>(context);
  ncclNet_t *base = resolve_socket_backend(ctx ? ctx->log_function : nullptr);
  void *base_listen = nullptr;
  ncclResult_t ret;

  if (listen_comm)
    *listen_comm = nullptr;
  if (!ctx || !base)
    return ncclInternalError;

  ret = base->listen(ctx->base_ctx, dev, handle, &base_listen);
  if (ret != ncclSuccess || !base_listen)
    return ret;

  auto *wrapped = wrap_comm(ctx, WrappedCommKind::kListen, base_listen, dev);
  if (!wrapped) {
    (void)base->closeListen(base_listen);
    return ncclSystemError;
  }

  exec_policy_hook(ctx, NCCL_NET_EBPF_HOOK_LISTEN, dev, 0, -1);
  *listen_comm = wrapped;
  return ncclSuccess;
}

ncclResult_t pluginConnectImpl(void *context, int dev, void *handle,
                               void **send_comm,
                               ncclNetDeviceHandle_t **send_dev_comm) {
  auto *ctx = reinterpret_cast<PluginCtx *>(context);
  ncclNet_t *base = resolve_socket_backend(ctx ? ctx->log_function : nullptr);
  void *base_send = nullptr;
  ncclResult_t ret;

  if (send_comm)
    *send_comm = nullptr;
  if (!ctx || !base)
    return ncclInternalError;

  ret = base->connect(ctx->base_ctx, dev, handle, &base_send, send_dev_comm);
  if (ret != ncclSuccess || !base_send)
    return ret;

  auto *wrapped = wrap_comm(ctx, WrappedCommKind::kSend, base_send, dev);
  if (!wrapped) {
    (void)base->closeSend(base_send);
    return ncclSystemError;
  }

  exec_policy_hook(ctx, NCCL_NET_EBPF_HOOK_CONNECT, dev, 0, -1);
  *send_comm = wrapped;
  return ncclSuccess;
}

ncclResult_t pluginAcceptImpl(void *listen_comm, void **recv_comm,
                              ncclNetDeviceHandle_t **recv_dev_comm) {
  auto *listen = reinterpret_cast<WrappedComm *>(listen_comm);
  ncclNet_t *base =
      resolve_socket_backend(listen && listen->ctx ? listen->ctx->log_function
                                                   : nullptr);
  void *base_recv = nullptr;
  ncclResult_t ret;

  if (recv_comm)
    *recv_comm = nullptr;
  if (!listen || !listen->ctx || !base)
    return ncclInternalError;

  ret = base->accept(listen->base_comm, &base_recv, recv_dev_comm);
  if (ret != ncclSuccess || !base_recv)
    return ret;

  auto *wrapped =
      wrap_comm(listen->ctx, WrappedCommKind::kRecv, base_recv, listen->dev);
  if (!wrapped) {
    (void)base->closeRecv(base_recv);
    return ncclSystemError;
  }

  exec_policy_hook(listen->ctx, NCCL_NET_EBPF_HOOK_ACCEPT, listen->dev, 0, -1);
  *recv_comm = wrapped;
  return ncclSuccess;
}

ncclResult_t pluginRegMrImpl(void *comm, void *data, size_t size, int type,
                             void **mhandle) {
  ncclNet_t *base = resolve_socket_backend(unwrap_ctx(comm) ? unwrap_ctx(comm)->log_function
                                                            : nullptr);
  return base ? base->regMr(unwrap_comm(comm), data, size, type, mhandle)
              : ncclInternalError;
}

ncclResult_t pluginDeregMrImpl(void *comm, void *mhandle) {
  ncclNet_t *base = resolve_socket_backend(unwrap_ctx(comm) ? unwrap_ctx(comm)->log_function
                                                            : nullptr);
  return base ? base->deregMr(unwrap_comm(comm), mhandle) : ncclInternalError;
}

ncclResult_t pluginIsendImpl(void *send_comm, void *data, size_t size, int tag,
                             void *mhandle, void *phandle, void **request) {
  auto *ctx = unwrap_ctx(send_comm);
  ncclNet_t *base =
      resolve_socket_backend(ctx ? ctx->log_function : nullptr);
  ncclResult_t ret;

  if (request)
    *request = nullptr;
  if (!ctx || !base)
    return ncclInternalError;

  ret = base->isend(unwrap_comm(send_comm), data, size, tag, mhandle, phandle,
                    request);
  if (ret == ncclSuccess && request && *request)
    exec_policy_hook(ctx, NCCL_NET_EBPF_HOOK_ISEND, unwrap_dev(send_comm), size,
                     tag);
  return ret;
}

ncclResult_t pluginIrecvImpl(void *recv_comm, int n, void **data,
                             size_t *sizes, int *tags, void **mhandles,
                             void **phandles, void **request) {
  auto *ctx = unwrap_ctx(recv_comm);
  ncclNet_t *base =
      resolve_socket_backend(ctx ? ctx->log_function : nullptr);
  ncclResult_t ret;

  if (request)
    *request = nullptr;
  if (!ctx || !base)
    return ncclInternalError;

  ret = base->irecv(unwrap_comm(recv_comm), n, data, sizes, tags, mhandles,
                    phandles, request);
  if (ret == ncclSuccess && request && *request) {
    exec_policy_hook(ctx, NCCL_NET_EBPF_HOOK_IRECV, unwrap_dev(recv_comm),
                     recv_total_bytes(n, sizes), recv_tag(n, tags));
  }
  return ret;
}

ncclResult_t pluginIflushImpl(void *recv_comm, int n, void **data, int *sizes,
                              void **mhandles, void **request) {
  auto *ctx = unwrap_ctx(recv_comm);
  ncclNet_t *base =
      resolve_socket_backend(ctx ? ctx->log_function : nullptr);
  return base ? base->iflush(unwrap_comm(recv_comm), n, data, sizes, mhandles,
                             request)
              : ncclInternalError;
}

ncclResult_t pluginTestImpl(void *request, int *done, int *size) {
  ncclNet_t *base = resolve_socket_backend(nullptr);
  return base ? base->test(request, done, size) : ncclInternalError;
}

ncclResult_t pluginCloseSendImpl(void *send_comm) {
  auto *wrapped = reinterpret_cast<WrappedComm *>(send_comm);
  auto *ctx = wrapped ? wrapped->ctx : nullptr;
  ncclNet_t *base =
      resolve_socket_backend(ctx ? ctx->log_function : nullptr);
  ncclResult_t ret =
      base ? base->closeSend(wrapped ? wrapped->base_comm : nullptr)
           : ncclInternalError;
  delete wrapped;
  return ret;
}

ncclResult_t pluginCloseRecvImpl(void *recv_comm) {
  auto *wrapped = reinterpret_cast<WrappedComm *>(recv_comm);
  auto *ctx = wrapped ? wrapped->ctx : nullptr;
  ncclNet_t *base =
      resolve_socket_backend(ctx ? ctx->log_function : nullptr);
  ncclResult_t ret =
      base ? base->closeRecv(wrapped ? wrapped->base_comm : nullptr)
           : ncclInternalError;
  delete wrapped;
  return ret;
}

ncclResult_t pluginCloseListenImpl(void *listen_comm) {
  auto *wrapped = reinterpret_cast<WrappedComm *>(listen_comm);
  auto *ctx = wrapped ? wrapped->ctx : nullptr;
  ncclNet_t *base =
      resolve_socket_backend(ctx ? ctx->log_function : nullptr);
  ncclResult_t ret =
      base ? base->closeListen(wrapped ? wrapped->base_comm : nullptr)
           : ncclInternalError;
  delete wrapped;
  return ret;
}

ncclResult_t pluginFinalizeImpl(void *context) {
  auto *ctx = reinterpret_cast<PluginCtx *>(context);
  ncclNet_t *base =
      resolve_socket_backend(ctx ? ctx->log_function : nullptr);
  ncclResult_t ret = ncclSuccess;

  if (!ctx)
    return ncclSuccess;

  exec_policy_hook(ctx, NCCL_NET_EBPF_HOOK_FINALIZE, -1, 0, -1);
  dump_policy_stats(ctx);
  if (base)
    ret = base->finalize(ctx->base_ctx);
  ctx->policy.reset();
  delete ctx;
  release_bpftime_runtime();
  return ret;
}

} // namespace

extern "C" {

static ncclResult_t pluginInit(void **context, uint64_t comm_id,
                               ncclNetCommConfig_t *config,
                               ncclDebugLogger_t log_function,
                               ncclProfilerCallback_t prof_function) {
  return pluginInitImpl(context, comm_id, config, log_function, prof_function);
}

static ncclResult_t pluginDevices(int *ndev) {
  return pluginDevicesImpl(ndev);
}

static ncclResult_t pluginGetProperties(int dev, ncclNetProperties_t *props) {
  return pluginGetPropertiesImpl(dev, props);
}

static ncclResult_t pluginListen(void *context, int dev, void *handle,
                                 void **listen_comm) {
  return pluginListenImpl(context, dev, handle, listen_comm);
}

static ncclResult_t pluginConnect(void *context, int dev, void *handle,
                                  void **send_comm,
                                  ncclNetDeviceHandle_t **send_dev_comm) {
  return pluginConnectImpl(context, dev, handle, send_comm, send_dev_comm);
}

static ncclResult_t pluginAccept(void *listen_comm, void **recv_comm,
                                 ncclNetDeviceHandle_t **recv_dev_comm) {
  return pluginAcceptImpl(listen_comm, recv_comm, recv_dev_comm);
}

static ncclResult_t pluginRegMr(void *comm, void *data, size_t size, int type,
                                void **mhandle) {
  return pluginRegMrImpl(comm, data, size, type, mhandle);
}

static ncclResult_t pluginDeregMr(void *comm, void *mhandle) {
  return pluginDeregMrImpl(comm, mhandle);
}

static ncclResult_t pluginIsend(void *send_comm, void *data, size_t size,
                                int tag, void *mhandle, void *phandle,
                                void **request) {
  return pluginIsendImpl(send_comm, data, size, tag, mhandle, phandle, request);
}

static ncclResult_t pluginIrecv(void *recv_comm, int n, void **data,
                                size_t *sizes, int *tags, void **mhandles,
                                void **phandles, void **request) {
  return pluginIrecvImpl(recv_comm, n, data, sizes, tags, mhandles, phandles,
                         request);
}

static ncclResult_t pluginIflush(void *recv_comm, int n, void **data,
                                 int *sizes, void **mhandles,
                                 void **request) {
  return pluginIflushImpl(recv_comm, n, data, sizes, mhandles, request);
}

static ncclResult_t pluginTest(void *request, int *done, int *size) {
  return pluginTestImpl(request, done, size);
}

static ncclResult_t pluginCloseSend(void *send_comm) {
  return pluginCloseSendImpl(send_comm);
}

static ncclResult_t pluginCloseRecv(void *recv_comm) {
  return pluginCloseRecvImpl(recv_comm);
}

static ncclResult_t pluginCloseListen(void *listen_comm) {
  return pluginCloseListenImpl(listen_comm);
}

static ncclResult_t pluginFinalize(void *context) {
  return pluginFinalizeImpl(context);
}

extern const ncclNet_v11_t ncclNetPlugin_v11
    __attribute__((visibility("default"))) = {
        .name = "Socket",
        .init = pluginInit,
        .devices = pluginDevices,
        .getProperties = pluginGetProperties,
        .listen = pluginListen,
        .connect = pluginConnect,
        .accept = pluginAccept,
        .regMr = pluginRegMr,
        .regMrDmaBuf = nullptr,
        .deregMr = pluginDeregMr,
        .isend = pluginIsend,
        .irecv = pluginIrecv,
        .iflush = pluginIflush,
        .test = pluginTest,
        .closeSend = pluginCloseSend,
        .closeRecv = pluginCloseRecv,
        .closeListen = pluginCloseListen,
        .getDeviceMr = nullptr,
        .irecvConsumed = nullptr,
        .makeVDevice = nullptr,
        .finalize = pluginFinalize,
        .setNetAttr = nullptr,
};

} // extern "C"
