#include <bpf/libbpf.h>
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
#include <memory>
#include <mutex>
#include <new>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

#include "bpftime-verifier.hpp"
#include "bpftime_config.hpp"
#include "bpftime_helper_group.hpp"
#include "bpftime_prog.hpp"
#include "bpftime_shm.hpp"
#include "nccl_tuner.h"

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

struct PluginContext {
  std::unique_ptr<bpftime::bpftime_prog> prog;
  ncclDebugLogger_t log_function = nullptr;
  size_t n_ranks = 0;
  size_t n_nodes = 0;
  uint64_t call_count = 0;
  uint64_t total_latency_ns = 0;
  uint64_t last_latency_ns = 0;
  uint64_t rolling_p99_ns = 0;
  int last_channels = 1;
  bool loaded_from_file = false;
  std::string policy_source = "hardcoded-noop";
  std::string section_name = "uprobe";
  std::unordered_map<std::string, int> map_fds;
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

std::mutex g_runtime_mu;
size_t g_runtime_users = 0;

void ensure_bpftime_shm_name()
{
  const char *existing = getenv("BPFTIME_GLOBAL_SHM_NAME");
  char buffer[128];

  if (existing && existing[0] != '\0')
    return;

  snprintf(buffer, sizeof(buffer), "nccl_policy_bpftime_%u_%d",
           static_cast<unsigned>(geteuid()), static_cast<int>(getpid()));
  setenv("BPFTIME_GLOBAL_SHM_NAME", buffer, 0);
}

uint64_t monotonic_time_ns()
{
  struct timespec ts;
  clock_gettime(CLOCK_MONOTONIC_RAW, &ts);
  return static_cast<uint64_t>(ts.tv_sec) * 1000000000ull +
         static_cast<uint64_t>(ts.tv_nsec);
}

uint64_t update_p99_estimate(uint64_t current_p99, uint64_t sample_ns)
{
  if (current_p99 == 0 || sample_ns > current_p99)
    return sample_ns;
  return (current_p99 * 99 + sample_ns) / 100;
}

void log_plugin_message(PluginContext *ctx, ncclDebugLogLevel level,
                        const char *fmt, ...)
{
  char buffer[512];
  va_list args;
  va_start(args, fmt);
  vsnprintf(buffer, sizeof(buffer), fmt, args);
  va_end(args);

  fprintf(stderr, "[nccl-policy-plugin] %s\n", buffer);
  if (ctx && ctx->log_function)
    ctx->log_function(level, NCCL_TUNING, __FILE__, __LINE__, "%s", buffer);
}

void acquire_bpftime_runtime()
{
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

void release_bpftime_runtime()
{
  std::lock_guard<std::mutex> lock(g_runtime_mu);
  if (g_runtime_users == 0)
    return;
  if (--g_runtime_users == 0) {
    bpftime_destroy_global_shm();
    bpftime_remove_global_shm();
  }
}

VerifyMode get_verify_mode()
{
  const char *mode = getenv("NCCL_POLICY_VERIFY_MODE");
  if (!mode || mode[0] == '\0')
    return VerifyMode::kStrict;

  if (strcmp(mode, "none") == 0)
    return VerifyMode::kNone;
  if (strcmp(mode, "warning") == 0 || strcmp(mode, "warn") == 0)
    return VerifyMode::kWarning;
  return VerifyMode::kStrict;
}

uint64_t parse_env_u64(const char *name, uint64_t fallback)
{
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

bool verifier_supports_section(const std::string &section_name)
{
  return section_name.rfind("uprobe", 0) == 0 ||
         section_name.rfind("uretprobe", 0) == 0 ||
         section_name.rfind("tracepoint", 0) == 0;
}

std::string verifier_section_name(const std::string &section_name)
{
  if (verifier_supports_section(section_name))
    return section_name;
  return "uprobe";
}

bool collect_map_helpers(std::vector<int32_t> *helper_ids)
{
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

bool register_helpers(bpftime::bpftime_prog *prog)
{
  bpftime::bpftime_helper_group helpers =
      bpftime::bpftime_helper_group::get_kernel_utils_helper_group();
  if (helpers.append(
          bpftime::bpftime_helper_group::get_shm_maps_helper_group()) < 0)
    return false;
  return helpers.add_helper_group_to_prog(prog) == 0;
}

bool create_bpftime_maps(PluginContext *ctx, struct bpf_object *obj)
{
  struct bpf_map *map = nullptr;

  bpf_object__for_each_map(map, obj) {
    bpftime::bpf_map_attr attr = {};
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

    fd = bpftime_maps_create(-1, bpf_map__name(map), attr);
    if (fd < 0) {
      log_plugin_message(ctx, NCCL_LOG_WARN,
                         "failed to create bpftime map %s",
                         bpf_map__name(map));
      return false;
    }
    ctx->map_fds.emplace(bpf_map__name(map), fd);
  }

  return true;
}

bool extract_program_spec(PluginContext *ctx, struct bpf_object *obj,
                          ProgramSpec *spec)
{
  struct bpf_program *prog = bpf_object__next_program(obj, nullptr);
  const struct bpf_insn *insns = nullptr;
  size_t insn_cnt = 0;

  if (!prog) {
    log_plugin_message(ctx, NCCL_LOG_WARN, "no program found in BPF object");
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

bool relocate_program_maps(PluginContext *ctx, const char *path,
                           ProgramSpec *spec)
{
  int fd = -1;
  Elf *elf = nullptr;
  Elf_Scn *scn = nullptr;
  Elf_Scn *symtab_scn = nullptr;
  GElf_Shdr symtab_shdr = {};
  size_t shstrndx = 0;
  size_t target_sec_index = 0;
  std::unordered_map<size_t, MapSymbol> symbols;
  std::vector<ebpf_inst> relocated = spec->insns;

  if (ctx->map_fds.empty())
    return true;

  if (elf_version(EV_CURRENT) == EV_NONE) {
    log_plugin_message(ctx, NCCL_LOG_WARN, "libelf initialization failed");
    return false;
  }

  fd = open(path, O_RDONLY);
  if (fd < 0) {
    log_plugin_message(ctx, NCCL_LOG_WARN, "failed to open %s for relocation",
                       path);
    return false;
  }

  elf = elf_begin(fd, ELF_C_READ, nullptr);
  if (!elf) {
    log_plugin_message(ctx, NCCL_LOG_WARN, "elf_begin failed for %s", path);
    close(fd);
    return false;
  }

  if (elf_getshdrstrndx(elf, &shstrndx) != 0) {
    log_plugin_message(ctx, NCCL_LOG_WARN,
                       "elf_getshdrstrndx failed for %s", path);
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
    log_plugin_message(ctx, NCCL_LOG_WARN,
                       "unable to find symtab or target section for %s", path);
    elf_end(elf);
    close(fd);
    return false;
  }

  {
    Elf_Data *data = elf_getdata(symtab_scn, nullptr);
    const size_t symbol_count = symtab_shdr.sh_size / symtab_shdr.sh_entsize;
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

    const size_t reloc_count = shdr.sh_size / shdr.sh_entsize;
    for (size_t i = 0; i < reloc_count; ++i) {
      uint64_t offset = 0;
      size_t symbol_index = 0;
      auto symbol_it = symbols.end();
      auto map_it = ctx->map_fds.end();

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

      map_it = ctx->map_fds.find(symbol_it->second.name);
      if (map_it == ctx->map_fds.end())
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

bool verify_program(PluginContext *ctx, const ProgramSpec &spec)
{
  std::vector<int32_t> helper_ids;
  const VerifyMode mode = get_verify_mode();
  const std::string verify_section = verifier_section_name(spec.section_name);

  if (mode == VerifyMode::kNone)
    return true;

  if (!collect_map_helpers(&helper_ids)) {
    log_plugin_message(ctx, NCCL_LOG_WARN,
                       "failed to assemble helper ids for verifier");
    return mode != VerifyMode::kStrict;
  }

  bpftime::verifier::set_available_helpers(helper_ids);
  bpftime::verifier::set_non_kernel_helpers({});

  if (verify_section != spec.section_name) {
    log_plugin_message(ctx, NCCL_LOG_INFO,
                       "verifier does not support section %s, using %s rules",
                       spec.section_name.c_str(), verify_section.c_str());
  }

  auto result = bpftime::verifier::verify_ebpf_program(
      reinterpret_cast<const uint64_t *>(spec.insns.data()), spec.insns.size(),
      verify_section);
  if (!result)
    return true;

  if (mode == VerifyMode::kStrict) {
    log_plugin_message(ctx, NCCL_LOG_WARN,
                       "verifier rejected %s: %s", ctx->policy_source.c_str(),
                       result->c_str());
    return false;
  }

  log_plugin_message(ctx, NCCL_LOG_WARN,
                     "verifier warning for %s: %s", ctx->policy_source.c_str(),
                     result->c_str());
  return true;
}

void seed_config_map(PluginContext *ctx)
{
  auto map_it = ctx->map_fds.find("config_map");
  if (map_it == ctx->map_fds.end())
    return;

  const uint64_t target_p99 = parse_env_u64("NCCL_POLICY_SLO_TARGET_NS", 150);
  const uint32_t min_channels =
      static_cast<uint32_t>(parse_env_u64("NCCL_POLICY_MIN_CHANNELS", 1));
  const uint32_t max_channels =
      static_cast<uint32_t>(parse_env_u64("NCCL_POLICY_MAX_CHANNELS", 8));
  const uint32_t step =
      static_cast<uint32_t>(parse_env_u64("NCCL_POLICY_AGGRESSIVENESS_STEP",
                                          1));

  for (uint32_t coll_type = 0; coll_type <= 4; ++coll_type) {
    const nccl_policy_config_key key = {.coll_type = coll_type};
    const nccl_policy_config_value value = {
        .target_p99_ns = target_p99,
        .min_channels = min_channels,
        .max_channels = max_channels,
        .aggressiveness_step = step,
    };
    (void)bpftime_map_update_elem(map_it->second, &key, &value, BPF_ANY);
  }
}

bool load_hardcoded_program(PluginContext *ctx)
{
  auto config = bpftime::construct_agent_config_from_env();
  config.set_vm_name("llvm");
  ctx->prog = std::make_unique<bpftime::bpftime_prog>(
      kHardcodedNoopProgram, std::size(kHardcodedNoopProgram),
      "hardcoded-noop", std::move(config));
  if (!ctx->prog || !register_helpers(ctx->prog.get()))
    return false;
  if (ctx->prog->bpftime_prog_load(true) < 0) {
    log_plugin_message(ctx, NCCL_LOG_WARN,
                       "failed to load hardcoded noop policy");
    return false;
  }
  ctx->policy_source = "hardcoded-noop";
  ctx->section_name = "uprobe";
  return true;
}

bool load_program_from_object(PluginContext *ctx, const char *path)
{
  std::unique_ptr<struct bpf_object, decltype(&bpf_object__close)> obj(
      bpf_object__open_file(path, nullptr), &bpf_object__close);
  ProgramSpec spec;
  auto config = bpftime::construct_agent_config_from_env();

  if (!obj) {
    log_plugin_message(ctx, NCCL_LOG_WARN,
                       "unable to open BPF object %s", path);
    return false;
  }

  ctx->policy_source = path;

  if (!extract_program_spec(ctx, obj.get(), &spec)) {
    log_plugin_message(ctx, NCCL_LOG_WARN,
                       "unable to extract a program from %s", path);
    return false;
  }
  if (!create_bpftime_maps(ctx, obj.get()))
    return false;
  if (!relocate_program_maps(ctx, path, &spec))
    return false;
  if (!verify_program(ctx, spec))
    return false;

  config.set_vm_name("llvm");
  ctx->prog = std::make_unique<bpftime::bpftime_prog>(
      spec.insns.data(), spec.insns.size(), spec.name.c_str(),
      std::move(config));
  if (!ctx->prog) {
    log_plugin_message(ctx, NCCL_LOG_WARN,
                       "failed to allocate bpftime_prog for %s", path);
    return false;
  }
  if (!register_helpers(ctx->prog.get())) {
    log_plugin_message(ctx, NCCL_LOG_WARN,
                       "failed to register helper groups for %s", path);
    return false;
  }
  if (ctx->prog->bpftime_prog_load(true) < 0) {
    log_plugin_message(ctx, NCCL_LOG_WARN,
                       "failed to JIT-load policy %s", path);
    return false;
  }

  ctx->loaded_from_file = true;
  ctx->section_name = spec.section_name;
  seed_config_map(ctx);
  return true;
}

bool warmup_program(PluginContext *ctx)
{
  struct nccl_policy_ctx warmup_ctx = {};
  uint64_t action = 0;
  const uint64_t start_ns = monotonic_time_ns();
  int err = 0;

  warmup_ctx.n_bytes = 1024;
  warmup_ctx.coll_type = static_cast<uint32_t>(ncclFuncAllReduce);
  warmup_ctx.num_pipe_ops = 1;
  warmup_ctx.n_ranks = static_cast<uint32_t>(ctx->n_ranks);
  warmup_ctx.n_nodes = static_cast<uint32_t>(ctx->n_nodes);
  warmup_ctx.current_channels = static_cast<uint32_t>(ctx->last_channels);

  err = ctx->prog->bpftime_prog_exec(&warmup_ctx, sizeof(warmup_ctx), &action);
  if (err < 0) {
    log_plugin_message(ctx, NCCL_LOG_WARN,
                       "warmup execution failed for %s",
                       ctx->policy_source.c_str());
    return false;
  }

  fprintf(stderr,
          "[nccl-policy-plugin] init warmup bytes=%" PRIu64
          " action=%" PRIu64 " latency_ns=%" PRIu64 "\n",
          warmup_ctx.n_bytes, action, monotonic_time_ns() - start_ns);
  return true;
}

void apply_policy_action(uint64_t action, float **coll_cost_table, int num_algo,
                         int num_proto, int *n_channels)
{
  const uint8_t flags = nccl_policy_action_flags_get(action);
  const int algo = nccl_policy_action_algo(action);
  const int proto = nccl_policy_action_proto(action);
  const int channels = nccl_policy_action_channels(action);

  if ((flags & NCCL_POLICY_ACTION_SET_CHANNELS) && channels > 0)
    *n_channels = channels;

  if (!coll_cost_table)
    return;

  if ((flags & NCCL_POLICY_ACTION_SET_ALGO) &&
      (flags & NCCL_POLICY_ACTION_SET_PROTO) &&
      algo >= 0 && proto >= 0 && algo < num_algo && proto < num_proto &&
      coll_cost_table[algo][proto] != NCCL_ALGO_PROTO_IGNORE) {
    coll_cost_table[algo][proto] = 0.0f;
  }
}

ncclResult_t pluginInitImpl(void **context, uint64_t comm_id, size_t n_ranks,
                            size_t n_nodes,
                            ncclDebugLogger_t log_function,
                            ncclNvlDomainInfo_v5_t *nvl_domain_info,
                            ncclTunerConstants_v5_t *constants)
{
  (void)comm_id;
  (void)nvl_domain_info;
  (void)constants;

  auto *ctx = new (std::nothrow) PluginContext();
  if (!ctx)
    return ncclSystemError;

  acquire_bpftime_runtime();

  ctx->log_function = log_function;
  ctx->n_ranks = n_ranks;
  ctx->n_nodes = n_nodes;

  const char *policy_path = getenv("NCCL_POLICY_BPF_PATH");
  bool loaded = false;

  if (policy_path && policy_path[0] != '\0') {
    loaded = load_program_from_object(ctx, policy_path);
    if (!loaded) {
      delete ctx;
      release_bpftime_runtime();
      return ncclInternalError;
    }
  } else {
    loaded = load_hardcoded_program(ctx);
  }
  if (!loaded || !ctx->prog || !warmup_program(ctx)) {
    delete ctx;
    release_bpftime_runtime();
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
                                   int *n_channels)
{
  auto *ctx = reinterpret_cast<PluginContext *>(context);
  struct nccl_policy_ctx policy_ctx = {};
  uint64_t action = 0;
  const uint64_t start_ns = monotonic_time_ns();
  int err = 0;

  if (!ctx || !ctx->prog || !n_channels)
    return ncclInternalError;

  *n_channels = ctx->last_channels;

  policy_ctx.n_bytes = n_bytes;
  policy_ctx.last_latency_ns = ctx->last_latency_ns;
  policy_ctx.avg_latency_ns =
      ctx->call_count == 0 ? 0 : ctx->total_latency_ns / ctx->call_count;
  policy_ctx.rolling_p99_ns = ctx->rolling_p99_ns;
  policy_ctx.call_count = ctx->call_count;
  policy_ctx.coll_type = static_cast<uint32_t>(coll_type);
  policy_ctx.num_pipe_ops = static_cast<uint32_t>(num_pipe_ops);
  policy_ctx.reg_buff = static_cast<uint32_t>(reg_buff);
  policy_ctx.n_ranks = static_cast<uint32_t>(ctx->n_ranks);
  policy_ctx.n_nodes = static_cast<uint32_t>(ctx->n_nodes);
  policy_ctx.current_channels = static_cast<uint32_t>(ctx->last_channels);

  err = ctx->prog->bpftime_prog_exec(&policy_ctx, sizeof(policy_ctx), &action);
  ctx->last_latency_ns = monotonic_time_ns() - start_ns;
  ctx->rolling_p99_ns =
      update_p99_estimate(ctx->rolling_p99_ns, ctx->last_latency_ns);

  if (err < 0) {
    log_plugin_message(ctx, NCCL_LOG_WARN,
                       "bpftime execution failed for %s",
                       ctx->policy_source.c_str());
    return ncclInternalError;
  }

  apply_policy_action(action, coll_cost_table, num_algo, num_proto, n_channels);
  ctx->last_channels = *n_channels;
  ctx->total_latency_ns += ctx->last_latency_ns;
  ctx->call_count++;

  if (ctx->call_count <= 5 || ctx->call_count % 100000 == 0) {
    fprintf(stderr,
            "[nccl-policy-plugin] call=%" PRIu64 " bytes=%zu action=%" PRIu64
            " latency_ns=%" PRIu64 " channels=%d aggr=%u\n",
            ctx->call_count, n_bytes, action, ctx->last_latency_ns,
            *n_channels, nccl_policy_action_aggressiveness(action));
  }

  return ncclSuccess;
}

ncclResult_t pluginFinalizeImpl(void *context)
{
  auto *ctx = reinterpret_cast<PluginContext *>(context);
  if (!ctx)
    return ncclSuccess;

  const uint64_t avg_latency =
      ctx->call_count == 0 ? 0 : ctx->total_latency_ns / ctx->call_count;
  fprintf(stderr,
          "[nccl-policy-plugin] finalize calls=%" PRIu64
          " avg_latency_ns=%" PRIu64 " last_latency_ns=%" PRIu64
          " p99_estimate_ns=%" PRIu64 " source=%s\n",
          ctx->call_count, avg_latency, ctx->last_latency_ns,
          ctx->rolling_p99_ns, ctx->policy_source.c_str());

  delete ctx;
  release_bpftime_runtime();
  return ncclSuccess;
}

}  // namespace

extern "C" {

static ncclResult_t pluginInit(void **context, uint64_t comm_id, size_t n_ranks,
                               size_t n_nodes,
                               ncclDebugLogger_t log_function,
                               ncclNvlDomainInfo_v5_t *nvl_domain_info,
                               ncclTunerConstants_v5_t *constants)
{
  return pluginInitImpl(context, comm_id, n_ranks, n_nodes, log_function,
                        nvl_domain_info, constants);
}

static ncclResult_t pluginGetCollInfo(void *context, ncclFunc_t coll_type,
                                      size_t n_bytes, int num_pipe_ops,
                                      float **coll_cost_table, int num_algo,
                                      int num_proto, int reg_buff,
                                      int *n_channels)
{
  return pluginGetCollInfoImpl(context, coll_type, n_bytes, num_pipe_ops,
                               coll_cost_table, num_algo, num_proto, reg_buff,
                               n_channels);
}

static ncclResult_t pluginFinalize(void *context)
{
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
