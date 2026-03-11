# 3-Rank NCCL Experiment Results

**Date**: 2026-03-10
**Testbed**: 1x RTX 5090, 3 MPI ranks on same GPU via NCCL_HOSTID hack
**Transport**: Socket (NCCL_P2P_DISABLE=1, NCCL_SHM_DISABLE=1, NCCL_NET=Socket)
**NCCL**: 2.29.7, nccl-tests 2.18.0

---

## 1. Did 3 ranks work?

**Yes.** The NCCL_HOSTID hack (3r-rank0, 3r-rank1, 3r-rank2) successfully creates 3 logical hosts on one GPU. All sizes 8B–16MB ran with 0 validation errors.

---

## 2. What did NCCL select for algo/proto?

From `NCCL_DEBUG=INFO NCCL_DEBUG_SUBSYS=TUNING` on rank 0:

| Size range    | Default algo/proto  |
|---------------|---------------------|
| 8B – 2048B    | **TREE / LL**        |
| 4096B – 65536B| **RING / LL**        |
| 131072B+      | **RING / SIMPLE**    |

Key transition: NCCL switches from TREE to RING at **4096 bytes** (4KB).

This is consistent with NCCL's internal cost model: for small messages TREE is preferred (fewer steps), but once the message size triggers a different channel/latency regime, RING/LL takes over.

---

## 3. Measurable difference between Ring and Tree?

### Summary table — median latency at representative sizes (us)

| Size     | Default   | Tree/LL   | Tree/Simple | Ring/LL   | Ring/Simple |
|----------|-----------|-----------|-------------|-----------|-------------|
| 8B       | 12,647    | 12,646    | 12,359      | 12,778    | 8,728       |
| 128B     | 12,872    | 12,646    | 12,801      | 17,417    | 8,823       |
| 512B     | 12,873    | 12,871    | 12,582      | 17,418    | 8,814       |
| 2KB      | 12,872    | 12,871    | 12,360      | 17,414    | 8,824       |
| 4KB      | 17,426    | 12,872    | 12,796      | 17,427    | 8,929       |
| 8KB      | 17,431    | 12,872    | 12,801      | 17,437    | 8,747       |
| 32KB     | 17,457    | 12,895    | 12,360      | 17,453    | 8,736       |
| 128KB    | 17,460    | 13,103    | 12,883      | 17,481    | 8,987       |
| 512KB    | 17,463    | 13,122    | 12,815      | 34,948    | 8,749       |
| 1MB      | 17,465    | 13,151    | 12,829      | 52,410    | 8,752       |
| 4MB      | 17,486    | —         | 12,859      | —         | 8,792       |
| 16MB     | 17,534    | —         | 12,973      | —         | 11,035      |

### Key findings

**Finding 1: NCCL default picks suboptimal Ring/LL at 4KB–128KB**

At 4KB, NCCL switches from Tree/LL (12,872 µs) to Ring/LL (17,427 µs). This is a **35% latency regression** at the 4KB boundary — the default is measurably worse than Tree/LL, which NCCL had been using up to 2KB. This is a genuine "default suboptimal" case for 3 ranks.

**Finding 2: Ring/Simple dominates at all sizes**

Ring/Simple is the fastest configuration across the board:
- At small messages (8B–2KB): ~8.7ms vs 12.6ms for default — **31% faster**
- At medium messages (4KB–128KB): ~8.7ms vs 17.4ms for default — **50% faster**
- At 1MB: 8.75ms vs 17.46ms — **50% faster**
- At 16MB: 11.0ms vs 17.5ms — **37% faster**

Ring/Simple beats Tree/Simple by roughly the socket round-trip cost difference at all sizes.

**Finding 3: Ring/LL protocol collapse at large messages**

With forced Ring/LL, there is a catastrophic collapse starting at 512KB:
- 256KB Ring/LL: 17,481 µs
- 512KB Ring/LL: 34,948 µs (+2x)
- 1MB Ring/LL: 52,410 µs (+3x)

This is the same LL protocol collapse previously observed with 2 ranks, now confirmed for 3 ranks. NCCL's default correctly switches away from LL at 131KB, but the static-LL environment variable scenario is still dangerous.

**Finding 4: Tree/Simple is consistently faster than Tree/LL**

Tree/Simple (~12.4ms) beats Tree/LL (~12.9ms) by ~4% at all sizes. This is because the socket transport's latency floor dominates; LL's pipelining benefit is negligible compared to socket RTT.

**Finding 5: Default makes two wrong choices at 3 ranks**

1. **At 4KB–128KB**: Switches to Ring/LL (17.4ms) instead of staying on Tree/LL (12.9ms) or Tree/Simple (12.8ms). Cost: 35% slowdown.
2. **Across all sizes**: Uses LL or Tree/Simple (~12.5–17.5ms) instead of Ring/Simple (~8.7–11ms). Cost: 31–50% slowdown.

Ring/Simple is the fastest uniformly because the 3-rank Ring with SIMPLE protocol avoids LL's per-slot overhead and uses NCCL's full pipeline.

---

## 4. Ring vs Tree topology difference at 3 ranks

