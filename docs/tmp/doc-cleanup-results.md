# Doc Cleanup Results

Date: 2026-03-09

## Summary

- Updated `docs/tmp/bpftime-migration-results.md` so it no longer presents the
  old warning-only verifier story as current.
- Added a correction note that Revise #1 fixed verifier map descriptor
  registration, so `adaptive_channels` and `slo_enforcer` now pass `strict`
  mode.
- Added a correction note that the old "could not confirm real 2-rank
  getCollInfo()" conclusion in the migration doc was later superseded by Phase
  4.

## NCCL Algo/Proto Verification

Run log:
- `docs/tmp/phase4-size-aware-v2-tuning-20260309.log`

Command used:
- Phase 4's 2-rank `mpirun` command, with `NCCL_DEBUG_SUBSYS=TUNING` added so
  NCCL emitted final selection lines such as
  `AllReduce: 65536 Bytes -> Algo RING proto LL ...`.

`size_aware_v2` policy rules:
- `n_bytes <= 32768`: `TREE`
- `n_bytes > 32768`: `RING`
- `n_bytes < 4096`: `SIMPLE`
- `4096 <= n_bytes < 1048576`: `LL`
- `n_bytes >= 1048576`: `SIMPLE`

Observed NCCL selections vs policy:

| Size(s) | Policy request | NCCL TUNING selection | Match |
| --- | --- | --- | --- |
| `1024`, `2048` | `TREE` + `SIMPLE` | `TREE` + `SIMPLE` | Yes |
| `4096`, `8192`, `16384`, `32768` | `TREE` + `LL` | `TREE` + `LL` | Yes |
| `65536`, `131072`, `262144`, `524288` | `RING` + `LL` | `RING` + `LL` | Yes |
| `1048576` | `RING` + `SIMPLE` | `RING` + `SIMPLE` | Yes |

Representative log lines:
- `AllReduce: 1024 Bytes -> Algo TREE proto SIMPLE channel{Lo..Hi}={0..0}`
- `AllReduce: 4096 Bytes -> Algo TREE proto LL channel{Lo..Hi}={0..0}`
- `AllReduce: 65536 Bytes -> Algo RING proto LL channel{Lo..Hi}={0..3}`
- `AllReduce: 1048576 Bytes -> Algo RING proto SIMPLE channel{Lo..Hi}={0..3}`

Conclusion:
- NCCL did pick the policy-requested `algo` and `proto` on the tested Phase 4
  path.
- Exact `n_channels` adoption is weaker. The plugin requested `channels=2` for
  `1024/2048`, `channels=4` for `4096..524288`, and `channels=8` for
  `1048576`, but NCCL's final scheduled channel ranges were `1, 1, 1, 2, 4,
  4, 4, 4, 4, 4, 4`. The policy clearly influenced channels, but the final
  channel count was not always the exact requested value.

## Remaining Inconsistencies

I treated explicitly historical review notes as historical unless they still use
present-tense wording that now conflicts with current docs.

Remaining issues:
- `docs/tmp/paper-readiness-review.md` still says
  `docs/tmp/bpftime-migration-results.md` contains the old warning-mode-only
  verifier story. That became stale after today's correction.
- `docs/tmp/phase4-results.md` treats the plugin's logged `channels=` value as
  if it were the final NCCL-applied channel count. The new tuning log shows the
  plugin log is the requested value, while NCCL's final scheduled channel range
  is smaller on small messages.
- `docs/tmp/paper-readiness-review.md` says the artifact does not fully prove
  that NCCL's final chosen `algo/proto/channel` matched the plugin request.
  That is now only partially true: today's tuning log proves final `algo` and
  `proto` on the tested path, but exact `channel` adoption still remains
  partial.

No other current verifier-mode contradictions were found in `docs/tmp/*.md`
after correcting `docs/tmp/bpftime-migration-results.md`.
