/* SPDX-License-Identifier: GPL-2.0 */
/*
 * nccl_cpu_observer.h - Shared data structures for CPU contention observer.
 *
 * Used by both the kernel eBPF program (nccl_cpu_observer.bpf.c) and
 * userspace policy consumers.  The kernel side includes this header via
 * the BPF build chain; userspace includes it with a normal C compiler.
 *
 * Design notes
 * ------------
 * Two distinct CPU-contention modes have been observed to affect NCCL
 * algorithm performance differently:
 *
 *   CONTENTION_SATURATION   -- Many runnable threads competing on a large
 *                              pool of CPUs.  Observed as a high sched_switch
 *                              rate (frequent preemption).  Degrades Ring
 *                              allreduce (~33 %) while NVLS remains immune.
 *
 *   CONTENTION_CPUSET_LIMITED -- NCCL threads pinned to a small CPU set via
 *                               cpuset/taskset.  Observed as a low allowed-CPU
 *                               count for the monitored PID.  Degrades NVLS
 *                               allreduce (~78 %) while Ring remains immune.
 *
 * The eBPF program writes a cpu_contention_state record into a
 * BPF_F_MMAPABLE array map so that the NCCL tuner policy can read it via
 * mmap with zero syscall overhead.
 */

#ifndef NCCL_CPU_OBSERVER_H_
#define NCCL_CPU_OBSERVER_H_

/*
 * Use kernel __u* types when compiled as a BPF program (vmlinux.h already
 * defines them), or stdint types when compiled as normal C (userspace
 * consumers, unit tests, the NCCL plugin loader, etc.).
 *
 * The BPF build passes -D__BPF_TRACING__ on the command line (see Makefile),
 * so we rely on that flag rather than a re-definition here.
 */
#ifdef __BPF_TRACING__
typedef __u64  obs_u64;
typedef __u32  obs_u32;
typedef __u8   obs_u8;
#else
#include <stdint.h>
typedef uint64_t obs_u64;
typedef uint32_t obs_u32;
typedef uint8_t  obs_u8;
#endif

/* ---------------------------------------------------------------------- */
/* Map index 0 -- the single shared state record.                          */
/* ---------------------------------------------------------------------- */
#define CPU_STATE_MAP_IDX  0u

/* ---------------------------------------------------------------------- */
/* Contention type classification                                           */
/* ---------------------------------------------------------------------- */
#define CONTENTION_NONE           0u   /* Low switch rate, ample CPUs      */
#define CONTENTION_SATURATION     1u   /* High switch rate, many CPUs      */
#define CONTENTION_CPUSET_LIMITED 2u   /* Few allowed CPUs for this PID    */

/* ---------------------------------------------------------------------- */
/* Thresholds (tunable at load time via the config map)                    */
/* ---------------------------------------------------------------------- */

/*
 * If the rolling sched_switch rate (switches/s for the watched PID group)
 * exceeds this value, contention is classified as SATURATION.
 */
#define SCHED_SWITCH_SATURATION_THRESH_HZ  2000u

/*
 * If the number of CPUs observed to have run the watched PID group within
 * the observation window drops to or below this value, contention is
 * classified as CPUSET_LIMITED.
 */
#define ALLOWED_CPUS_LIMITED_THRESH        4u

/* Window length for rate computation: 1 second in nanoseconds. */
#define RATE_WINDOW_NS  1000000000ULL

/* ---------------------------------------------------------------------- */
/* Per-CPU raw accounting (kept in a BPF_MAP_TYPE_PERCPU_ARRAY)            */
/* ---------------------------------------------------------------------- */
struct cpu_switch_slot {
    obs_u64 window_start_ns;    /* Timestamp of the current window start    */
    obs_u32 switches_in_window; /* Count of sched_switches in this window   */
    obs_u32 cpu_seen_mask;      /* Bitmask of CPUs seen (lower 32 CPUs)     */
};

/* ---------------------------------------------------------------------- */
/* Shared contention state (index 0 of the mmapable state_map)             */
/* ---------------------------------------------------------------------- */
struct cpu_contention_state {
    obs_u64 timestamp_ns;      /* When this record was last updated (CLOCK_MONOTONIC) */
    obs_u32 sched_switch_rate; /* Estimated switches/s for the watched PID group       */
    obs_u32 allowed_cpus;      /* Number of distinct CPUs observed to run this group   */
    obs_u8  contention_type;   /* CONTENTION_* constant above                          */
    obs_u8  _pad[7];           /* Explicit padding for struct alignment                */
};

/* ---------------------------------------------------------------------- */
/* Config map (index 0) -- userspace writes thresholds before attaching    */
/* ---------------------------------------------------------------------- */
struct cpu_observer_config {
    obs_u32 target_pid;                  /* NCCL process PID to monitor     */
    obs_u32 saturation_thresh_hz;        /* Override SCHED_SWITCH_SATURATION_THRESH_HZ */
    obs_u32 cpuset_limited_thresh_cpus;  /* Override ALLOWED_CPUS_LIMITED_THRESH       */
    obs_u32 _pad;
};

/* ---------------------------------------------------------------------- */
/* BPF filesystem pin path for the state map                               */
/* ---------------------------------------------------------------------- */
#define CPU_STATE_PIN_PATH  "/sys/fs/bpf/nccl_cpu_state"
#define CPU_CONFIG_PIN_PATH "/sys/fs/bpf/nccl_cpu_config"

#endif /* NCCL_CPU_OBSERVER_H_ */
