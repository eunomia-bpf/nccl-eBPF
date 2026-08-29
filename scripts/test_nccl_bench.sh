#!/usr/bin/env bash
set -euo pipefail

TEST_SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
TEST_TMP_DIR="$(mktemp -d)"
cleanup() {
    find "$TEST_TMP_DIR" -type f -delete
    find "$TEST_TMP_DIR" -depth -type d -empty -delete
}
trap cleanup EXIT
mkdir -p "$TEST_TMP_DIR/ebpf-policies"
touch "$TEST_TMP_DIR/ebpf-policies/noop.bpf.o"
touch "$TEST_TMP_DIR/libnccl-policy.so"

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

[[ "$CHECK" == 1 ]] || fail "nccl-tests correctness checking is not enabled by default"

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

mv "$TEST_TMP_DIR/libnccl-policy.so" "$TEST_TMP_DIR/libnccl-policy.so.missing"
if apply_arm noop 2>/dev/null; then
    fail "policy arm accepted a missing plugin library"
fi
mv "$TEST_TMP_DIR/libnccl-policy.so.missing" "$TEST_TMP_DIR/libnccl-policy.so"
apply_arm noop

printf '%s\n' \
    "[nccl-policy-plugin] READY tuner=v5 rank=0 policy=$NCCL_POLICY_BPF_PATH" \
    "[nccl-policy-plugin] READY tuner=v5 rank=1 policy=$NCCL_POLICY_BPF_PATH" \
    >"$TEST_TMP_DIR/ready.log"
verify_policy_load "$TEST_TMP_DIR/ready.log" "$NCCL_POLICY_BPF_PATH" 2 ||
    fail "complete per-rank policy markers were rejected"
printf '%s\n' \
    "[nccl-policy-plugin] READY tuner=v5 rank=0 policy=$NCCL_POLICY_BPF_PATH" \
    >"$TEST_TMP_DIR/missing-rank.log"
if verify_policy_load "$TEST_TMP_DIR/missing-rank.log" \
    "$NCCL_POLICY_BPF_PATH" 2 2>/dev/null; then
    fail "policy run accepted a missing rank marker"
fi

export RESULTS_DIR="$TEST_TMP_DIR/results"
export ARM=noop
export NPROC=2
mpirun() {
    local rank
    for ((rank = 0; rank < NPROC; rank++)); do
        printf '[nccl-policy-plugin] READY tuner=v5 rank=%d policy=%s\n' \
            "$rank" "$NCCL_POLICY_BPF_PATH"
    done
}
run_once run 1 >/dev/null
[[ -f "$RESULTS_DIR/noop-r1.log" ]] ||
    fail "verified run did not receive its policy-labelled log"

mpirun() {
    printf '[nccl-policy-plugin] READY tuner=v5 rank=0 policy=%s\n' \
        "$NCCL_POLICY_BPF_PATH"
}
if run_once run 2 >/dev/null 2>&1; then
    fail "run accepted incomplete per-rank load proof"
fi
[[ ! -e "$RESULTS_DIR/noop-r2.log" ]] ||
    fail "unverified run received a policy-labelled log"

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
