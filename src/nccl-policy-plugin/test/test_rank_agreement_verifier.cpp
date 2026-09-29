#include "../rank_agreement_verifier.h"
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
static bool check(std::vector<bpf_insn> p, bool agreed, std::string *why) {
  return rank_agreement::check(p, agreed ? std::unordered_set<int>{7}
                                           : std::unordered_set<int>{}, why);
}
int main() {
  std::string why;
  auto exit = insn(BPF_JMP | BPF_EXIT);
  auto load_shared = insn(BPF_LDX | BPF_MEM | BPF_DW, 0, 1, 0);
  auto load_local = insn(BPF_LDX | BPF_MEM | BPF_DW, 0, 1, 8);
  assert(check({load_shared, exit}, false, &why));
  assert(!check({load_local, exit}, false, &why));
  assert(why.find("rank-local action") != std::string::npos);

  auto branch_local = std::vector<bpf_insn>{
      insn(BPF_LDX | BPF_MEM | BPF_DW, 2, 1, 8),
      insn(BPF_JMP | BPF_JEQ | BPF_K, 2, 0, 1, 0),
      insn(BPF_ALU64 | BPF_MOV | BPF_K, 0, 0, 0, 0),
      exit};
  assert(!check(branch_local, false, &why));
  assert(why.find("control flow") != std::string::npos);

  auto lookup = std::vector<bpf_insn>{
      insn(BPF_LD | BPF_DW | BPF_IMM, 1, BPF_PSEUDO_MAP_FD, 0, 7),
      insn(0),
      insn(BPF_JMP | BPF_CALL, 0, 0, 0, BPF_FUNC_map_lookup_elem),
      insn(BPF_LDX | BPF_MEM | BPF_W, 0, 0, 0),
      exit};
  assert(!check(lookup, false, &why));
  assert(check(lookup, true, &why));
  lookup[3] = insn(BPF_JMP | BPF_JEQ | BPF_K, 0, 0, 1, 0);
  lookup.insert(lookup.end()-1, insn(BPF_ALU64 | BPF_MOV | BPF_K, 0, 0, 0, 0));
  lookup.insert(lookup.end()-1, insn(BPF_ALU64 | BPF_MOV | BPF_K, 0, 0, 0, 0));
  assert(!check(lookup, false, &why));
  assert(check(lookup, true, &why));

  auto write_agreed = std::vector<bpf_insn>{
      insn(BPF_LD | BPF_DW | BPF_IMM, 1, BPF_PSEUDO_MAP_FD, 0, 7),
      insn(0),
      insn(BPF_JMP | BPF_CALL, 0, 0, 0, BPF_FUNC_map_update_elem),
      insn(BPF_ALU64 | BPF_MOV | BPF_K, 0, 0, 0, 0),
      exit};
  assert(!check(write_agreed, true, &why));
  assert(why.find("writes agreed") != std::string::npos);

  assert(!check({insn(BPF_JMP | BPF_CALL, 0, 0, 0,
                      BPF_FUNC_ktime_get_ns), exit}, false, &why));
  assert(check({insn(BPF_ALU64 | BPF_MOV | BPF_K, 0, 0, 0, 0),
                exit}, false, &why));
  assert(!check({insn(BPF_ALU64 | BPF_MOV | BPF_X, 0, 1),
                 insn(BPF_ALU64 | BPF_AND | BPF_K, 0, 0, 0, 1),
                 exit}, false, &why));
  assert(!check({insn(BPF_ALU64 | BPF_MOV | BPF_X, 0, 1),
                 exit}, false, &why));
  assert(!check({insn(BPF_JMP | BPF_JEQ | BPF_K, 1, 0, 1, 1),
                 insn(BPF_ALU64 | BPF_MOV | BPF_K, 0, 0, 0, 0),
                 exit}, false, &why));
  assert(why.find("pointer-dependent") != std::string::npos);
  std::puts("rank agreement verifier tests passed");
}
