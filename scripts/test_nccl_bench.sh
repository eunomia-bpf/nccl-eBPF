#!/usr/bin/env bash
set -euo pipefail

TEST_SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
TEST_TMP_DIR="$(mktemp -d)"
trap 'rm -f "$TEST_TMP_DIR/ebpf-policies/noop.bpf.o"; rmdir "$TEST_TMP_DIR/ebpf-policies" "$TEST_TMP_DIR"' EXIT
mkdir -p "$TEST_TMP_DIR/ebpf-policies"
touch "$TEST_TMP_DIR/ebpf-policies/noop.bpf.o"

fail() {
    echo "FAIL: $*" >&2
    exit 1
}

assert_unset() {
    local variable
    for variable in "$@"; do
        [[ ! -v $variable ]] || fail "$variable remained set"
    done
}

export LD_LIBRARY_PATH=/original/lib
export NCCL_TUNER_PLUGIN=/stale/plugin.so
export NCCL_POLICY_BPF_PATH=/stale/policy.bpf.o
export NCCL_POLICY_VERIFY_MODE=none
export NCCL_PROFILER_PLUGIN=/stale/profiler.so
export NCCL_POLICY_PROFILER_MODE=ebpf
export NCCL_POLICY_PROFILER_BPF_PATH=/stale/profiler.bpf.o
export NCCL_ALGO=Tree
export NCCL_PROTO=Simple
export NCCL_MAX_NCHANNELS=99
export NCCL_BENCH_SOURCE_ONLY=1
export PLUGIN_DIR="$TEST_TMP_DIR"
# shellcheck disable=SC1091
source "$TEST_SCRIPT_DIR/nccl_bench.sh"

apply_arm baseline
assert_unset NCCL_TUNER_PLUGIN NCCL_POLICY_BPF_PATH NCCL_POLICY_VERIFY_MODE \
    NCCL_PROFILER_PLUGIN NCCL_POLICY_PROFILER_MODE \
    NCCL_POLICY_PROFILER_BPF_PATH NCCL_ALGO NCCL_PROTO NCCL_MAX_NCHANNELS
[[ "$LD_LIBRARY_PATH" == /original/lib ]] || fail "baseline did not restore LD_LIBRARY_PATH"

export NCCL_LIB_DIR=/alternate/lib
apply_arm algo:Ring/LL128
[[ "$NCCL_ALGO" == Ring ]] || fail "algorithm arm was not applied"
[[ "$NCCL_PROTO" == LL128 ]] || fail "protocol arm was not applied"
[[ "$LD_LIBRARY_PATH" == /alternate/lib:/original/lib ]] ||
    fail "alternate library path was not prepended exactly once"

export NCCL_LIB_DIR=""
apply_arm baseline
assert_unset NCCL_ALGO NCCL_PROTO
[[ "$LD_LIBRARY_PATH" == /original/lib ]] || fail "arm state leaked into baseline"

apply_arm noop
[[ "$NCCL_TUNER_PLUGIN" == "$TEST_TMP_DIR/libnccl-policy.so" ]] ||
    fail "policy plugin was not selected"
[[ "$NCCL_POLICY_BPF_PATH" == "$TEST_TMP_DIR/ebpf-policies/noop.bpf.o" ]] ||
    fail "policy object was not selected"

export NCCL_TEST_PROPAGATION=enabled
flags=()
propagate_flags flags
joined=" ${flags[*]} "
[[ "$joined" == *" -x NCCL_TEST_PROPAGATION "* ]] ||
    fail "NCCL-prefixed variable was not propagated"
[[ "$joined" == *" -x LD_LIBRARY_PATH "* ]] ||
    fail "LD_LIBRARY_PATH was not propagated explicitly"

unset NCCL_TEST_PROPAGATION
echo "nccl_bench arm isolation: PASS"
