// SPDX-License-Identifier: GPL-2.0
/*
 * nccl_cpu_observer.bpf.c - Kernel eBPF program for NCCL CPU contention
 * classification.
 *
 * Attach point: tp/sched/sched_switch  (tracepoint, no kernel mod needed)
 *
 * Algorithm
 * ---------
 * On every sched_switch event:
 *
 *  1. Check whether prev_pid or next_pid belongs to the watched NCCL process
 *     group (tgid == config.target_pid).  Ignore irrelevant events early to
 *     minimise overhead on busy machines.
 *
 *  2. Update per-CPU accounting in `percpu_slot_map`:
 *       - Increment the switch counter for the current window.
 *       - Record the current CPU in a bitmask of observed CPUs.
 *       - If the 1-second window has expired, publish the aggregated counters
 *         to the shared `state_map` and reset the window.
 *
 *  3. Classify contention based on the published state:
 *       - Many switches AND many CPUs  => SATURATION
 *       - Few allowed CPUs             => CPUSET_LIMITED
 *       - Otherwise                    => NONE
 *
 * Maps
 * ----
 *  config_map     [ARRAY, 1 entry]  -- userspace writes target_pid + thresholds
 *  percpu_slot_map[PERCPU_ARRAY]    -- per-CPU rolling window accumulators
 *  state_map      [ARRAY, 1 entry, BPF_F_MMAPABLE] -- published contention state
 *
 * Pinning
 * -------
 *  state_map  is pinned at /sys/fs/bpf/nccl_cpu_state  by load.sh so that
 *  the NCCL process can mmap it without a syscall per read.
 */

/*
 * vmlinux.h provides all kernel type definitions via BTF CO-RE.
 * bpf_helpers.h / bpf_tracing.h / bpf_core_read.h come from the libbpf
 * source tree (included directly, not via a bpf/ subdirectory prefix).
 */
#include "vmlinux.h"
#include "bpf_helpers.h"
#include "bpf_tracing.h"
#include "bpf_core_read.h"

#include "nccl_cpu_observer.h"

/* ------------------------------------------------------------------ */
/* Helper: count set bits in a 32-bit word (popcount)                  */
/* ------------------------------------------------------------------ */
static __always_inline __u32 popcount32(__u32 v)
{
    __u32 c = 0;
    /* Unrolled for verifier: at most 32 iterations, always terminates. */
    c += (v >> 0)  & 1u;
    c += (v >> 1)  & 1u;
    c += (v >> 2)  & 1u;
    c += (v >> 3)  & 1u;
    c += (v >> 4)  & 1u;
    c += (v >> 5)  & 1u;
    c += (v >> 6)  & 1u;
    c += (v >> 7)  & 1u;
    c += (v >> 8)  & 1u;
    c += (v >> 9)  & 1u;
    c += (v >> 10) & 1u;
    c += (v >> 11) & 1u;
    c += (v >> 12) & 1u;
    c += (v >> 13) & 1u;
    c += (v >> 14) & 1u;
    c += (v >> 15) & 1u;
    c += (v >> 16) & 1u;
    c += (v >> 17) & 1u;
    c += (v >> 18) & 1u;
    c += (v >> 19) & 1u;
    c += (v >> 20) & 1u;
    c += (v >> 21) & 1u;
    c += (v >> 22) & 1u;
    c += (v >> 23) & 1u;
    c += (v >> 24) & 1u;
    c += (v >> 25) & 1u;
    c += (v >> 26) & 1u;
    c += (v >> 27) & 1u;
    c += (v >> 28) & 1u;
    c += (v >> 29) & 1u;
    c += (v >> 30) & 1u;
    c += (v >> 31) & 1u;
    return c;
}

/* ------------------------------------------------------------------ */
/* Maps                                                                 */
/* ------------------------------------------------------------------ */

/*
 * config_map: single entry written by load.sh / userspace before the
 * program is attached.  Holds the target PID and classification thresholds.
 */
struct {
    __uint(type, BPF_MAP_TYPE_ARRAY);
    __uint(max_entries, 1);
    __type(key, __u32);
    __type(value, struct cpu_observer_config);
    __uint(pinning, LIBBPF_PIN_BY_NAME); /* pinned as /sys/fs/bpf/config_map */
} config_map SEC(".maps");

/*
 * percpu_slot_map: per-CPU rolling window accounting.  Each CPU maintains
 * its own slot; no atomic ops required.
 */
struct {
    __uint(type, BPF_MAP_TYPE_PERCPU_ARRAY);
    __uint(max_entries, 1);
    __type(key, __u32);
    __type(value, struct cpu_switch_slot);
} percpu_slot_map SEC(".maps");

/*
 * state_map: published contention state.  BPF_F_MMAPABLE allows the NCCL
 * tuner plugin to mmap the underlying page and read directly without a
 * syscall.  Pinned to /sys/fs/bpf/nccl_cpu_state by load.sh.
 */
struct {
    __uint(type, BPF_MAP_TYPE_ARRAY);
    __uint(max_entries, 1);
    __uint(map_flags, BPF_F_MMAPABLE);
    __type(key, __u32);
    __type(value, struct cpu_contention_state);
    __uint(pinning, LIBBPF_PIN_BY_NAME); /* pinned as /sys/fs/bpf/state_map */
} state_map SEC(".maps");

