#!/usr/bin/env bash
# Run reproducible nccl-tests arms without embedding cluster topology.
#
# Commands:
#   run       run the configured arm and write its combined output to a log
#   sweep     run each whitespace-separated ARMS entry REPS times
#   selftest  preflight artifacts and print the mpirun command without running
#
# Required/commonly used environment:
#   TEST_BIN    nccl-tests MPI binary (default: ~/nccl-tests/build/all_reduce_perf_mpi)
#   NPROC       total MPI ranks (default: 1)
#   HOSTLIST    optional Open MPI host list, including any slot counts
#   ARM         baseline | noop | policy:<name> | algo:<Algo>[/<Proto>]
#   PROFILER    none | native | ebpf (default: none)
#   PLUGIN_DIR  policy plugin build directory
#   NCCL_LIB_DIR  optional libnccl directory to prepend to LD_LIBRARY_PATH
#   MAX_NCH     optional NCCL_MAX_NCHANNELS value
#   ARMS/REPS   sweep arms and repetitions (default: "baseline noop" / 3)
#   RESULTS_DIR output directory (default: scripts/results)
#
# NCCL topology, transport, and interface settings are deliberately left to
# the caller. Exported NCCL_* variables are forwarded to every MPI rank.

set -euo pipefail

NCCL_BENCH_SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
TEST_BIN="${TEST_BIN:-$HOME/nccl-tests/build/all_reduce_perf_mpi}"
NPROC="${NPROC:-1}"
HOSTLIST="${HOSTLIST:-}"
ARM="${ARM:-baseline}"
PROFILER="${PROFILER:-none}"
PLUGIN_DIR="${PLUGIN_DIR:-$NCCL_BENCH_SCRIPT_DIR/../src/nccl-policy-plugin/build}"
NCCL_LIB_DIR="${NCCL_LIB_DIR:-}"
MAX_NCH="${MAX_NCH:-}"
ARMS="${ARMS:-baseline noop}"
REPS="${REPS:-3}"
RESULTS_DIR="${RESULTS_DIR:-$NCCL_BENCH_SCRIPT_DIR/results}"
MSG_MIN="${MSG_MIN:-8}"
MSG_MAX="${MSG_MAX:-8G}"
FACTOR="${FACTOR:-2}"
ITERS="${ITERS:-20}"
WARMUP="${WARMUP:-5}"
CHECK="${CHECK:-1}"

if [[ -v LD_LIBRARY_PATH ]]; then
    NCCL_BENCH_BASE_LD_LIBRARY_PATH_SET=1
    NCCL_BENCH_BASE_LD_LIBRARY_PATH="$LD_LIBRARY_PATH"
else
    NCCL_BENCH_BASE_LD_LIBRARY_PATH_SET=0
    NCCL_BENCH_BASE_LD_LIBRARY_PATH=""
fi

restore_ld_library_path() {
    if [[ "$NCCL_BENCH_BASE_LD_LIBRARY_PATH_SET" == 1 ]]; then
        export LD_LIBRARY_PATH="$NCCL_BENCH_BASE_LD_LIBRARY_PATH"
    else
        unset LD_LIBRARY_PATH
    fi
}

require_artifact() {
    local label=$1
    local path=$2

    if [[ ! -f "$path" || ! -r "$path" ]]; then
        echo "ERROR: $label is not a readable file: $path" >&2
        return 1
    fi
}

reset_arm_environment() {
    unset NCCL_TUNER_PLUGIN
    unset NCCL_POLICY_BPF_PATH
    unset NCCL_POLICY_VERIFY_MODE
    unset NCCL_POLICY_BENCHMARK_READY
    unset NCCL_PROFILER_PLUGIN
    unset NCCL_POLICY_PROFILER_MODE
    unset NCCL_POLICY_PROFILER_BPF_PATH
    unset NCCL_ALGO
    unset NCCL_PROTO
    unset NCCL_MAX_NCHANNELS
    restore_ld_library_path
}