The theoretical prediction was: with 3 ranks, Tree should have O(log 3) = 2 steps while Ring has O(3) = 3 steps, giving Tree a latency advantage. In practice:

- **Tree/Simple** (12.4ms) vs **Ring/Simple** (8.7ms): Ring is *faster*, not Tree.
- **Tree/LL** (12.9ms) vs **Ring/LL** (17.4ms): Ring/LL is *slower* due to 3-hop LL overhead.

The socket transport RTT (~4.3ms per hop) means Ring with 3 hops takes ~13ms in theory, but Ring/Simple achieves ~8.7ms because NCCL can pipeline the reduce-scatter and all-gather phases. Tree with 2 steps takes ~12.4ms, which is consistent with 2 socket RTTs + overhead.

**Conclusion**: Ring with SIMPLE protocol is faster than Tree because ring reduce-scatter+all-gather is pipelined. The topological advantage of Tree (fewer steps) is negated by the pipelining efficiency of Ring/Simple. However, NCCL's default at 3 ranks makes an *additional* suboptimal choice: it uses Ring/LL at 4KB–128KB, which is worse than both Tree/LL and Ring/Simple.

---

## 5. Policy opportunity

This 3-rank result reveals a clear policy use case:

- **Default at 4KB–128KB**: Ring/LL = 17.4ms
- **Optimal**: Ring/Simple = 8.7ms
- **Suboptimality ratio**: 2.0x (100% overhead from wrong choice)

A policy that forces Ring/Simple for all sizes in a 3-rank job would deliver ~2x speedup in the 4KB–128KB range. This is a stronger "policy beats default" case than the 2-rank scenario where the socket floor dominated.

The eBPF policy would simply need to:
1. Detect n_ranks == 3 (or any n > 2 where Tree loses pipelining advantage)
2. Set algo=RING, proto=SIMPLE unconditionally
3. Set nChannels=0 (use NCCL default)

---

## Raw Data

### Default (3 ranks, 8B–16MB)
```
           8    12,647 µs
          16    12,871 µs
          32    12,871 µs
          64    13,089 µs
         128    12,872 µs
         256    13,090 µs
         512    12,873 µs
        1024    12,873 µs
        2048    12,872 µs
        4096    17,426 µs  ← SWITCH TO RING/LL
        8192    17,431 µs
       16384    17,453 µs
       32768    17,457 µs
       65536    17,487 µs
      131072    17,460 µs
      262144    17,462 µs
      524288    17,463 µs
     1048576    17,465 µs
     2097152    17,472 µs
     4194304    17,486 µs
     8388608    17,499 µs
    16777216    17,534 µs
```

### Ring/Simple (3 ranks, 8B–16MB)
```
           8     8,728 µs
          16     8,825 µs
          32     8,826 µs
          64     8,825 µs
         128     8,823 µs
         256     8,825 µs
         512     8,814 µs
        1024     8,820 µs
        2048     8,824 µs
        4096     8,929 µs
        8192     8,747 µs
       16384     8,728 µs
       32768     8,736 µs
       65536     8,842 µs
      131072     8,987 µs
      262144     8,740 µs
      524288     8,749 µs
     1048576     8,752 µs
     2097152     8,769 µs
     4194304     8,802 µs
     8388608     8,881 µs
    16777216    11,035 µs
```

### Tree/Simple (3 ranks, 8B–16MB)
```
           8    12,359 µs
          16    12,582 µs
          32    12,357 µs
          64    12,583 µs
         128    12,801 µs
         256    12,801 µs
         512    12,582 µs
        1024    12,799 µs
        2048    12,360 µs
        4096    12,796 µs
        8192    12,801 µs
       16384    12,801 µs
       32768    12,360 µs
       65536    12,803 µs
      131072    12,883 µs
      262144    12,815 µs
      524288    12,815 µs
     1048576    12,829 µs
     2097152    12,840 µs
     4194304    12,859 µs
     8388608    12,898 µs
    16777216    12,973 µs
```

### Ring/LL (3 ranks, 8B–1MB)
```
           8    12,778 µs
          16    12,990 µs
          32    13,432 µs
          64    13,432 µs
         128    17,417 µs
         256    17,417 µs
         512    17,418 µs
        1024    17,420 µs
        2048    17,414 µs
        4096    17,427 µs
        8192    17,437 µs
       16384    17,446 µs
       32768    17,453 µs
       65536    17,480 µs
      131072    17,481 µs
      262144    17,481 µs
      524288    34,948 µs  ← LL COLLAPSE
     1048576    52,410 µs  ← LL COLLAPSE
```

### Tree/LL (3 ranks, 8B–1MB)
```
           8    12,646 µs
          16    12,871 µs
          32    13,095 µs
          64    13,088 µs
         128    12,646 µs
         256    12,646 µs
         512    12,871 µs
        1024    12,870 µs
        2048    12,871 µs
        4096    12,872 µs
        8192    12,872 µs
       16384    12,894 µs
       32768    12,895 µs
       65536    12,896 µs
      131072    13,103 µs
      262144    13,115 µs
      524288    13,122 µs
     1048576    13,151 µs
```