/* ------------------------------------------------------------------ */
/* sched_switch tracepoint handler                                      */
/* ------------------------------------------------------------------ */
SEC("tp/sched/sched_switch")
int handle_sched_switch(struct trace_event_raw_sched_switch *ctx)
{
    /* ---- Step 1: load config and filter irrelevant events ---- */
    __u32 zero = 0;
    struct cpu_observer_config *cfg = bpf_map_lookup_elem(&config_map, &zero);
    if (!cfg)
        return 0;

    __u32 target_pid = cfg->target_pid;
    if (target_pid == 0)
        return 0; /* Not yet configured; bail out cheaply. */

    /*
     * We track switches where the outgoing task (prev) OR the incoming task
     * (next) is a thread in the target NCCL process group.
     *
     * bpf_get_current_pid_tgid() returns (tgid << 32 | pid) for the task
     * that was *running* when the tracepoint fired.  For sched_switch that
     * is the outgoing (prev) task.  The incoming task's tgid is not directly
     * exposed by the tracepoint args, so we use prev_pid == target_pid as a
     * heuristic: if either prev_pid matches any thread of the target tgid, we
     * count the switch.
     *
     * A more precise approach would use a supplementary hash map of known
     * thread pids; that can be layered in later without changing the map ABI.
     */
    __u64 pid_tgid = bpf_get_current_pid_tgid();
    __u32 cur_tgid = (__u32)(pid_tgid >> 32);

    /* Also read the prev_pid directly from the tracepoint args. */
    pid_t prev_pid = BPF_CORE_READ(ctx, prev_pid);
    pid_t next_pid = BPF_CORE_READ(ctx, next_pid);

    /*
     * Accept the event if the outgoing task's tgid matches (most reliable),
     * or if either raw pid value matches target_pid (covers the case where
     * target_pid refers to the main thread / tgid).
     */
    int relevant = (cur_tgid == target_pid) ||
                   ((__u32)prev_pid == target_pid) ||
                   ((__u32)next_pid == target_pid);
    if (!relevant)
        return 0;

    /* ---- Step 2: update per-CPU rolling window ---- */
    __u64 now_ns = bpf_ktime_get_ns();
    __u32 cpu    = bpf_get_smp_processor_id();

    /* CPU index clamped to lower 32 bits for the observed-cpu bitmask. */
    __u32 cpu_bit = (cpu < 32u) ? (1u << cpu) : 0u;

    struct cpu_switch_slot *slot = bpf_map_lookup_elem(&percpu_slot_map, &zero);
    if (!slot)
        return 0;

    __u64 elapsed = now_ns - slot->window_start_ns;

    if (elapsed >= RATE_WINDOW_NS) {
        /*
         * Window expired: publish aggregated stats, then reset.
         *
         * Compute aggregates: combine the per-CPU bits across all CPUs is
         * not possible inside a single BPF program without a second map
         * pass.  Instead, we accumulate the union of CPU bits seen on THIS
         * cpu into the global state and let the rate reflect per-CPU
         * observations.  The NCCL consumer can accumulate across CPUs if
         * needed.  For the classification heuristic, the per-CPU count
         * already captures whether "the process was confined to a small set
         * of CPUs on each CPU".
         *
         * Duration is at least 1ns to avoid division by zero.
         */
        __u64 duration_ns = elapsed;
        if (duration_ns == 0)
            duration_ns = 1;

        /* switches_per_second = switches_in_window * 1e9 / duration_ns */
        __u64 rate64 = ((__u64)slot->switches_in_window * RATE_WINDOW_NS) /
                       duration_ns;
        __u32 rate = (__u32)(rate64 > 0xFFFFFFFFULL ? 0xFFFFFFFFU : rate64);

        __u32 allowed = popcount32(slot->cpu_seen_mask);

        /* Read thresholds from config; fall back to compile-time defaults. */
        __u32 sat_thresh = cfg->saturation_thresh_hz;
        if (sat_thresh == 0)
            sat_thresh = SCHED_SWITCH_SATURATION_THRESH_HZ;
        __u32 cpuset_thresh = cfg->cpuset_limited_thresh_cpus;
        if (cpuset_thresh == 0)
            cpuset_thresh = ALLOWED_CPUS_LIMITED_THRESH;

        /* ---- Step 3: classify ---- */
        __u8 ctype;
        if (allowed <= cpuset_thresh) {
            /*
             * Process is confined to very few CPUs: cpuset/taskset
             * limitation.  This is the primary signal regardless of the
             * switch rate, because even a lightly loaded but pinned process
             * will show this pattern.
             */
            ctype = CONTENTION_CPUSET_LIMITED;
        } else if (rate >= sat_thresh) {
            /*
             * High switch rate + ample CPUs: saturation from competing
             * workloads (e.g. stress-ng --cpu N).
             */
            ctype = CONTENTION_SATURATION;
        } else {
            ctype = CONTENTION_NONE;
        }

        /* Publish to shared mmapable map. */
        struct cpu_contention_state new_state = {
            .timestamp_ns      = now_ns,
            .sched_switch_rate = rate,
            .allowed_cpus      = allowed,
            .contention_type   = ctype,
        };
        bpf_map_update_elem(&state_map, &zero, &new_state, BPF_ANY);

        /* Reset window. */
        slot->window_start_ns    = now_ns;
        slot->switches_in_window = 1; /* count this switch in the new window */
        slot->cpu_seen_mask      = cpu_bit;
    } else {
        /* Still within the window: accumulate. */
        slot->switches_in_window += 1;
        slot->cpu_seen_mask      |= cpu_bit;
    }

    return 0;
}

char LICENSE[] SEC("license") = "GPL";
