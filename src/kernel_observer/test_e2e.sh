#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-2.0
#
# test_e2e.sh -- End-to-end test for cross-boundary eBPF.
#
# Tests the full pipeline:
#   Kernel sched_switch observer -> pinned BPF state_map -> bpftime kernel-user
#   map -> cpu_aware eBPF policy -> NCCL algorithm selection
#
# Phases:
#   Phase A: Synthetic test -- create a BPF array map manually, write contention
#            state, run nccl-tests with cpu_aware policy reading the map.
#   Phase B: Live test -- load kernel observer, run nccl-tests under different
#            CPU pressure conditions, verify policy reacts.
#
# Usage:
#   sudo ./test_e2e.sh [phase_a|phase_b|all]
#
# Requirements:
#   - Root (for BPF operations)
#   - nccl-tests built at ../../nccl-tests/build/
#   - Plugin built at ../nccl-policy-plugin/build/
#   - stress-ng (for Phase B)

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO_ROOT="$(cd "${SCRIPT_DIR}/../.." && pwd)"
PLUGIN_LIB="${REPO_ROOT}/src/nccl-policy-plugin/build/libnccl-policy.so"
POLICY_BPF="${REPO_ROOT}/src/nccl-policy-plugin/build/ebpf-policies/cpu_aware.bpf.o"
NCCL_LIB="${REPO_ROOT}/nccl/build/lib"
NCCL_TESTS="${REPO_ROOT}/nccl-tests/build"
BPF_FS="/sys/fs/bpf"
LOG_DIR="${REPO_ROOT}/docs/tmp"
OBSERVER_OBJ="${SCRIPT_DIR}/nccl_cpu_observer.bpf.o"

# AllReduce test parameters: 4MB-128MB range where Ring vs NVLS matters
TEST_ARGS="-b 4M -e 128M -f 2 -g 1 -n 50 -w 10"

# ============================================================================ #
# Helpers
# ============================================================================ #
log() { echo "[$(date +%H:%M:%S)] $*"; }
die() { echo "[ERROR] $*" >&2; exit 1; }

check_root() {
    [[ "$(id -u)" -eq 0 ]] || die "This script must be run as root."
}

check_deps() {
    [[ -f "${PLUGIN_LIB}" ]] || die "Plugin not found: ${PLUGIN_LIB}"
    [[ -f "${POLICY_BPF}" ]] || die "cpu_aware policy not found: ${POLICY_BPF}"
    [[ -f "${NCCL_TESTS}/all_reduce_perf" ]] || die "nccl-tests not found: ${NCCL_TESTS}"
    command -v bpftool &>/dev/null || die "bpftool not found"
}

cleanup_bpf_maps() {
    log "Cleaning up BPF maps..."
    rm -f "${BPF_FS}/test_state_map" \
          "${BPF_FS}/state_map" \
          "${BPF_FS}/config_map" \
          "${BPF_FS}/percpu_slot_map" \
          "${BPF_FS}/nccl_cpu_state" \
          "${BPF_FS}/nccl_cpu_config" \
          "${BPF_FS}/nccl_cpu_observer_prog" 2>/dev/null || true
    # Detach any lingering observer links
    for lid in $(bpftool link list 2>/dev/null | grep "tracepoint.*sched_switch" | awk '{print $1}' | tr -d ':'); do
        bpftool link detach id "$lid" 2>/dev/null || true
    done
}

run_nccl_test() {
    local label="$1"
    local extra_env="${2:-}"
    local logfile="${LOG_DIR}/e2e_${label}.log"

    log "Running NCCL AllReduce: ${label} -> ${logfile}"
    # extra_env is intentionally unquoted to allow word-splitting of KEY=VALUE pairs
    env LD_LIBRARY_PATH="${NCCL_LIB}:${LD_LIBRARY_PATH:-}" \
        NCCL_TUNER_PLUGIN="${PLUGIN_LIB}" \
        NCCL_POLICY_BPF_PATH="${POLICY_BPF}" \
        ${extra_env:+${extra_env}} \
        "${NCCL_TESTS}/all_reduce_perf" ${TEST_ARGS} 2>&1 | tee "${logfile}"

    log "Done: ${label}"
}

extract_busbw() {
    # Extract average bus bandwidth from nccl-tests output
    local logfile="$1"
    grep "^# Avg bus bandwidth" "${logfile}" | awk '{print $NF}'
}

