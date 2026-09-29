#pragma once

#include <array>
#include <cstdint>
#include <deque>
#include <string>
#include <unordered_set>
#include <vector>
#include <linux/bpf.h>

namespace rank_merge_verifier {

enum class Kind : uint8_t {
  shared, local, context, stack, slots_map, agreed_map, slots_value
};
struct Value {
  Kind kind = Kind::local;
  int32_t offset = 0;
  bool operator==(const Value &) const = default;
};
struct State {
  std::array<Value, 11> reg{};
  std::array<Value, 512> stack_bytes{};
};
inline Value shared() { return {Kind::shared, 0}; }
inline Value local() { return {Kind::local, 0}; }
inline Value join(Value a, Value b) {
  return a == b ? a : local();
}
inline bool merge(State &dst, const State &src) {
  bool changed = false;
  for (size_t i = 0; i < dst.reg.size(); ++i) {
    Value v = join(dst.reg[i], src.reg[i]);
    changed |= !(v == dst.reg[i]);
    dst.reg[i] = v;
  }
  for (size_t i = 0; i < dst.stack_bytes.size(); ++i) {
    Value v = join(dst.stack_bytes[i], src.stack_bytes[i]);
    changed |= !(v == dst.stack_bytes[i]);
    dst.stack_bytes[i] = v;
  }
  return changed;
}
inline int access_size(uint8_t code) {
  switch (BPF_SIZE(code)) {
    case BPF_B: return 1;
    case BPF_H: return 2;
    case BPF_W: return 4;
    case BPF_DW: return 8;
    default: return 0;
  }
}
inline Value load(const State &s, Value base, int off, int size) {
  if (base.kind == Kind::context)
    return base.offset + off == 0 && size == 4 ? shared() : local();
  if (base.kind == Kind::slots_value) return shared();
  if (base.kind == Kind::stack) {
    int pos = 512 + base.offset + off;
    if (pos < 0 || size <= 0 || pos + size > 512) return local();
    Value v = s.stack_bytes[pos];
    for (int i = 1; i < size; ++i) v = join(v, s.stack_bytes[pos+i]);
    return v;
  }
  return local();
}
inline bool store(State &s, Value base, int off, int size, Value value) {
  if (base.kind == Kind::slots_value) return false;
  if (base.kind != Kind::stack) return true;
  int pos = 512 + base.offset + off;
  if (pos < 0 || size <= 0 || pos + size > 512) return false;
  for (int i = 0; i < size; ++i) s.stack_bytes[pos+i] = value;
  return true;
}
inline bool shared_bytes(const State &s, Value pointer, size_t n) {
  if (pointer.kind == Kind::slots_value) return n <= 8;
  if (pointer.kind != Kind::stack) return false;
  int pos = 512 + pointer.offset;
  if (pos < 0 || pos + static_cast<int>(n) > 512) return false;
  for (size_t i = 0; i < n; ++i)
    if (s.stack_bytes[pos+i].kind != Kind::shared) return false;
  return true;
}
inline Value arithmetic(Value dst, Value src, uint8_t op, bool is_reg,
                        int32_t imm, bool wide) {
  if (wide && !is_reg && (op == BPF_ADD || op == BPF_SUB) &&
      (dst.kind == Kind::context || dst.kind == Kind::stack ||
       dst.kind == Kind::slots_value)) {
    dst.offset += op == BPF_ADD ? imm : -imm;
    return dst;
  }
  return dst.kind == Kind::shared && src.kind == Kind::shared
             ? shared() : local();
}

template <class Inst>
bool check(const std::vector<Inst> &insns,
           const std::unordered_set<int> &rank_slots_fds,
           const std::unordered_set<int> &agreed_map_fds,
           std::string *reason) {
  auto fail = [&](size_t pc, const char *why) {
    if (reason) *reason = "instruction " + std::to_string(pc) + ": " + why;
    return false;
  };
  if (insns.empty()) return fail(0, "empty merge program");
  State initial{};
  initial.reg.fill(local());
  initial.stack_bytes.fill(local());
  initial.reg[1] = {Kind::context, 0};
  initial.reg[10] = {Kind::stack, 0};
  std::vector<State> states(insns.size());
  std::vector<bool> seen(insns.size(), false);
  std::deque<size_t> queue;
  states[0] = initial;
  seen[0] = true;
  queue.push_back(0);
  size_t steps = 0;
  while (!queue.empty()) {
    size_t pc = queue.front();
    queue.pop_front();
    if (++steps > 200000) return fail(pc, "analysis did not converge");
    auto in = insns[pc];
    State s = states[pc];
    size_t next = pc + 1;
    bool branch = false;
    bool exiting = false;
    uint8_t cls = BPF_CLASS(in.code);
    if (cls == BPF_LD && in.code == (BPF_LD | BPF_DW | BPF_IMM)) {
      if (next >= insns.size()) return fail(pc, "truncated 64-bit load");
      if (in.src_reg == BPF_PSEUDO_MAP_FD) {
        if (rank_slots_fds.count(in.imm))
          s.reg[in.dst_reg] = {Kind::slots_map, 0};
        else if (agreed_map_fds.count(in.imm))
          s.reg[in.dst_reg] = {Kind::agreed_map, 0};
        else
          return fail(pc, "unapproved map");
      } else {
        s.reg[in.dst_reg] = in.src_reg == 0 ? shared() : local();
      }
      ++next;
    } else if (cls == BPF_LDX && BPF_MODE(in.code) == BPF_MEM) {
      s.reg[in.dst_reg] =
          load(s, s.reg[in.src_reg], in.off, access_size(in.code));
    } else if ((cls == BPF_ST || cls == BPF_STX) &&
               BPF_MODE(in.code) == BPF_MEM) {
      Value value = cls == BPF_ST ? shared() : s.reg[in.src_reg];
      if (!store(s, s.reg[in.dst_reg], in.off, access_size(in.code), value))
        return fail(pc, "invalid write");
    } else if (cls == BPF_ALU || cls == BPF_ALU64) {
      uint8_t op = BPF_OP(in.code);
      bool x = BPF_SRC(in.code) == BPF_X;
      Value rhs = x ? s.reg[in.src_reg] : shared();
      if (op == BPF_MOV) s.reg[in.dst_reg] = rhs;
      else if (op == BPF_NEG)
        s.reg[in.dst_reg] =
            s.reg[in.dst_reg].kind == Kind::shared ? shared() : local();
      else
        s.reg[in.dst_reg] =
            arithmetic(s.reg[in.dst_reg], rhs, op, x, in.imm,
                       cls == BPF_ALU64);
    } else if (cls == BPF_JMP || cls == BPF_JMP32) {
      uint8_t op = BPF_OP(in.code);
      if (op == BPF_EXIT) {
        if (s.reg[0].kind != Kind::shared)
          return fail(pc, "rank-local return");
        exiting = true;
      } else if (op == BPF_CALL) {
        if (in.src_reg != 0) return fail(pc, "subprogram call is unanalysed");
        if (in.imm == BPF_FUNC_map_lookup_elem) {
          if (s.reg[1].kind != Kind::slots_map)
            return fail(pc, "only rank_slots may be read");
          if (!shared_bytes(s, s.reg[2], 4))
            return fail(pc, "rank-local rank_slots key");
          s.reg[0] = {Kind::slots_value, 0};
        } else if (in.imm == BPF_FUNC_map_update_elem) {
          if (s.reg[1].kind != Kind::agreed_map)
            return fail(pc, "only agreed_map may be written");
          if (!shared_bytes(s, s.reg[2], 4) ||
              !shared_bytes(s, s.reg[3], 8) ||
              s.reg[4].kind != Kind::shared)
            return fail(pc, "rank-local agreed_map write");
          // The helper's success/failure is a local runtime result.
          s.reg[0] = local();
        } else {
          return fail(pc, "non-deterministic or unapproved helper");
        }
        for (int arg = 1; arg <= 5; ++arg) s.reg[arg] = local();
      } else if (op == BPF_JA) {
        int64_t target = static_cast<int64_t>(pc) + 1 + in.off;
        if (target < 0 || target >= static_cast<int64_t>(insns.size()))
          return fail(pc, "invalid jump");
        next = static_cast<size_t>(target);
      } else {
        Value rhs = BPF_SRC(in.code) == BPF_X ? s.reg[in.src_reg] : shared();
        if ((s.reg[in.dst_reg].kind != Kind::shared &&
             s.reg[in.dst_reg].kind != Kind::context &&
             s.reg[in.dst_reg].kind != Kind::slots_value) ||
            rhs.kind == Kind::local || rhs.kind == Kind::stack ||
            rhs.kind == Kind::agreed_map || rhs.kind == Kind::slots_map)
          return fail(pc, "rank-local control flow");
        if (s.reg[in.dst_reg].kind != Kind::shared ||
            rhs.kind != Kind::shared) {
          if (cls != BPF_JMP || BPF_SRC(in.code) != BPF_K ||
              in.imm != 0 || (op != BPF_JEQ && op != BPF_JNE) ||
              (s.reg[in.dst_reg].kind != Kind::context &&
               s.reg[in.dst_reg].kind != Kind::slots_value))
            return fail(pc, "pointer-dependent control flow");
        }
        branch = true;
      }
    } else {
      return fail(pc, "unsupported instruction");
    }
    auto enqueue = [&](size_t target) {
      if (target >= insns.size()) return false;
      if (!seen[target]) {
        states[target] = s;
        seen[target] = true;
        queue.push_back(target);
      } else if (merge(states[target], s)) {
        queue.push_back(target);
      }
      return true;
    };
    if (exiting) continue;
    if (!enqueue(next)) return fail(pc, "falls off merge program");
    if (branch) {
      int64_t target = static_cast<int64_t>(pc) + 1 + in.off;
      if (target < 0 || target >= static_cast<int64_t>(insns.size()))
        return fail(pc, "invalid branch");
      enqueue(static_cast<size_t>(target));
    }
  }
  return true;
}

} // namespace rank_merge_verifier