apply_arm() {
    local arm="${1:-$ARM}"
    local spec

    reset_arm_environment
    ARM_POLICY=""
    ARM_ALGO=""
    ARM_PROTO=""

    case "$arm" in
        baseline)
            ;;
        noop|policy:*)
            ARM_POLICY="${arm#policy:}"
            [[ "$arm" == noop ]] && ARM_POLICY=noop
            if [[ ! "$ARM_POLICY" =~ ^[A-Za-z0-9_.-]+$ ]]; then
                echo "ERROR: invalid policy name: $ARM_POLICY" >&2
                return 1
            fi
            export NCCL_TUNER_PLUGIN="$PLUGIN_DIR/libnccl-policy.so"
            export NCCL_POLICY_BPF_PATH="$PLUGIN_DIR/ebpf-policies/${ARM_POLICY}.bpf.o"
            export NCCL_POLICY_VERIFY_MODE="${POLICY_VERIFY_MODE:-strict}"
            require_artifact "policy plugin" "$NCCL_TUNER_PLUGIN" || return 1
            require_artifact "policy object" "$NCCL_POLICY_BPF_PATH" || return 1
            export NCCL_POLICY_BENCHMARK_READY=1
            ;;
        algo:*)
            spec="${arm#algo:}"
            ARM_ALGO="${spec%%/*}"
            if [[ "$spec" == */* ]]; then
                ARM_PROTO="${spec#*/}"
            fi
            if [[ ! "$ARM_ALGO" =~ ^[A-Za-z0-9_-]+$ ]] ||
               [[ -n "$ARM_PROTO" && ! "$ARM_PROTO" =~ ^[A-Za-z0-9_-]+$ ]]; then
                echo "ERROR: invalid algorithm arm: $arm" >&2
                return 1
            fi
            export NCCL_ALGO="$ARM_ALGO"
            [[ -n "$ARM_PROTO" ]] && export NCCL_PROTO="$ARM_PROTO"
            ;;
        *)
            echo "ERROR: unknown ARM=$arm" >&2
            return 1
            ;;
    esac

    case "$PROFILER" in
        none)
            ;;
        native|ebpf)
            export NCCL_PROFILER_PLUGIN="$PLUGIN_DIR/libnccl-policy.so"
            export NCCL_POLICY_PROFILER_MODE="$PROFILER"
            require_artifact "profiler plugin" "$NCCL_PROFILER_PLUGIN" ||
                return 1
            if [[ "$PROFILER" == ebpf ]]; then
                export NCCL_POLICY_PROFILER_BPF_PATH="$PLUGIN_DIR/ebpf-policies/profiler_latency.bpf.o"
                require_artifact "profiler policy" \
                    "$NCCL_POLICY_PROFILER_BPF_PATH" || return 1
            fi
            ;;
        *)
            echo "ERROR: unknown PROFILER=$PROFILER" >&2
            return 1
            ;;
    esac

    [[ -n "$MAX_NCH" ]] && export NCCL_MAX_NCHANNELS="$MAX_NCH"
    if [[ -n "$NCCL_LIB_DIR" ]]; then
        if [[ "$NCCL_BENCH_BASE_LD_LIBRARY_PATH_SET" == 1 &&
              -n "$NCCL_BENCH_BASE_LD_LIBRARY_PATH" ]]; then
            export LD_LIBRARY_PATH="$NCCL_LIB_DIR:$NCCL_BENCH_BASE_LD_LIBRARY_PATH"
        else
            export LD_LIBRARY_PATH="$NCCL_LIB_DIR"
        fi
    fi
}

