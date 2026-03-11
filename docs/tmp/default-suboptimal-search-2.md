# DEFAULT Tuner Suboptimality Search 2

## Environment
- Date: 2026-03-10
- Hardware: 1x NVIDIA GeForce RTX 5090, 2 MPI ranks sharing GPU 0
- NCCL lib: `/home/yunwei37/workspace/nccl-eBPF/nccl/build/lib`
- nccl-tests: `/home/yunwei37/workspace/nccl-eBPF/nccl-tests/build/`
- Raw logs: `/home/yunwei37/workspace/nccl-eBPF/docs/tmp/default-suboptimal-search-2-raw/`
- Experiment D note: `reduce_perf_mpi` and `gather_perf_mpi` were built locally in `nccl-tests/build/` because the MPI variants were not present in the starting build.

## Method
- Reported metric is out-of-place `algbw` from `nccl-tests`.
- `mean+-std` uses the sample standard deviation over repetitions. Single-rep rows are shown as `+-n/a`.
- `% vs DEFAULT` is `(alternative_mean - default_mean) / default_mean`.
- `>2sigma` is `yes` only when `alternative_mean - default_mean > 2 * sqrt(std_default^2 + std_alt^2)`. Single-rep comparisons are `n/a`.
- Tiny-message rows should be treated cautiously because `nccl-tests` prints `algbw` with only two decimal places, so sub-GB/s values are heavily quantized.

## Overall Verdict
- No medium-size or large-size case showed a reproducible, statistically clear win over NCCL `DEFAULT` on this setup.
- The prior single-rep ReduceScatter claim at `1 MB` did not reproduce: `DEFAULT` was `0.397+-0.006 GB/s` and `NCCL_PROTO=Simple` was `0.387+-0.006 GB/s` (`-2.5%`).
- The only `>2sigma` alternative wins were tiny-size effects:
  - Experiment C AllReduce P2P: Simple at `64 KB`: DEFAULT `0.010+-0.000` vs alternative `0.020+-0.000` (+100.0%). These are small-message effects, not the substantial default miss we were searching for.
  - Experiment D Gather: Simple at `16 KB`: DEFAULT `0.267+-0.006` vs alternative `0.280+-0.000` (+5.0%). These are small-message effects, not the substantial default miss we were searching for.

## Experiment A: ReduceScatter Validation (3 reps)
The earlier single-rep `1 MB` `Simple` advantage did not reproduce. No size in this sweep produced a meaningful, statistically clear `Simple` win over `DEFAULT`.

| Size | DEFAULT (GB/s) | Simple (GB/s) | % vs DEFAULT | >2sigma |
| --- | --- | --- | --- | --- |
| 8 B | 0.000+-0.000 | 0.000+-0.000 | n/a | no |
| 16 B | 0.000+-0.000 | 0.000+-0.000 | n/a | no |
| 32 B | 0.000+-0.000 | 0.000+-0.000 | n/a | no |
| 64 B | 0.000+-0.000 | 0.000+-0.000 | n/a | no |
| 128 B | 0.000+-0.000 | 0.000+-0.000 | n/a | no |
| 256 B | 0.000+-0.000 | 0.000+-0.000 | n/a | no |
| 512 B | 0.000+-0.000 | 0.000+-0.000 | n/a | no |
| 1 KB | 0.000+-0.000 | 0.000+-0.000 | n/a | no |
| 2 KB | 0.000+-0.000 | 0.000+-0.000 | n/a | no |
| 4 KB | 0.000+-0.000 | 0.000+-0.000 | n/a | no |
| 8 KB | 0.000+-0.000 | 0.000+-0.000 | n/a | no |
| 16 KB | 0.007+-0.006 | 0.010+-0.000 | +50.0% | no |
| 32 KB | 0.010+-0.000 | 0.010+-0.000 | +0.0% | no |
| 64 KB | 0.030+-0.000 | 0.020+-0.000 | -33.3% | no |
| 128 KB | 0.053+-0.006 | 0.060+-0.000 | +12.5% | no |
| 256 KB | 0.097+-0.023 | 0.117+-0.006 | +20.7% | no |
| 512 KB | 0.213+-0.025 | 0.210+-0.026 | -1.6% | no |
| 1 MB | 0.397+-0.006 | 0.387+-0.006 | -2.5% | no |
| 2 MB | 0.733+-0.029 | 0.710+-0.017 | -3.2% | no |
| 4 MB | 1.223+-0.085 | 1.267+-0.117 | +3.5% | no |
| 8 MB | 1.640+-0.448 | 1.423+-0.189 | -13.2% | no |
| 16 MB | 1.920+-0.098 | 1.857+-0.051 | -3.3% | no |
| 32 MB | 2.383+-0.846 | 2.587+-0.730 | +8.5% | no |
| 64 MB | 2.807+-0.553 | 3.173+-0.125 | +13.1% | no |
| 128 MB | 2.293+-0.242 | 2.223+-0.099 | -3.1% | no |