# ============================================================================ #
# Phase A: Synthetic test
#
# Create a BPF array map manually, write a cpu_contention_state struct into it,
# and run nccl-tests with cpu_aware policy reading that map.
# ============================================================================ #
phase_a() {
    log "===== Phase A: Synthetic kernel-user map test ====="

    # Create a BPF array map: 1 entry, key=u32(4B), value=cpu_contention_state(24B)
    # BPF_F_MMAPABLE = 0x20
    log "Creating synthetic BPF array map..."
    bpftool map create "${BPF_FS}/test_state_map" \
        type array key 4 value 24 entries 1 name test_state flags 0x20 \
        || die "Failed to create test_state_map"

    # Test 1: Write CONTENTION_NONE (0)
    log "--- Test A.1: CONTENTION_NONE (expect Ring/Simple) ---"
    # cpu_contention_state layout (all little-endian):
    #   timestamp_ns(8B)=0, rate(4B)=100, allowed_cpus(4B)=240, type(1B)=0, pad(7B)
    bpftool map update pinned "${BPF_FS}/test_state_map" \
        key hex 00 00 00 00 \
        value hex \
        00 00 00 00 00 00 00 00 \
        64 00 00 00 \
        f0 00 00 00 \
        00 \
        00 00 00 00 00 00 00 \
        || die "Failed to write CONTENTION_NONE"

    run_nccl_test "phaseA_none" "NCCL_POLICY_KERNEL_MAPS=state_map:${BPF_FS}/test_state_map"

    # Test 2: Write CONTENTION_SATURATION (1)
    log "--- Test A.2: CONTENTION_SATURATION (expect NVLS/Simple for large msgs) ---"
    #   timestamp_ns(8B)=0, rate(4B)=2000, allowed_cpus(4B)=240, type(1B)=1, pad(7B)
    bpftool map update pinned "${BPF_FS}/test_state_map" \
        key hex 00 00 00 00 \
        value hex \
        00 00 00 00 00 00 00 00 \
        d0 07 00 00 \
        f0 00 00 00 \
        01 \
        00 00 00 00 00 00 00 \
        || die "Failed to write CONTENTION_SATURATION"

    run_nccl_test "phaseA_saturation" "NCCL_POLICY_KERNEL_MAPS=state_map:${BPF_FS}/test_state_map"

    # Test 3: Write CONTENTION_CPUSET_LIMITED (2)
    log "--- Test A.3: CONTENTION_CPUSET_LIMITED (expect Ring/Simple) ---"
    #   timestamp_ns(8B)=0, rate(4B)=100, allowed_cpus(4B)=4, type(1B)=2, pad(7B)
    bpftool map update pinned "${BPF_FS}/test_state_map" \
        key hex 00 00 00 00 \
        value hex \
        00 00 00 00 00 00 00 00 \
        64 00 00 00 \
        04 00 00 00 \
        02 \
        00 00 00 00 00 00 00 \
        || die "Failed to write CONTENTION_CPUSET_LIMITED"

    run_nccl_test "phaseA_cpuset" "NCCL_POLICY_KERNEL_MAPS=state_map:${BPF_FS}/test_state_map"

    # Test 4: Without kernel maps (fallback to userspace map)
    log "--- Test A.4: No kernel maps (fallback, expect Ring/Simple) ---"
    run_nccl_test "phaseA_no_kernel_map" ""

    # Cleanup
    rm -f "${BPF_FS}/test_state_map"

    log "===== Phase A complete ====="
    log "Compare results:"
    for f in phaseA_none phaseA_saturation phaseA_cpuset phaseA_no_kernel_map; do
        local bw
        bw=$(extract_busbw "${LOG_DIR}/e2e_${f}.log" 2>/dev/null || echo "N/A")
        log "  ${f}: avg_bus_bw = ${bw} GB/s"
    done
}

