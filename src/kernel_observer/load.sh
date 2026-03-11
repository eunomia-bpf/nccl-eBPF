#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-2.0
#
# load.sh -- Load the nccl_cpu_observer kernel eBPF program.
#
# Usage:
#   sudo ./load.sh <nccl_pid>
#
# The script:
#   1. Verifies prerequisites (root, bpftool, bpffs mounted).
#   2. Builds the .bpf.o if not already present.
#   3. Writes the target PID and thresholds into the config_map.
#   4. Loads the program and pins state_map to /sys/fs/bpf/nccl_cpu_state.
#   5. Attaches the program to the tp/sched/sched_switch tracepoint.
#
# To detach:
#   sudo bpftool link detach id <link_id_printed_by_this_script>
#   sudo rm -f /sys/fs/bpf/nccl_cpu_state /sys/fs/bpf/nccl_cpu_config
#              /sys/fs/bpf/config_map /sys/fs/bpf/state_map

set -euo pipefail

# --------------------------------------------------------------------------- #
# Constants / defaults
# --------------------------------------------------------------------------- #
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
BPF_OBJ="${SCRIPT_DIR}/nccl_cpu_observer.bpf.o"
BPF_FS="/sys/fs/bpf"
STATE_PIN="${BPF_FS}/nccl_cpu_state"
CONFIG_PIN="${BPF_FS}/nccl_cpu_config"

# Classification thresholds (can be overridden with env vars).
# SATURATION_THRESH_HZ: switches/s above which we call it CPU saturation.
SATURATION_THRESH_HZ="${SATURATION_THRESH_HZ:-2000}"
# CPUSET_LIMITED_THRESH: allowed-CPU count at or below which we call it cpuset.
CPUSET_LIMITED_THRESH="${CPUSET_LIMITED_THRESH:-4}"

# --------------------------------------------------------------------------- #
# Argument parsing
# --------------------------------------------------------------------------- #
usage() {
    echo "Usage: sudo $0 <nccl_pid> [saturation_thresh_hz] [cpuset_limited_thresh_cpus]"
    echo ""
    echo "  nccl_pid                 PID of the NCCL process to monitor"
    echo "  saturation_thresh_hz     Optional: switches/s threshold (default: ${SATURATION_THRESH_HZ})"
    echo "  cpuset_limited_thresh    Optional: allowed-CPU threshold (default: ${CPUSET_LIMITED_THRESH})"
    exit 1
}

