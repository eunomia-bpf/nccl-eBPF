#include "../rank_merge_verifier.h"
#include <cassert>
#include <cstdio>
#include <linux/bpf.h>
#include <string>
#include <unordered_set>
#include <vector>

static bpf_insn insn(uint8_t code, uint8_t dst = 0, uint8_t src = 0,
                     int16_t off = 0, int32_t imm = 0) {
  bpf_insn i{};
  i.code = code;
  i.dst_reg = dst;
  i.src_reg = src;
  i.off = off;
  i.imm = imm;
  return i;
}
static bool check(const std::vector<bpf_insn> &p, std::string *why) {
  return rank_merge_verifier::check(p, {7}, {8}, why);
}
int main() {
  std::string why;
  auto exit = insn(BPF_JMP | BPF_EXIT);
  auto merged = std::vector<bpf_insn>{
      insn(BPF_ST | BPF_MEM | BPF_W, 10, 0, -4, 0),
      insn(BPF_LD | BPF_DW | BPF_IMM, 1, BPF_PSEUDO_MAP_FD, 0, 7),
      insn(0),
      insn(BPF_ALU64 | BPF_MOV | BPF_X, 2, 10),
      insn(BPF_ALU64 | BPF_ADD | BPF_K, 2, 0, 0, -4),
      insn(BPF_JMP | BPF_CALL, 0, 0, 0, BPF_FUNC_map_lookup_elem),
      insn(BPF_LDX | BPF_MEM | BPF_DW, 3, 0, 0),
      insn(BPF_STX | BPF_MEM | BPF_DW, 10, 3, -16),
      insn(BPF_ST | BPF_MEM | BPF_W, 10, 0, -20, 0),
      insn(BPF_LD | BPF_DW | BPF_IMM, 1, BPF_PSEUDO_MAP_FD, 0, 8),
      insn(0),
      insn(BPF_ALU64 | BPF_MOV | BPF_X, 2, 10),
      insn(BPF_ALU64 | BPF_ADD | BPF_K, 2, 0, 0, -20),
      insn(BPF_ALU64 | BPF_MOV | BPF_X, 3, 10),
      insn(BPF_ALU64 | BPF_ADD | BPF_K, 3, 0, 0, -16),
      insn(BPF_ALU64 | BPF_MOV | BPF_K, 4, 0, 0, 0),
      insn(BPF_JMP | BPF_CALL, 0, 0, 0, BPF_FUNC_map_update_elem),
      insn(BPF_ALU64 | BPF_MOV | BPF_K, 0, 0, 0, 0),
      exit};
  assert(check(merged, &why));

  auto rank_local_key = merged;
  rank_local_key[0] = insn(BPF_STX | BPF_MEM | BPF_W, 10, 1, -4);
  assert(!check(rank_local_key, &why));
  assert(why.find("rank-local rank_slots key") != std::string::npos);

  auto wrong_map = merged;
  wrong_map[1].imm = 9;
  assert(!check(wrong_map, &why));
  assert(why.find("unapproved map") != std::string::npos);

  auto wrong_write = merged;
  wrong_write[9].imm = 7;
  assert(!check(wrong_write, &why));
  assert(why.find("only agreed_map") != std::string::npos);

  auto rank_local_value = merged;
  rank_local_value[6] = insn(BPF_LDX | BPF_MEM | BPF_DW, 3, 1, 0);
  assert(!check(rank_local_value, &why));
  assert(why.find("rank-local agreed_map write") != std::string::npos);

  auto clock = std::vector<bpf_insn>{
      insn(BPF_JMP | BPF_CALL, 0, 0, 0, BPF_FUNC_ktime_get_ns),
      exit};
  assert(!check(clock, &why));
  auto random = std::vector<bpf_insn>{
      insn(BPF_JMP | BPF_CALL, 0, 0, 0, BPF_FUNC_get_prandom_u32),
      exit};
  assert(!check(random, &why));

  auto reserved = std::vector<bpf_insn>{
      insn(BPF_LDX | BPF_MEM | BPF_W, 0, 1, 4),
      exit};
  assert(!check(reserved, &why));
  assert(why.find("rank-local return") != std::string::npos);
  auto nranks = std::vector<bpf_insn>{
      insn(BPF_LDX | BPF_MEM | BPF_W, 0, 1, 0),
      exit};
  assert(check(nranks, &why));

  auto update_status = merged;
  update_status.erase(update_status.end()-2);
  assert(!check(update_status, &why));
  assert(why.find("rank-local return") != std::string::npos);

  assert(!check({insn(BPF_JMP | BPF_JEQ | BPF_K, 1, 0, 1, 1),
                 insn(BPF_ALU64 | BPF_MOV | BPF_K, 0, 0, 0, 0),
                 exit}, &why));
  assert(why.find("pointer-dependent") != std::string::npos);
  std::puts("rank merge verifier tests passed");
}