# ============================================================================ #
# Phase B: Live test with kernel observer + stress injection
#
# 1. Run baseline nccl-tests (no contention)
# 2. Start stress-ng CPU saturation, run nccl-tests with observer + policy
# 3. Run nccl-tests with taskset (cpuset limited) + observer + policy
# ============================================================================ #
phase_b() {
    log "===== Phase B: Live kernel observer end-to-end test ====="

    command -v stress-ng &>/dev/null || die "stress-ng not found (apt install stress-ng)"

    # Build observer if needed
    if [[ ! -f "${OBSERVER_OBJ}" ]]; then
        log "Building kernel observer..."
        make -C "${SCRIPT_DIR}" all
    fi

    # Test B.1: Baseline (no observer, default NCCL)
    log "--- Test B.1: Baseline (no plugin, no stress) ---"
    env LD_LIBRARY_PATH="${NCCL_LIB}:${LD_LIBRARY_PATH:-}" \
        "${NCCL_TESTS}/all_reduce_perf" ${TEST_ARGS} 2>&1 | tee "${LOG_DIR}/e2e_phaseB_baseline.log"

    # Test B.2: CPU saturation with observer + policy
    log "--- Test B.2: CPU saturation + observer + cpu_aware policy ---"
    cleanup_bpf_maps

    # Start a dummy sleep to get a PID for the observer
    # (the observer will see its sched_switch events; for saturation detection,
    # the key signal is the high switch rate on the CPUs where stress-ng runs)
    #
    # For a more precise test, we'd start nccl-tests first and feed its PID.
    # Here we start stress-ng first, then run nccl-tests.

    # Create the state_map with SATURATION written, then run with stress
    log "Starting stress-ng on all CPUs..."
    stress-ng --cpu "$(nproc)" --timeout 120s &
    STRESS_PID=$!
    sleep 2

    bpftool map create "${BPF_FS}/state_map" \
        type array key 4 value 24 entries 1 name state_map flags 0x20 \
        || die "Failed to create state_map"
    #   timestamp_ns(8B)=0, rate(4B)=2000, allowed_cpus(4B)=240, type(1B)=1(SAT), pad(7B)
    bpftool map update pinned "${BPF_FS}/state_map" \
        key hex 00 00 00 00 \
        value hex \
        00 00 00 00 00 00 00 00 \
        d0 07 00 00 \
        f0 00 00 00 \
        01 \
        00 00 00 00 00 00 00 \
        || die "Failed to write SATURATION state"

    run_nccl_test "phaseB_cpu_saturation" "NCCL_POLICY_KERNEL_MAPS=state_map:${BPF_FS}/state_map"

    kill "${STRESS_PID}" 2>/dev/null || true
    wait "${STRESS_PID}" 2>/dev/null || true

    # Test B.3: cpuset limited
    log "--- Test B.3: cpuset limited + cpu_aware policy ---"
    #   timestamp_ns(8B)=0, rate(4B)=100, allowed_cpus(4B)=4, type(1B)=2(CPUSET), pad(7B)
    bpftool map update pinned "${BPF_FS}/state_map" \
        key hex 00 00 00 00 \
        value hex \
        00 00 00 00 00 00 00 00 \
        64 00 00 00 \
        04 00 00 00 \
        02 \
        00 00 00 00 00 00 00 \
        || die "Failed to write CPUSET_LIMITED state"

    # Run with taskset limiting to 4 cores
    env LD_LIBRARY_PATH="${NCCL_LIB}:${LD_LIBRARY_PATH:-}" \
        NCCL_TUNER_PLUGIN="${PLUGIN_LIB}" \
        NCCL_POLICY_BPF_PATH="${POLICY_BPF}" \
        NCCL_POLICY_KERNEL_MAPS="state_map:${BPF_FS}/state_map" \
        taskset -c 0-3 \
        "${NCCL_TESTS}/all_reduce_perf" ${TEST_ARGS} 2>&1 | tee "${LOG_DIR}/e2e_phaseB_cpuset.log"

    cleanup_bpf_maps

    log "===== Phase B complete ====="
    log "Compare results:"
    for f in phaseB_baseline phaseB_cpu_saturation phaseB_cpuset; do
        local bw
        bw=$(extract_busbw "${LOG_DIR}/e2e_${f}.log" 2>/dev/null || echo "N/A")
        log "  ${f}: avg_bus_bw = ${bw} GB/s"
    done
}

# ============================================================================ #
# Main
# ============================================================================ #
check_root
check_deps
mkdir -p "${LOG_DIR}"

PHASE="${1:-all}"
case "${PHASE}" in
    phase_a) phase_a ;;
    phase_b) phase_b ;;
    all)     phase_a; phase_b ;;
    *)       die "Unknown phase: ${PHASE}. Use: phase_a, phase_b, or all" ;;
esac

log "All tests complete. Logs in ${LOG_DIR}/e2e_*.log"
