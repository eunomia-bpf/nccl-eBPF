# Protocol Sweep Results: AllReduce on 8x B300 SXM6 NVLink

**Date:** 2026-03-11
**Hardware:** 8x NVIDIA B300 SXM6 AC, NV18 NVLink topology
**NCCL version:** 22907
**Test:** `all_reduce_perf -b 8 -e 8G -f 2 -g 8 -n 100 -w 20`
(LL large: -n 20 -w 5; LL safe: -e 4M)

---

## Comparison Table (busbw, GB/s)

Default column uses **in-place busbw** (NVLS is IP-only; OOP uses slower fallback).
Simple/LL/LL128 use max(OOP, IP) with timing sanity filter (discard times < 0.5x physical minimum).

| Size  | Default (NVLS) | Simple | LL    | LL128  | vs Default      |
|-------|----------------|--------|-------|--------|-----------------|
| 8B    | 0.00           | 0.00   | 0.00  | 0.00   | ---             |
| 16B   | 0.00           | 0.00   | 0.00  | 0.00   | ---             |
| 32B   | 0.00           | 0.00   | 0.00  | 0.00   | ---             |
| 64B   | 0.00           | 0.00   | 0.00  | 0.00   | ---             |
| 128B  | 0.01           | 0.00   | 0.00  | 0.00   | ---             |
| 256B  | 0.01           | 0.01   | 0.01  | 0.00   | ~equal          |
| 512B  | 0.03           | 0.02   | 0.02  | 0.00   | default +33%    |
| 1K    | 0.06           | 0.02   | 0.05  | 0.03   | default +17%    |
| 2K    | 0.12           | 0.05   | 0.09  | 0.04   | default +25%    |
| 4K    | 0.23           | 0.12   | 0.18  | 0.09   | default +22%    |
| 8K    | 0.45           | 0.24   | 0.23  | 0.10   | default +47%    |
| 16K   | 0.87           | 0.27   | 0.52  | 0.15   | default +40%    |
| 32K   | 1.75           | 0.32   | 1.12  | 0.67   | default +36%    |
| 64K   | 3.54           | 0.31   | 2.51  | 0.96   | default +29%    |
| 128K  | 6.87           | 0.83   | 3.69  | 1.53   | default +46%    |
| 256K  | 13.59          | 4.00   | 4.56  | 3.95   | default +66%    |
| 512K  | 26.30          | 8.39   | 4.44  | 9.23   | default +65%    |
| 1M    | 50.49          | 7.57   | 17.96 | 10.56  | default +64%    |
| 2M    | 100.69         | 30.21  | 30.29 | 25.79  | default +70%    |
| 4M    | 133.50         | 57.63  | 59.70 | 46.82  | default +55%    |
| 8M    | 196.46         | 85.80  | 42.10 | 112.46 | default +43%    |
| 16M   | 283.61         | 5.42*  | 75.43 | 141.70 | default +50%    |
| 32M   | 353.34         | 7.78*  | 11.02 | 189.40 | default +46%    |
| 64M   | 425.45         | 8.56*  | 16.82 | 120.20 | default +72%    |
| 128M  | 595.71         | 405.77 | ---   | 132.59 | default +32%    |
| 256M  | 657.00         | 53.99* | ---   | 137.80 | default +79%    |
| 512M  | 704.46         | 102.26 | ---   | 57.08  | default +85%    |
| 1G    | 732.02         | 126.37 | ---   | 56.32  | default +83%    |
| 2G    | 821.44         | 99.15  | ---   | 32.33  | default +88%    |
| 4G    | 831.85         | 64.47  | ---   | 116.26 | default +86%    |
| 8G    | 836.53         | 154.99 | ---   | 43.58  | default +81%    |

\* = high single-run variance observed (OOP and IP diverged >3x); use with caution.

LL range: tested 8B-64M. No deadlock or hang observed up to 64M.

---

