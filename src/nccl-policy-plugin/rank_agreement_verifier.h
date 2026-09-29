#pragma once

#include <array>
#include <cstdint>
#include <deque>
#include <string>
#include <unordered_set>
#include <vector>
#include <linux/bpf.h>

namespace rank_agreement {

// This is a second, collective-safety check after the ordinary eBPF verifier.
// Its inputs are the relocated instructions and the fd of the host-published
// agreed_map. A local input may never select a control path or reach the action.
enum class Kind : uint8_t {
  shared, local, context, stack, agreed_map, local_map,
  agreed_value, local_value
};
struct Value {
  Kind kind = Kind::local;
  int32_t offset = 0;
  bool operator==(const Value &) const = default;
};
struct State {
  std::array<Value, 11> reg{};
  std::array<Value, 512> stack_bytes{};
  bool operator==(const State &) const = default;
};
inline Value scalar() { return {Kind::shared, 0}; }
inline Value local() { return {Kind::local, 0}; }
inline bool rank_local(Value v) {
  return v.kind == Kind::local || v.kind == Kind::local_value ||
         v.kind == Kind::local_map;
}
inline Value join(Value a, Value b) {
  if (a == b) return a;
  if (a.kind == Kind::shared && b.kind == Kind::shared) return scalar();
  return local();
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
inline bool shared_context_access(int offset, int size) {
  // Collective arguments and communicator-wide values. Timing, call count,
  // registration, previous channels and rank-local topology remain local.
  constexpr int ranges[][2] = {
      {0, 8}, {40, 48}, {52, 60}, {64, 68}};
  for (auto &range : ranges)
    if (offset >= range[0] && offset + size <= range[1]) return true;
  return false;
}
inline Value load_memory(const State &s, Value base, int off, int size) {
  if (base.kind == Kind::context)
    return shared_context_access(base.offset + off, size) ? scalar() : local();
  if (base.kind == Kind::agreed_value) return scalar();
  if (base.kind == Kind::stack) {
    int pos = 512 + base.offset + off;
    if (pos < 0 || size <= 0 || pos + size > 512) return local();
    Value result = s.stack_bytes[pos];
    for (int i = 1; i < size; ++i) result = join(result, s.stack_bytes[pos+i]);
    return result;
  }
  return local();
}
inline bool store_memory(State &s, Value base, int off, int size, Value value) {
  if (base.kind == Kind::agreed_value) return false;
  if (base.kind != Kind::stack) return true;
  int pos = 512 + base.offset + off;
  if (pos < 0 || size <= 0 || pos + size > 512) return false;
  for (int i = 0; i < size; ++i) s.stack_bytes[pos+i] = value;
  return true;
}
inline Value arithmetic(Value dst, Value src, uint8_t op, bool src_is_reg,
                        int32_t imm, bool wide) {
  if (wide && (op == BPF_ADD || op == BPF_SUB) && !src_is_reg &&
      (dst.kind == Kind::context || dst.kind == Kind::stack ||
       dst.kind == Kind::agreed_value || dst.kind == Kind::local_value)) {
    dst.offset += op == BPF_ADD ? imm : -imm;
    return dst;
  }
  return dst.kind == Kind::shared && src.kind == Kind::shared
             ? scalar() : local();
}

template <class Inst>
bool check(const std::vector<Inst> &insns,
           const std::unordered_set<int> &agreed_map_fds,
           std::string *reason) {
  auto fail = [&](size_t pc, const char *why) {
    if (reason) *reason = "instruction " + std::to_string(pc) + ": " + why;
    return false;
  };
  if (insns.empty()) return fail(0, "empty program");
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
    const auto &in = insns[pc];
    State s = states[pc];
    size_t next = pc + 1;
    bool conditional = false;
    bool exiting = false;
    uint8_t cls = BPF_CLASS(in.code);
    if (cls == BPF_LD && in.code == (BPF_LD | BPF_DW | BPF_IMM)) {
      if (next >= insns.size()) return fail(pc, "truncated 64-bit load");
      s.reg[in.dst_reg] = in.src_reg == BPF_PSEUDO_MAP_FD
          ? Value{agreed_map_fds.count(in.imm) ? Kind::agreed_map :
                  Kind::local_map, 0}
          : in.src_reg == 0 ? scalar() : local();
      ++next;
    } else if (cls == BPF_LDX && BPF_MODE(in.code) == BPF_MEM) {
      s.reg[in.dst_reg] =
          load_memory(s, s.reg[in.src_reg], in.off, access_size(in.code));
    } else if ((cls == BPF_ST || cls == BPF_STX) &&
               BPF_MODE(in.code) == BPF_MEM) {
      Value value = cls == BPF_ST ? scalar() : s.reg[in.src_reg];
      if (!store_memory(s, s.reg[in.dst_reg], in.off,
                        access_size(in.code), value))
        return fail(pc, "write to agreed snapshot or invalid stack");
    } else if (cls == BPF_ALU || cls == BPF_ALU64) {
      uint8_t op = BPF_OP(in.code);
      bool x = BPF_SRC(in.code) == BPF_X;
      Value rhs = x ? s.reg[in.src_reg] : scalar();
      if (op == BPF_MOV) s.reg[in.dst_reg] = rhs;
      else if (op == BPF_NEG)
        s.reg[in.dst_reg] =
            s.reg[in.dst_reg].kind == Kind::shared ? scalar() : local();
      else
        s.reg[in.dst_reg] = arithmetic(s.reg[in.dst_reg], rhs, op, x, in.imm,
                                           cls == BPF_ALU64);
    } else if (cls == BPF_JMP || cls == BPF_JMP32) {
      uint8_t op = BPF_OP(in.code);
      if (op == BPF_EXIT) {
        if (s.reg[0].kind != Kind::shared)
          return fail(pc, "rank-local action");
        exiting = true;
      } else if (op == BPF_CALL) {
        if (in.src_reg != 0) return fail(pc, "subprogram call is unanalysed");
        if (in.imm == BPF_FUNC_map_lookup_elem) {
          if (s.reg[1].kind == Kind::agreed_map)
            s.reg[0] = {Kind::agreed_value, 0};
          else
            s.reg[0] = {Kind::local_value, 0};
        } else {
          if ((in.imm == BPF_FUNC_map_update_elem ||
               in.imm == BPF_FUNC_map_delete_elem) &&
              s.reg[1].kind == Kind::agreed_map)
            return fail(pc, "policy writes agreed snapshot");
          // Any other helper, including timers, is rank-local. It may also
          // mutate memory reachable through pointer arguments.
          for (int arg = 1; arg <= 5; ++arg)
            if (s.reg[arg].kind == Kind::stack)
              for (auto &byte : s.stack_bytes) byte = local();
          s.reg[0] = local();
        }
        for (int arg = 1; arg <= 5; ++arg) s.reg[arg] = local();
      } else if (op == BPF_JA) {
        int64_t target = static_cast<int64_t>(pc) + 1 + in.off;
        if (target < 0 || target >= static_cast<int64_t>(insns.size()))
          return fail(pc, "invalid jump target");
        next = static_cast<size_t>(target);
      } else {
        Value rhs = BPF_SRC(in.code) == BPF_X ? s.reg[in.src_reg] : scalar();
        if (rank_local(s.reg[in.dst_reg]) || rank_local(rhs))
          return fail(pc, "rank-local control flow");
        conditional = true;
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
    if (!enqueue(next)) return fail(pc, "falls off program");
    if (conditional) {
      int64_t target = static_cast<int64_t>(pc) + 1 + in.off;
      if (target < 0 || target >= static_cast<int64_t>(insns.size()))
        return fail(pc, "invalid branch target");
      enqueue(static_cast<size_t>(target));
    }
  }
  return true;
}

} // namespace rank_agreement
