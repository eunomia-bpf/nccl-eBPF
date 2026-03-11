# Plan B: NVML/GPU Metric Observability Analysis

**Date**: 2026-03-11  
**Hardware**: 8x NVIDIA B300 SXM6 AC (NVLink 7, 18 links/GPU)

## Summary

Under sustained NCCL all-reduce workloads (256M-1G messages, 8 GPUs), we found
several NVML/DCGM metrics with clear, observable dynamic changes that can serve
as eBPF policy input signals.

---

## Metrics That Change During NCCL Workloads

### 1. SM Clock Frequency (SM Throttling) — HIGH VALUE

**Observable via**: `nvidia-smi dmon -s p` (pclk column), NVML
`nvmlDeviceGetClockInfo`, DCGM field 140, `nvmlDeviceGetCurrentClocksThrottleReasons`

**Findings**:
- Baseline: 2032 MHz (max) on all 8 GPUs
- Under heavy all-reduce: drops to **1650-1927 MHz** on specific GPUs (3, 5, 6, 7)
- Throttle reason bitmask: `0x20` = `SW_THERMAL_SLOWDOWN` (software thermal protection)
- GPU 3 saw drops to **1650 MHz** with power at 958W (during 1G all-reduce runs)
- GPU 5 in the sustained load test: 964-1033W power, 1912-2002 MHz clocks
- `HW_THERMAL_SLOWDOWN` (0x40) and `HW_POWER_BRAKE` (0x80) were NOT triggered
- Throttle events occur ~14 times over 30s of heavy load on the most-affected GPU

**Policy use case**: A policy that reads SM clock frequency can detect thermal stress
and reduce NCCL aggressiveness (e.g., limit message sizes, reduce channel count,
prefer algorithms with lower compute intensity like Tree over Ring for large messages).

### 2. GPU Power Draw — HIGH VALUE

**Observable via**: `nvidia-smi dmon -s p` (pwr column), NVML `nvmlDeviceGetPowerUsage`

**Findings**:
- Idle baseline: ~190-200W per GPU
- During all-reduce (256M-1G messages): **250-1054W** depending on GPU role
- GPU 3 and GPU 7 showed the most extreme variation (peaks >1000W)
- Power spikes correlate directly with computation-heavy collective phases
- Individual GPUs spike to >5x idle power during the reduce computation phase

**Policy use case**: Power as a proxy for compute intensity — a policy can read
`nvmlDeviceGetPowerUsage` and adjust algorithmic choices when approaching thermal
design power (TDP). For B300, TDP is ~1750W; the observed 1054W peaks leave ~40%
headroom but policy should react at ~80% TDP threshold.

### 3. Margin Temperature — MEDIUM-HIGH VALUE

**Observable via**: NVML `nvmlDeviceGetMarginTemperature` (newly discovered API)

**Findings**:
- Returns "margin to thermal limit" in Celsius, not raw temperature
- Baseline values: GPU 0=55C, GPU 1=50C, GPU 2=52C (margin, not temperature)
- During heavy load: margin decreases by ~15-25C
- GPU 3 margin dropped from 47C (idle) to **31C** during peak throttle events
- This is a direct, pre-processed signal — when margin approaches 0, throttling
  is imminent. Available directly via NVML API without post-processing.

**Policy use case**: The most actionable thermal signal — directly encodes
"how far from thermal limit we are." Policy rule: if `marginTemperature < threshold`,
reduce aggressiveness. This is better than raw temperature for policy because it
accounts for different GPU thermal envelopes.

### 4. NVLink Cumulative Data Counters — MEDIUM VALUE

**Observable via**: `nvidia-smi nvlink -gt d`, `nvidia-smi nvlink -gt r`

**Findings**:
- Cumulative counters are readable and grow proportionally to NCCL traffic
- 1G all-reduce with 200 iterations: ~30-41 GB transferred per GPU Link 0
- Counters reflect actual data moved (data throughput vs. raw throughput ~93% efficiency)
- 18 active NVLink 7 links per GPU confirmed
- **`nvmlDeviceGetNvLinkUtilizationCounter` returns "Not Supported"** on B300

**DCGM alternative** (field 1011=NVLTX, 1012=NVLRX):
- Returns per-500ms bandwidth samples during NCCL
- Average: ~47-49 GB per 500ms per GPU during 256M-1G all-reduce
- BUT counters are raw cumulative bytes — need differentiation for rate

**Policy use case**: Useful for *load balancing* across GPUs — a policy could
compare NVLink utilization per-GPU to detect hot links. Less useful for real-time
throttling decisions (counters need to be differentiated).

### 5. GPU/Memory Temperature — MEDIUM VALUE

**Observable via**: `nvidia-smi dmon -s t` (gtemp, mtemp columns)

**Findings**:
- GPU temp range: 33-68°C during all-reduce (varies by GPU position)
- Memory temp often 1-5°C lower than GPU temp
- Temperature changes lag behind power spikes by ~10-15 seconds (thermal inertia)
- Not fast enough for real-time policy decisions at sub-second granularity

**Policy use case**: Useful as a *longer-term* signal for sustained overheating
prediction, but power draw and clock frequency change faster and are better
real-time signals.

### 6. SM/Memory Utilization — LOW-MEDIUM VALUE

**Observable via**: `nvidia-smi dmon -s u` (sm, mem columns), DCGM fields 200/201