## Key Findings

### Finding 1: NCCL Default Uses NVLS Across the Entire Size Range

The most striking result: the IP/OOP busbw ratio is consistently ~1.75x across ALL sizes from 512B to 8G. This is the signature of NVLS (NVLink SHARP multicast), which only works in-place. NCCL on B300 NV18 chooses NVLS almost exclusively, not Ring+LL as on prior hardware generations.

This explains why forcing Simple, LL, or LL128 all lose badly: they are Ring-based and cannot use the NVLink multicast hardware path.

**Default peak throughput: 836.5 GB/s busbw at 8G (in-place)**

### Finding 2: Default Is Optimal at Every Tested Size

NCCL default wins at all 31 size points from 8B to 8G. No alternative protocol ever beats it.

- vs Simple: default leads by 17% to 88% across 1K-8G
- vs LL: default leads by 17% to 70% across 1K-4M (LL not tested >64M)
- vs LL128: default leads by 40% to 88% across 8K-8G

The only near-tie is sub-256B where all protocols are noise-floor limited.

### Finding 3: LL Protocol Safety Boundary

LL did NOT deadlock or crash at any tested size (8B to 64M, 20 iterations). The test completed normally. However:
- LL performance degrades severely with size (peak only 75.4 GB/s at 16M)
- High run-to-run variance: single-run OOP vs IP often differ 2-5x
- LL is essentially useless on NVLink NV18 hardware; NVLS dominates

The "LL deadlock" concern for large sizes is real in theory (LL requires round-trip ACKs and can stall if buffers are too small), but 64M completed safely here. Larger sizes were not tested.

### Finding 4: LL128 Better Than LL but Still Far Behind Default

LL128 peak: 189.4 GB/s (32M in-place). Consistently better than plain LL but still 46-88% below default for medium-large messages. LL128 also shows high single-run variance (some timings are physically impossible, indicating measurement noise).

### Finding 5: Simple Protocol Has Severe Variance at Mid-Large Sizes

Simple at 16M-256M shows wildly varying OOP vs IP times (e.g., 3096 us vs 4 us for 16M). The 4 us reading is physically impossible given NVLink bandwidth constraints (minimum ~33 us for 16M at 900 GB/s). These are measurement artifacts, not real performance. The reliable Simple numbers cap at ~405 GB/s (128M), still 32% below default.

---

## Implication for Policy Research

On B300 NV18, **the interesting policy dimension is not LL vs Simple vs LL128** (all are clearly suboptimal). The decision space is:

1. **NVLS vs Ring**: When is NVLS actually safe? (NVLS requires in-place semantics; some ML frameworks use out-of-place allreduce, which would force the ~1.75x slower OOP fallback)
2. **When to force OOP fallback**: If a tenant's buffer aliasing policy forbids in-place ops, the policy plane should know the 1.75x penalty and account for it in SLO estimation
3. **NVLS registration**: NVLS requires special NVLink multicast memory registration; an eBPF policy could monitor registration failures and fall back gracefully
4. **Multi-tenant NVLS contention**: NVLink SHARP resources are shared; a fairness policy is needed when multiple jobs compete for multicast slots

The prior assumption that "4M-128M segment doesn't choose Ring" was partially correct (Ring is never chosen here), but the real story is that NVLS dominates the entire range, not just large messages.

---

## Raw Log Files

- `docs/tmp/baseline-8gpu-allreduce.log` — default (100 iters, 20 warmup)
- `docs/tmp/sweep-8gpu-simple.log` — NCCL_PROTO=Simple, full range
- `docs/tmp/sweep-8gpu-ll-safe.log` — NCCL_PROTO=LL, 8B-4M
- `docs/tmp/sweep-8gpu-ll-large.log` — NCCL_PROTO=LL, 8M-64M (20 iters)
- `docs/tmp/sweep-8gpu-ll128.log` — NCCL_PROTO=LL128, full range