# Open MPI sets OMPI_COMM_WORLD_RANK in each process. The plugin emits one READY
# marker only after its tuner v5 object has loaded, verified, JIT-compiled, and
# completed warmup. Require a marker for every requested rank before a run can
# receive a policy-labelled log filename.
verify_policy_load() {
    local log=$1
    local policy_path=$2
    local requested_ranks=$3
    local rank
    local marker

    if [[ ! "$requested_ranks" =~ ^[1-9][0-9]*$ ]]; then
        echo "ERROR: NPROC must be a positive integer: $requested_ranks" >&2
        return 1
    fi

    for ((rank = 0; rank < 10#$requested_ranks; rank++)); do
        marker="[nccl-policy-plugin] READY tuner=v5 rank=$rank policy=$policy_path"
        if ! grep -Fq -- "$marker" "$log"; then
            echo "ERROR: tuner policy load was not proven for MPI rank $rank" >&2
            return 1
        fi
    done
}

# Append Open MPI -x pairs to the named array. compgen's prefix argument avoids
# a pipe whose normal no-match status would abort the script under pipefail.
propagate_flags() {
    local -n output=$1
    local variable

    while IFS= read -r variable; do
        [[ -n "$variable" ]] && output+=(-x "$variable")
    done < <(compgen -e NCCL_ || true)
    if [[ -v LD_LIBRARY_PATH ]]; then
        output+=(-x LD_LIBRARY_PATH)
    fi
}

print_command() {
    printf '%q ' "$@"
    printf '\n'
}

run_once() {
    local mode="${1:-run}"
    local repetition="${2:-1}"
    local arm_tag="${ARM//:/-}"
    local log
    local pending_log
    local -a exports=()
    local -a command=(mpirun -np "$NPROC")

    arm_tag="${arm_tag//\//-}"
    if [[ ! "$NPROC" =~ ^[1-9][0-9]*$ ]]; then
        echo "ERROR: NPROC must be a positive integer: $NPROC" >&2
        return 1
    fi
    apply_arm "$ARM"
    propagate_flags exports
    [[ -n "$HOSTLIST" ]] && command+=(-H "$HOSTLIST")
    command+=("${exports[@]}" "$TEST_BIN" -b "$MSG_MIN" -e "$MSG_MAX"
             -f "$FACTOR" -n "$ITERS" -w "$WARMUP" -c "$CHECK" -g 1)

    if [[ "$mode" == print ]]; then
        print_command "${command[@]}"
        return 0
    fi

    mkdir -p "$RESULTS_DIR"
    log="$RESULTS_DIR/${arm_tag}-r${repetition}.log"
    if [[ -n "$ARM_POLICY" ]]; then
        pending_log="$(mktemp "$RESULTS_DIR/.unverified-r${repetition}.XXXXXX.log")"
        echo "[nccl_bench] starting unverified tuner arm repetition $repetition"
        if ! "${command[@]}" >"$pending_log" 2>&1; then
            echo "ERROR: nccl-tests failed; unverified output: $pending_log" >&2
            return 1
        fi
        if ! verify_policy_load "$pending_log" "$NCCL_POLICY_BPF_PATH" "$NPROC"; then
            echo "ERROR: refusing policy label; unverified output: $pending_log" >&2
            return 1
        fi
        mv "$pending_log" "$log"
        echo "[nccl_bench] validated $ARM repetition $repetition -> $log"
    else
        echo "[nccl_bench] $ARM repetition $repetition -> $log"
        "${command[@]}" >"$log" 2>&1
    fi
}

run_sweep() {
    local arm repetition
    for arm in $ARMS; do
        for repetition in $(seq 1 "$REPS"); do
            ARM="$arm" run_once run "$repetition"
        done
    done
}

main() {
    case "${1:-run}" in
        run)
            run_once run "${REP:-1}"
            ;;
        sweep)
            run_sweep
            ;;
        selftest)
            run_once print "${REP:-1}"
            ;;
        *)
            echo "usage: $0 [run|sweep|selftest]" >&2
            return 2
            ;;
    esac
}

if [[ "${NCCL_BENCH_SOURCE_ONLY:-0}" != 1 ]]; then
    main "$@"
fi
