# NCCLbpf paper artifact

This is the entry point for the published paper's implementation and evidence.
The submitted [PDF](../paper/paper.pdf), [source archive](../paper/paper-source.zip),
and [LaTeX source](../paper/paper.tex) are frozen. Their checksums are recorded in
[`SHA256SUMS`](SHA256SUMS).

## Check the paper files

From the repository root, run:

```sh
make artifact-check
make artifact-arxiv
```

`artifact-check` builds the arXiv bundle, checks the frozen file hashes,
compares every paper source and figure against the source archive, and compiles
both archives in temporary directories. It never rebuilds the checked-in PDF.
`artifact-arxiv` creates `docs/paper/arxiv-submission.tar.gz` from the existing
source archive. CI runs the check and uploads the frozen PDF,
source archive, and arXiv bundle as workflow artifacts.

On Debian/Ubuntu, the paper check needs `make`, `unzip`, `pdflatex`, and `bibtex`:

```sh
sudo apt-get update
sudo apt-get install -y --no-install-recommends make unzip zip \
  cm-super texlive-latex-base texlive-latex-extra texlive-publishers \
  texlive-bibtex-extra texlive-fonts-recommended
```

## Build and test the implementation

The top-level [README](../../README.md) gives the full prerequisites and build
instructions. The repository pins the NCCL, bpftime, libbpf, and bpftool
submodule revisions. CUDA and a compatible GPU are required for the full build.

```sh
git submodule update --init --recursive
# Build NCCL and bpftime as described in the top-level README.
make test
```

`make test` builds both NCCL plugins, runs the CTest policy/verifier/hot-reload
tests, and runs the hardware-free benchmark-driver regression. The driver test
can also run on a host without CUDA:

```sh
scripts/test_nccl_bench.sh
```

## Repeat a GPU benchmark

The paper's GPU evaluation used eight NVIDIA B300 GPUs with NVLink, NCCL
2.29.7, and nccl-tests 2.18.0. Its AllReduce runs used one nccl-tests process
with `-g 8`. Use the source-built NCCL from the pinned submodule and the built
policy plugin. The recorded command shape for the five independent launches
per arm was `-b 1M -e 8G -f 2 -g 8 -n 50 -w 10`:

```sh
export LD_LIBRARY_PATH="$PWD/nccl/build/lib${LD_LIBRARY_PATH:+:$LD_LIBRARY_PATH}"
export TEST_BIN="$PWD/nccl-tests/build/all_reduce_perf"
export NCCL_TUNER_PLUGIN="$PWD/src/nccl-policy-plugin/build/libnccl-policy.so"
export NCCL_POLICY_BPF_PATH="$PWD/src/nccl-policy-plugin/build/ebpf-policies/nvlink_ring_mid_v2.bpf.o"
mkdir -p scripts/results/paper-b300

for rep in 1 2 3 4 5; do
  env -u NCCL_TUNER_PLUGIN -u NCCL_POLICY_BPF_PATH \
    "$TEST_BIN" -b 1M -e 8G -f 2 -g 8 -n 50 -w 10 \
    > "scripts/results/paper-b300/baseline-r${rep}.log" 2>&1
  "$TEST_BIN" -b 1M -e 8G -f 2 -g 8 -n 50 -w 10 \
    > "scripts/results/paper-b300/policy-r${rep}.log" 2>&1
done
```

Inspect the plugin initialization lines and nccl-tests `#wrong` column in each
log. The recorded `nvlink_ring_mid_v2` run had cold-start variability; its
steady-state comparison used repetitions 4 and 5 (see
[`policy-v2-results.md`](../tmp/policy-v2-results.md)). Run baseline and policy
arms under the same hardware allocation; results from another topology need
not reproduce the paper's bandwidth numbers.

For the paper's 128 MiB AllGather stability comparison, use
`nccl-tests/build/all_gather_perf` with `-b 128M -e 128M -g 8 -n 50 -w 20`.
Launch the no-plugin and `nvlink_ring_mid_v2` configurations independently
20 times each, as recorded in
[`stability-allgather-summary.md`](../tmp/stability-allgather-summary.md).
The algorithm sweep uses the same eight-GPU AllReduce setup with
`NCCL_ALGO=Ring` as the environment-forced comparison. The verifier,
hot-reload, CPU-overhead, and injected-contention cases run in the
`test_ebpf_plugin` CTest executable; they do not require a GPU.

For later MPI or multi-host experiments, use `scripts/nccl_bench.sh` and set
`HOSTLIST` when needed. Its `selftest` mode prints the resolved command without
starting a benchmark, and its policy arms require a successful plugin-ready
marker from each MPI rank before labeling a result as a policy run. This runner
uses one GPU per MPI rank and is not the paper's original `-g 8` command.

## Evidence retained in this repository

| Topic | Files |
|---|---|
| Verifier and hot reload | [`phase3-safety-results.md`](../tmp/phase3-safety-results.md), [`eval-improvement-results.md`](../tmp/eval-improvement-results.md) |
| CPU policy overhead | [`benchmark-results.md`](../tmp/benchmark-results.md), [`phase4-results.md`](../tmp/phase4-results.md) |
| B300 AllReduce and algorithm sweep | [`experiment-results-b300.md`](../tmp/experiment-results-b300.md), [`policy-v2-results.md`](../tmp/policy-v2-results.md), [`protocol-sweep-results.md`](../tmp/protocol-sweep-results.md) |
| Profiler and tuner interaction | [`profiler-adapter-results.md`](../tmp/profiler-adapter-results.md), [`composability-v2-experiment.md`](../tmp/composability-v2-experiment.md) |
| AllGather stability | [`stability-allgather-summary.md`](../tmp/stability-allgather-summary.md) |
| Net plugin | [`src/nccl-net-ebpf-plugin/README.md`](../../src/nccl-net-ebpf-plugin/README.md), [`net-plugin-experiment.md`](../tmp/net-plugin-experiment.md) |

These are the tracked experimental summaries and captured output. Some notes
refer to raw logs under ignored `docs/tmp/` paths that were not committed;
those raw logs are not part of this repository. The table is an evidence index,
not a claim that all measurements can be independently recomputed from raw data.

The separate [GB300 NVL72 evaluation PR](https://github.com/datacrunch-research/nccl-eBPF/pull/1)
records later rack-scale work on a different testbed. It does not supply the
original paper's B300 measurements.

## Release

The paper artifact workflow checks the frozen files on pull requests and
`master` pushes. The release workflow publishes the frozen PDF, source archive,
and generated arXiv bundle when a new `v*` tag is pushed. It does not create
tags automatically.