## Experiment B: AllReduce Channel Count Sweep
I added a 3-rep unforced baseline so the forced channel-count runs could be compared against actual `DEFAULT`, not just a forced `nch=4` spot check. The sweep supports the default 4-channel choice: `nch=1` and `nch=2` are worse, `nch=8` is flat to slightly worse, and forced `nch=4` mirrors default.

### 3-rep comparisons vs DEFAULT
| Size | DEFAULT (GB/s) | nch=1 (GB/s) | % vs DEFAULT | >2sigma | nch=8 (GB/s) | % vs DEFAULT | >2sigma |
| --- | --- | --- | --- | --- | --- | --- | --- |
| 1 MB | 0.240+-0.000 | 0.240+-0.000 | +0.0% | no | 0.240+-0.000 | +0.0% | no |
| 2 MB | 0.480+-0.000 | 0.480+-0.000 | +0.0% | no | 0.480+-0.000 | +0.0% | no |
| 4 MB | 0.960+-0.000 | 0.960+-0.000 | +0.0% | no | 0.960+-0.000 | +0.0% | no |
| 8 MB | 1.903+-0.012 | 0.960+-0.000 | -49.6% | no | 1.907+-0.006 | +0.2% | no |
| 16 MB | 2.187+-0.075 | 0.960+-0.000 | -56.1% | no | 2.097+-0.012 | -4.1% | no |
| 32 MB | 2.227+-0.035 | 0.960+-0.000 | -56.9% | no | 2.230+-0.010 | +0.1% | no |
| 64 MB | 2.260+-0.017 | 0.960+-0.000 | -57.5% | no | 2.227+-0.031 | -1.5% | no |
| 128 MB | 2.297+-0.025 | 0.960+-0.000 | -58.2% | no | 2.257+-0.012 | -1.7% | no |

### 1-rep spot checks
| Size | DEFAULT (GB/s) | nch=2 (GB/s) | % vs DEFAULT | nch=4 (GB/s) | % vs DEFAULT |
| --- | --- | --- | --- | --- | --- |
| 1 MB | 0.240+-0.000 | 0.240+-n/a | +0.0% | 0.240+-n/a | +0.0% |
| 2 MB | 0.480+-0.000 | 0.480+-n/a | +0.0% | 0.480+-n/a | +0.0% |
| 4 MB | 0.960+-0.000 | 0.960+-n/a | +0.0% | 0.960+-n/a | +0.0% |
| 8 MB | 1.903+-0.012 | 1.910+-n/a | +0.4% | 1.910+-n/a | +0.4% |
| 16 MB | 2.187+-0.075 | 1.920+-n/a | -12.2% | 2.170+-n/a | -0.8% |
| 32 MB | 2.227+-0.035 | 1.920+-n/a | -13.8% | 2.260+-n/a | +1.5% |
| 64 MB | 2.260+-0.017 | 1.920+-n/a | -15.0% | 2.310+-n/a | +2.2% |
| 128 MB | 2.297+-0.025 | 1.920+-n/a | -16.4% | 2.310+-n/a | +0.6% |