if [[ $# -lt 1 ]]; then
    usage
fi

NCCL_PID="$1"
[[ $# -ge 2 ]] && SATURATION_THRESH_HZ="$2"
[[ $# -ge 3 ]] && CPUSET_LIMITED_THRESH="$3"

# --------------------------------------------------------------------------- #
# Privilege check
# --------------------------------------------------------------------------- #
if [[ "$(id -u)" -ne 0 ]]; then
    echo "[ERROR] This script must be run as root (kernel eBPF requires CAP_BPF / CAP_SYS_ADMIN)."
    exit 1
fi

# --------------------------------------------------------------------------- #
# Dependency checks
# --------------------------------------------------------------------------- #
for dep in bpftool clang make; do
    if ! command -v "${dep}" &>/dev/null; then
        echo "[ERROR] Required tool not found in PATH: ${dep}"
        exit 1
    fi
done

# --------------------------------------------------------------------------- #
# BPF filesystem
# --------------------------------------------------------------------------- #
if ! mount | grep -q "type bpf"; then
    echo "[INFO] Mounting BPF filesystem at ${BPF_FS} ..."
    mount -t bpf bpf "${BPF_FS}"
fi

# --------------------------------------------------------------------------- #
# Build if necessary
# --------------------------------------------------------------------------- #
if [[ ! -f "${BPF_OBJ}" ]]; then
    echo "[INFO] Building ${BPF_OBJ} ..."
    make -C "${SCRIPT_DIR}" all
fi

echo "[INFO] Using object: ${BPF_OBJ}"
echo "[INFO] Target PID  : ${NCCL_PID}"
echo "[INFO] Thresholds  : saturation=${SATURATION_THRESH_HZ} Hz, cpuset<=${CPUSET_LIMITED_THRESH} CPUs"

# --------------------------------------------------------------------------- #
# Load BPF object (program + maps)
# --------------------------------------------------------------------------- #
# bpftool prog load creates all maps defined in the object.  The .maps section
# uses LIBBPF_PIN_BY_NAME so bpftool will pin them under BPF_FS by their
# section name.
echo "[INFO] Loading BPF program ..."
bpftool prog load "${BPF_OBJ}" "${BPF_FS}/nccl_cpu_observer_prog" \
    pinmaps "${BPF_FS}"

# Verify pins created by bpftool
for MAP_PIN in "${BPF_FS}/config_map" "${BPF_FS}/state_map" "${BPF_FS}/percpu_slot_map"; do
    if [[ ! -e "${MAP_PIN}" ]]; then
        echo "[WARN] Expected pin not found: ${MAP_PIN}  (may depend on bpftool version)"
    fi
done

# --------------------------------------------------------------------------- #
# Populate config map with target PID and thresholds
# --------------------------------------------------------------------------- #
# The cpu_observer_config struct layout (all __u32, 4 bytes each):
#   offset 0: target_pid
#   offset 4: saturation_thresh_hz
#   offset 8: cpuset_limited_thresh_cpus
#   offset 12: _pad
#
# bpftool map update with hex values (little-endian 4-byte words).
pid_hex=$(printf '%08x' "${NCCL_PID}" | fold -w2 | tac | tr -d '\n')
sat_hex=$(printf '%08x' "${SATURATION_THRESH_HZ}" | fold -w2 | tac | tr -d '\n')
cpuset_hex=$(printf '%08x' "${CPUSET_LIMITED_THRESH}" | fold -w2 | tac | tr -d '\n')
pad_hex="00000000"

echo "[INFO] Writing config: pid=0x${pid_hex} sat=0x${sat_hex} cpuset=0x${cpuset_hex}"

# Key = 0 (array index), value = 16-byte struct
bpftool map update pinned "${BPF_FS}/config_map" \
    key  hex 00 00 00 00 \
    value hex ${pid_hex} ${sat_hex} ${cpuset_hex} ${pad_hex}

# Create legacy symlinks for human-readable names.
[[ -e "${CONFIG_PIN}" ]] || ln -sf "${BPF_FS}/config_map" "${CONFIG_PIN}" 2>/dev/null || true
[[ -e "${STATE_PIN}"  ]] || ln -sf "${BPF_FS}/state_map"  "${STATE_PIN}"  2>/dev/null || true

# --------------------------------------------------------------------------- #
# Attach the program to the tracepoint
# --------------------------------------------------------------------------- #
PROG_ID=$(bpftool prog show pinned "${BPF_FS}/nccl_cpu_observer_prog" \
              2>/dev/null | awk '/^[0-9]+:/{print $1}' | tr -d ':')

if [[ -z "${PROG_ID}" ]]; then
    # Fallback: look up by name
    PROG_ID=$(bpftool prog list 2>/dev/null \
                  | grep "handle_sched_switch" \
                  | awk '{print $1}' | tr -d ':' | head -1)
fi

if [[ -z "${PROG_ID}" ]]; then
    echo "[ERROR] Could not find loaded program ID.  Check: bpftool prog list"
    exit 1
fi

echo "[INFO] Attaching prog id=${PROG_ID} to tp/sched/sched_switch ..."
LINK_ID=$(bpftool link attach tracepoint sched sched_switch \
              prog id "${PROG_ID}" 2>&1 | tee /dev/stderr | awk '/link id/{print $NF}')

echo ""
echo "[OK] nccl_cpu_observer loaded and attached."
echo "     Link ID    : ${LINK_ID:-<see above>}"
echo "     State map  : ${STATE_PIN} (or ${BPF_FS}/state_map)"
echo "     Config map : ${CONFIG_PIN} (or ${BPF_FS}/config_map)"
echo ""
echo "To read the current contention state:"
echo "  bpftool map dump pinned ${BPF_FS}/state_map"
echo ""
echo "To detach and unload:"
echo "  bpftool link detach id <link_id>"
echo "  rm -f ${BPF_FS}/nccl_cpu_observer_prog ${BPF_FS}/config_map \\"
echo "        ${BPF_FS}/state_map ${BPF_FS}/percpu_slot_map \\"
echo "        ${STATE_PIN} ${CONFIG_PIN}"