**Findings**:
- During all-reduce: SM utilization = 90-100% on active GPUs
- Memory utilization = 5-35% (varies with message sizes)
- During inter-node transfers or barrier waits: SM drops to 0-65%
- Highly bursty — not stable enough for fine-grained policy decisions

**Policy use case**: Can distinguish computation-heavy phases from communication-
dominated phases. If SM utilization is consistently low (<20%) but power is high,
indicates memory-bound or NVLink-bound collective — candidate for algorithm switch.

### 7. NVLink Error Counters — LOW-VALUE (Healthy Baseline)

**Observable via**: NVML `nvmlDeviceGetNvLinkErrorCounter`

**Findings**:
- All NVLink error counters (replay, recovery, CRC FLIT) = 0 throughout all tests
- NVLink 7 has 18 active links per GPU, all functioning correctly
- No degraded link detection needed for healthy hardware

**Policy use case**: Sentinel for fault resilience policy — if errors start
accumulating, a policy should route around degraded links or reduce message sizes.

### 8. NVLink BW Mode — STATIC

**Observable via**: NVML `nvmlDeviceGetNvlinkBwMode`

**Findings**:
- Consistently returns `bwMode=0 isBest=1` (full bandwidth mode)
- Does not change during NCCL workloads
- Could be modified via `nvmlSystemSetNvlinkBwMode` to throttle NVLink

**Policy use case**: Not useful as a *sensing* signal. Could be a policy *action*
— a policy could reduce NVLink bandwidth mode to throttle aggressive tenants.

---

## Metrics That Do NOT Change (or Not Readable)

| Metric | Status | Reason |
|--------|--------|--------|
| `clocks_throttle_reasons.hw_thermal_slowdown` | Never active | B300 uses SW thermal path first |
| `clocks_throttle_reasons.hw_power_brake` | Never active | No power brake events observed |
| `pviol` / `tviol` (dmon) | Always 0 | B300 doesn't use these flags |
| `pstate` | Always P0 | B300 stays in P0 even when throttled |
| `nvmlDeviceGetNvLinkUtilizationCounter` | Not Supported | B300 hardware limitation |
| PCIe TX/RX (dmon) | N/A | NVLink used for GPU-GPU traffic |

---

## Priority Ranking for eBPF Policy Inputs

1. **SM Clock Frequency** (`nvmlDeviceGetClockInfo(NVML_CLOCK_SM)`) + **Throttle Reason Bitmask** (`nvmlDeviceGetCurrentClocksThrottleReasons`)  
   - Directly indicates thermal stress event
   - Sub-second resolution
   - Policy: if pclk < 2000 MHz, reduce NCCL message sizes or switch algorithm

2. **Margin Temperature** (`nvmlDeviceGetMarginTemperature`)  
   - Pre-computed distance to thermal limit
   - Policy: if margin < 30°C, preemptively reduce aggressiveness before throttle kicks in

3. **Power Draw** (`nvmlDeviceGetPowerUsage`)  
   - Robust proxy for workload intensity, faster than temperature
   - Policy: if power > 0.8 * TDP, begin aggressiveness reduction

4. **NVLink DCGM Counters** (fields 1011/1012, 500ms resolution)  
   - Rate-of-change indicates bandwidth pressure
   - Policy: if NVLink bandwidth delta is low despite high SM utilization, indicates contention

5. **NVLink Cumulative Counters** (`nvidia-smi nvlink -gt d`)  
   - For per-GPU fairness enforcement across tenants
   - Policy: limit tenants that exceed byte-transfer quotas

---

## Integration Path: eBPF Policy via NVML

The proposed integration mechanism:

1. **NCCL profiler plugin** calls `ncclProfilerEvent` before each collective
2. The eBPF policy program (loaded via bpftime) calls `nvmlDeviceGetClockInfo`
   and `nvmlDeviceGetCurrentClocksThrottleReasons` via helper functions
3. Decision: if throttle reason `0x20` is active on any GPU in the communicator,
   the policy returns a reduced algorithm selection (e.g., force `TREE` instead
   of `RING` to reduce compute intensity)
4. The NCCL tuner plugin applies the policy decision via `ncclTuner_v3`

**Sampling overhead**: NVML API calls take ~10-50µs each. A policy checking 8 GPUs
at 500µs intervals adds ~400µs overhead per second — negligible vs. collective latency.

---

## Observations on B300-Specific Behavior

- **GPU 3 disproportionately throttles**: Consistently highest power draw and most
  frequent throttle events across all test runs. Likely due to its physical position
  in the NVLink switch topology requiring more data relaying.
- **Throttle pattern**: Rapid oscillation between 2032 MHz and 1650-1972 MHz at
  ~0.5-2s intervals — suggests the B300's firmware uses aggressive thermal recovery.
- **No P-state changes**: B300 stays in P0 (maximum performance state) even when
  thermally throttled — frequency reduction is a sub-P-state mechanism.
- **NVLink overhead in power**: B300 NVLink 7 draw is significant — the ~590W
  power on GPU 3 during all-reduce vs ~280W idle represents a large portion going
  to NVLink switching/routing overhead.

---

## Conclusion

The **SM clock frequency** combined with the **throttle reason bitmask** and
**margin temperature** form the ideal trio of policy input signals for thermal-aware
NCCL governance on B300 hardware. These metrics:

1. Are observable via standard NVML APIs (no special permissions)
2. Change dynamically during NCCL workloads with clear correlation to workload intensity
3. Can be sampled at sub-second granularity with negligible overhead
4. Directly encode the GPU's thermal headroom, making them actionable signals for policy