## Experiment C: P2P Enabled AllReduce
Removing `NCCL_P2P_DISABLE=1` did not create a medium-size or large-size `DEFAULT` failure. `Simple` is effectively tied with `DEFAULT` at the useful sizes; `Tree` is generally worse in its single-rep spot check.

### DEFAULT vs Simple (3 reps each)
| Size | DEFAULT (GB/s) | Simple (GB/s) | % vs DEFAULT | >2sigma |
| --- | --- | --- | --- | --- |
| 8 B | 0.000+-0.000 | 0.000+-0.000 | n/a | no |
| 16 B | 0.000+-0.000 | 0.000+-0.000 | n/a | no |
| 32 B | 0.000+-0.000 | 0.000+-0.000 | n/a | no |
| 64 B | 0.000+-0.000 | 0.000+-0.000 | n/a | no |
| 128 B | 0.000+-0.000 | 0.000+-0.000 | n/a | no |
| 256 B | 0.000+-0.000 | 0.000+-0.000 | n/a | no |
| 512 B | 0.000+-0.000 | 0.000+-0.000 | n/a | no |
| 1 KB | 0.000+-0.000 | 0.000+-0.000 | n/a | no |
| 2 KB | 0.000+-0.000 | 0.000+-0.000 | n/a | no |
| 4 KB | 0.000+-0.000 | 0.000+-0.000 | n/a | no |
| 8 KB | 0.000+-0.000 | 0.000+-0.000 | n/a | no |
| 16 KB | 0.000+-0.000 | 0.000+-0.000 | n/a | no |
| 32 KB | 0.010+-0.000 | 0.010+-0.000 | +0.0% | no |
| 64 KB | 0.010+-0.000 | 0.020+-0.000 | +100.0% | yes |
| 128 KB | 0.030+-0.000 | 0.030+-0.000 | +0.0% | no |
| 256 KB | 0.060+-0.000 | 0.060+-0.000 | +0.0% | no |
| 512 KB | 0.120+-0.000 | 0.120+-0.000 | +0.0% | no |
| 1 MB | 0.240+-0.000 | 0.240+-0.000 | +0.0% | no |
| 2 MB | 0.480+-0.000 | 0.480+-0.000 | +0.0% | no |
| 4 MB | 0.960+-0.000 | 0.960+-0.000 | +0.0% | no |
| 8 MB | 1.910+-0.000 | 1.903+-0.012 | -0.3% | no |
| 16 MB | 2.190+-0.017 | 2.173+-0.032 | -0.8% | no |
| 32 MB | 2.230+-0.010 | 2.260+-0.026 | +1.3% | no |
| 64 MB | 2.283+-0.012 | 2.283+-0.015 | +0.0% | no |
| 128 MB | 2.303+-0.006 | 2.307+-0.021 | +0.1% | no |

### Tree Spot Check (1 rep)
| Size | DEFAULT (GB/s) | Tree (GB/s) | % vs DEFAULT | >2sigma |
| --- | --- | --- | --- | --- |
| 8 B | 0.000+-0.000 | 0.000+-n/a | n/a | n/a |
| 16 B | 0.000+-0.000 | 0.000+-n/a | n/a | n/a |
| 32 B | 0.000+-0.000 | 0.000+-n/a | n/a | n/a |
| 64 B | 0.000+-0.000 | 0.000+-n/a | n/a | n/a |
| 128 B | 0.000+-0.000 | 0.000+-n/a | n/a | n/a |
| 256 B | 0.000+-0.000 | 0.000+-n/a | n/a | n/a |
| 512 B | 0.000+-0.000 | 0.000+-n/a | n/a | n/a |
| 1 KB | 0.000+-0.000 | 0.000+-n/a | n/a | n/a |
| 2 KB | 0.000+-0.000 | 0.000+-n/a | n/a | n/a |
| 4 KB | 0.000+-0.000 | 0.000+-n/a | n/a | n/a |
| 8 KB | 0.000+-0.000 | 0.000+-n/a | n/a | n/a |
| 16 KB | 0.000+-0.000 | 0.000+-n/a | n/a | n/a |
| 32 KB | 0.010+-0.000 | 0.010+-n/a | +0.0% | n/a |
| 64 KB | 0.010+-0.000 | 0.020+-n/a | +100.0% | n/a |
| 128 KB | 0.030+-0.000 | 0.030+-n/a | +0.0% | n/a |
| 256 KB | 0.060+-0.000 | 0.060+-n/a | +0.0% | n/a |
| 512 KB | 0.120+-0.000 | 0.120+-n/a | +0.0% | n/a |
| 1 MB | 0.240+-0.000 | 0.240+-n/a | +0.0% | n/a |
| 2 MB | 0.480+-0.000 | 0.480+-n/a | +0.0% | n/a |
| 4 MB | 0.960+-0.000 | 0.960+-n/a | +0.0% | n/a |
| 8 MB | 1.910+-0.000 | 1.710+-n/a | -10.5% | n/a |
| 16 MB | 2.190+-0.017 | 2.020+-n/a | -7.8% | n/a |
| 32 MB | 2.230+-0.010 | 2.220+-n/a | -0.4% | n/a |
| 64 MB | 2.283+-0.012 | 2.250+-n/a | -1.5% | n/a |
| 128 MB | 2.303+-0.006 | 2.200+-n/a | -4.5% | n/a |

## Experiment D: Reduce and Gather
`reduce_perf_mpi` showed a clean default transition from `LL` to `Simple` at tiny sizes only, so there was no medium-size `LL` selection to challenge. `gather_perf_mpi` did not emit per-size `Bytes -> Algo ...` lines in this build, so I ran a direct `DEFAULT` vs `Simple` vs `LL` sweep instead.

### Reduce Protocol Selections (from `NCCL_DEBUG_SUBSYS=TUNING`)
| Size | Algo | Proto | Channels |
| --- | --- | --- | --- |
| 1 KB | RING | LL | 0..0 |
| 4 KB | RING | LL | 0..0 |
| 16 KB | RING | LL | 0..1 |
| 64 KB | RING | SIMPLE | 0..1 |
| 256 KB | RING | SIMPLE | 0..3 |
| 1 MB | RING | SIMPLE | 0..3 |
| 4 MB | RING | SIMPLE | 0..3 |
| 16 MB | RING | SIMPLE | 0..3 |

### Gather Direct Sweep (3 reps each)
| Size | DEFAULT (GB/s) | Simple (GB/s) | % vs DEFAULT | >2sigma | LL (GB/s) | % vs DEFAULT | >2sigma |
| --- | --- | --- | --- | --- | --- | --- | --- |
| 1 KB | 0.017+-0.006 | 0.010+-0.000 | -40.0% | no | 0.023+-0.023 | +40.0% | no |
| 4 KB | 0.113+-0.075 | 0.070+-0.000 | -38.2% | no | 0.070+-0.000 | -38.2% | no |
| 16 KB | 0.267+-0.006 | 0.280+-0.000 | +5.0% | yes | 0.277+-0.006 | +3.8% | no |
| 64 KB | 0.993+-0.035 | 1.010+-0.036 | +1.7% | no | 1.010+-0.017 | +1.7% | no |
| 256 KB | 3.287+-1.276 | 3.363+-1.305 | +2.3% | no | 2.627+-0.090 | -20.1% | no |
| 1 MB | 2.983+-0.924 | 2.390+-0.946 | -19.9% | no | 1.273+-0.650 | -57.3% | no |
| 4 MB | 3.273+-0.465 | 3.277+-0.144 | +0.1% | no | 3.240+-0.666 | -1.0% | no |
| 16 MB | 3.940+-0.000 | 3.763+-0.280 | -4.5% | no | 4.103+-0.462 | +4.1% | no |

## Conclusion
Across Experiments A through D, I did not find a compelling medium-size or large-size case where NCCL `DEFAULT` was genuinely suboptimal on this exact setup. The only `>2sigma` wins for alternatives were tiny-message effects at heavily quantized bandwidth values, which are not strong evidence of a practically important tuner miss.
